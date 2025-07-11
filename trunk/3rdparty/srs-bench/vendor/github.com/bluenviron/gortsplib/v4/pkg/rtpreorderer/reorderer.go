// Package rtpreorderer implements a filter to reorder incoming RTP packets.
package rtpreorderer

import (
	"github.com/pion/rtp"
)

const (
	bufferSize = 64
)

// Reorderer filters incoming RTP packets, in order to
// - order packets
// - remove duplicate packets
type Reorderer struct {
	initialized    bool
	expectedSeqNum uint16
	buffer         []*rtp.Packet
	absPos         uint16
	negativeCount  int
}

// New allocates a Reorderer.
//
// Deprecated: replaced by Initialize().
func New() *Reorderer {
	r := &Reorderer{}
	r.Initialize()
	return r
}

// Initialize initializes a Reorderer.
func (r *Reorderer) Initialize() {
	r.buffer = make([]*rtp.Packet, bufferSize)
}

// Process processes a RTP packet.
// It returns a sequence of ordered packets and the number of lost packets.
func (r *Reorderer) Process(pkt *rtp.Packet) ([]*rtp.Packet, uint) {
	if !r.initialized {
		r.initialized = true
		r.expectedSeqNum = pkt.SequenceNumber + 1
		return []*rtp.Packet{pkt}, 0
	}

	relPos := int16(pkt.SequenceNumber - r.expectedSeqNum)

	// packet is a duplicate or has been sent
	// before the first packet processed by Reorderer.
	// discard.
	if relPos < 0 {
		r.negativeCount++

		// stream has been resetted, therefore reset reorderer too
		if r.negativeCount > bufferSize {
			r.negativeCount = 0

			// clear buffer
			for i := uint16(0); i < bufferSize; i++ {
				p := (r.absPos + i) & (bufferSize - 1)
				r.buffer[p] = nil
			}

			// reset position
			r.expectedSeqNum = pkt.SequenceNumber + 1
			return []*rtp.Packet{pkt}, 0
		}

		return nil, 0
	}
	r.negativeCount = 0

	// there's a missing packet and buffer is full.
	// return entire buffer and clear it.
	if relPos >= bufferSize {
		n := 1
		for i := uint16(0); i < bufferSize; i++ {
			p := (r.absPos + i) & (bufferSize - 1)
			if r.buffer[p] != nil {
				n++
			}
		}

		ret := make([]*rtp.Packet, n)
		pos := 0

		for i := uint16(0); i < bufferSize; i++ {
			p := (r.absPos + i) & (bufferSize - 1)
			if r.buffer[p] != nil {
				ret[pos], r.buffer[p] = r.buffer[p], nil
				pos++
			}
		}

		ret[pos] = pkt

		r.expectedSeqNum = pkt.SequenceNumber + 1
		return ret, uint(int(relPos) - n + 1)
	}

	// there's a missing packet
	if relPos != 0 {
		p := (r.absPos + uint16(relPos)) & (bufferSize - 1)

		// current packet is a duplicate. discard
		if r.buffer[p] != nil {
			return nil, 0
		}

		// put current packet in buffer
		r.buffer[p] = pkt
		return nil, 0
	}

	// all packets have been received correctly.
	// return them

	n := uint16(1)
	for {
		p := (r.absPos + n) & (bufferSize - 1)
		if r.buffer[p] == nil {
			break
		}
		n++
	}

	ret := make([]*rtp.Packet, n)

	ret[0] = pkt
	r.absPos++
	r.absPos &= (bufferSize - 1)

	for i := uint16(1); i < n; i++ {
		ret[i], r.buffer[r.absPos] = r.buffer[r.absPos], nil
		r.absPos++
		r.absPos &= (bufferSize - 1)
	}

	r.expectedSeqNum = pkt.SequenceNumber + n

	return ret, 0
}
