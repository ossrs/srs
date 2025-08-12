package rtph264

import (
	"crypto/rand"
	"fmt"

	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h264"
)

const (
	rtpVersion            = 2
	defaultPayloadMaxSize = 1460 // 1500 (UDP MTU) - 20 (IP header) - 8 (UDP header) - 12 (RTP header)
)

func randUint32() (uint32, error) {
	var b [4]byte
	_, err := rand.Read(b[:])
	if err != nil {
		return 0, err
	}
	return uint32(b[0])<<24 | uint32(b[1])<<16 | uint32(b[2])<<8 | uint32(b[3]), nil
}

func lenAggregated(nalus [][]byte, addNALU []byte) int {
	n := 1 // header

	for _, nalu := range nalus {
		n += 2         // size
		n += len(nalu) // nalu
	}

	if addNALU != nil {
		n += 2            // size
		n += len(addNALU) // nalu
	}

	return n
}

func packetCount(avail, le int) int {
	n := le / avail
	if (le % avail) != 0 {
		n++
	}
	return n
}

// Encoder is a RTP/H264 encoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc6184
type Encoder struct {
	// payload type of packets.
	PayloadType uint8

	// SSRC of packets (optional).
	// It defaults to a random value.
	SSRC *uint32

	// initial sequence number of packets (optional).
	// It defaults to a random value.
	InitialSequenceNumber *uint16

	// maximum size of packet payloads (optional).
	// It defaults to 1460.
	PayloadMaxSize int

	PacketizationMode int

	sequenceNumber uint16
}

// Init initializes the encoder.
func (e *Encoder) Init() error {
	if e.PacketizationMode >= 2 {
		return fmt.Errorf("PacketizationMode >= 2 is not supported")
	}

	if e.SSRC == nil {
		v, err := randUint32()
		if err != nil {
			return err
		}
		e.SSRC = &v
	}
	if e.InitialSequenceNumber == nil {
		v, err := randUint32()
		if err != nil {
			return err
		}
		v2 := uint16(v)
		e.InitialSequenceNumber = &v2
	}
	if e.PayloadMaxSize == 0 {
		e.PayloadMaxSize = defaultPayloadMaxSize
	}

	e.sequenceNumber = *e.InitialSequenceNumber
	return nil
}

// Encode encodes an access unit into RTP/H264 packets.
func (e *Encoder) Encode(au [][]byte) ([]*rtp.Packet, error) {
	var rets []*rtp.Packet
	var batch [][]byte

	// split NALUs into batches
	for _, nalu := range au {
		if lenAggregated(batch, nalu) <= e.PayloadMaxSize {
			// add to existing batch
			batch = append(batch, nalu)
		} else {
			// write current batch
			if batch != nil {
				pkts, err := e.writeBatch(batch, false)
				if err != nil {
					return nil, err
				}
				rets = append(rets, pkts...)
			}

			// initialize new batch
			batch = [][]byte{nalu}
		}
	}

	// write final batch
	// marker is used to indicate when all NALUs with same PTS have been sent
	pkts, err := e.writeBatch(batch, true)
	if err != nil {
		return nil, err
	}
	rets = append(rets, pkts...)

	return rets, nil
}

func (e *Encoder) writeBatch(nalus [][]byte, marker bool) ([]*rtp.Packet, error) {
	if len(nalus) == 1 {
		// the NALU fits into a single RTP packet
		if len(nalus[0]) < e.PayloadMaxSize {
			return e.writeSingle(nalus[0], marker)
		}

		// split the NALU into multiple fragmentation packet
		return e.writeFragmented(nalus[0], marker)
	}

	return e.writeAggregated(nalus, marker)
}

func (e *Encoder) writeSingle(nalu []byte, marker bool) ([]*rtp.Packet, error) {
	pkt := &rtp.Packet{
		Header: rtp.Header{
			Version:        rtpVersion,
			PayloadType:    e.PayloadType,
			SequenceNumber: e.sequenceNumber,
			SSRC:           *e.SSRC,
			Marker:         marker,
		},
		Payload: nalu,
	}

	e.sequenceNumber++

	return []*rtp.Packet{pkt}, nil
}

func (e *Encoder) writeFragmented(nalu []byte, marker bool) ([]*rtp.Packet, error) {
	// use only FU-A, not FU-B, since we always use non-interleaved mode
	// (packetization-mode=1)
	avail := e.PayloadMaxSize - 2
	le := len(nalu) - 1
	packetCount := packetCount(avail, le)

	ret := make([]*rtp.Packet, packetCount)

	nri := (nalu[0] >> 5) & 0x03
	typ := nalu[0] & 0x1F
	nalu = nalu[1:] // remove header
	le = avail
	start := uint8(1)
	end := uint8(0)

	for i := range ret {
		if i == (packetCount - 1) {
			end = 1
			le = len(nalu)
		}

		data := make([]byte, 2+le)
		data[0] = (nri << 5) | uint8(h264.NALUTypeFUA)
		data[1] = (start << 7) | (end << 6) | typ
		copy(data[2:], nalu)
		nalu = nalu[le:]

		ret[i] = &rtp.Packet{
			Header: rtp.Header{
				Version:        rtpVersion,
				PayloadType:    e.PayloadType,
				SequenceNumber: e.sequenceNumber,
				SSRC:           *e.SSRC,
				Marker:         (i == (packetCount-1) && marker),
			},
			Payload: data,
		}

		e.sequenceNumber++
		start = 0
	}

	return ret, nil
}

func (e *Encoder) writeAggregated(nalus [][]byte, marker bool) ([]*rtp.Packet, error) {
	payload := make([]byte, lenAggregated(nalus, nil))

	// header
	payload[0] = uint8(h264.NALUTypeSTAPA)
	pos := 1

	for _, nalu := range nalus {
		// size
		naluLen := len(nalu)
		payload[pos] = uint8(naluLen >> 8)
		payload[pos+1] = uint8(naluLen)
		pos += 2

		// nalu
		copy(payload[pos:], nalu)
		pos += naluLen
	}

	pkt := &rtp.Packet{
		Header: rtp.Header{
			Version:        rtpVersion,
			PayloadType:    e.PayloadType,
			SequenceNumber: e.sequenceNumber,
			SSRC:           *e.SSRC,
			Marker:         marker,
		},
		Payload: payload,
	}

	e.sequenceNumber++

	return []*rtp.Packet{pkt}, nil
}
