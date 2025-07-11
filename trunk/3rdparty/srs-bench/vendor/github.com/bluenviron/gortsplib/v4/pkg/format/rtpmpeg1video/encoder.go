package rtpmpeg1video

import (
	"bytes"
	"crypto/rand"

	"github.com/pion/rtp"
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

func lenAggregated(slices [][]byte, slice []byte) int {
	n := 4 + len(slice)
	for _, fr := range slices {
		n += len(fr)
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

// Encoder is a RTP/MPEG-1/2 Video encoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc2250
type Encoder struct {
	// SSRC of packets (optional).
	// It defaults to a random value.
	SSRC *uint32

	// initial sequence number of packets (optional).
	// It defaults to a random value.
	InitialSequenceNumber *uint16

	// maximum size of packet payloads (optional).
	// It defaults to 1460.
	PayloadMaxSize int

	sequenceNumber uint16
}

// Init initializes the encoder.
func (e *Encoder) Init() error {
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

// Encode encodes frames into RTP packets.
func (e *Encoder) Encode(frame []byte) ([]*rtp.Packet, error) {
	var rets []*rtp.Packet
	var batch [][]byte

	var temporalReference uint16
	beginOfSequence := uint8(0)
	var frameType uint8

	for {
		var slice []byte
		end := bytes.Index(frame[4:], []byte{0, 0, 1})
		if end >= 0 {
			slice, frame = frame[:end+4], frame[end+4:]
		} else {
			slice, frame = frame, nil
		}

		if lenAggregated(batch, slice) <= e.PayloadMaxSize {
			batch = append(batch, slice)
		} else {
			// write current batch
			if batch != nil {
				pkts, err := e.writeBatch(batch,
					temporalReference,
					beginOfSequence,
					frameType)
				if err != nil {
					return nil, err
				}
				rets = append(rets, pkts...)
				beginOfSequence = 0
			}

			// initialize new batch
			batch = [][]byte{slice}
		}

		switch slice[3] {
		case 0:
			temporalReference = uint16(slice[4])<<2 | uint16(slice[5])>>6
			frameType = (slice[5] >> 3) & 0b111

		case 0xB8:
			beginOfSequence = 1
		}

		if frame == nil {
			break
		}
	}

	// write last batch
	pkts, err := e.writeBatch(batch,
		temporalReference,
		beginOfSequence,
		frameType)
	if err != nil {
		return nil, err
	}
	rets = append(rets, pkts...)

	rets[len(rets)-1].Marker = true

	return rets, nil
}

func (e *Encoder) writeBatch(
	slices [][]byte,
	temporalReference uint16,
	beginOfSequence uint8,
	frameType uint8,
) ([]*rtp.Packet, error) {
	if len(slices) != 1 || lenAggregated(slices, nil) < e.PayloadMaxSize {
		return e.writeAggregated(slices, temporalReference, beginOfSequence, frameType)
	}

	return e.writeFragmented(slices[0], temporalReference, beginOfSequence, frameType)
}

func (e *Encoder) writeFragmented(
	slice []byte,
	temporalReference uint16,
	beginOfSequence uint8,
	frameType uint8,
) ([]*rtp.Packet, error) {
	avail := e.PayloadMaxSize - 4
	le := len(slice)
	packetCount := packetCount(avail, le)

	ret := make([]*rtp.Packet, packetCount)
	le = avail
	start := uint8(1)
	end := uint8(0)

	for i := range ret {
		if i == (packetCount - 1) {
			le = len(slice)
			end = 1
		}

		payload := make([]byte, 4+le)
		payload[0] = byte(temporalReference >> 8)
		payload[1] = byte(temporalReference)
		payload[2] = beginOfSequence<<5 | start<<4 | end<<3 | frameType
		copy(payload[4:], slice)
		slice = slice[le:]

		ret[i] = &rtp.Packet{
			Header: rtp.Header{
				Version:        rtpVersion,
				PayloadType:    32,
				SequenceNumber: e.sequenceNumber,
				SSRC:           *e.SSRC,
			},
			Payload: payload,
		}

		e.sequenceNumber++
		start = 0
		beginOfSequence = 0
	}

	return ret, nil
}

func (e *Encoder) writeAggregated(
	slices [][]byte,
	temporalReference uint16,
	beginOfSequence uint8,
	frameType uint8,
) ([]*rtp.Packet, error) {
	payload := make([]byte, lenAggregated(slices, nil))

	payload[0] = byte(temporalReference >> 8)
	payload[1] = byte(temporalReference)
	payload[2] = beginOfSequence<<5 | 1<<4 | 1<<3 | frameType

	n := 4
	for _, slice := range slices {
		n += copy(payload[n:], slice)
	}

	pkt := &rtp.Packet{
		Header: rtp.Header{
			Version:        rtpVersion,
			PayloadType:    32,
			SequenceNumber: e.sequenceNumber,
			SSRC:           *e.SSRC,
		},
		Payload: payload,
	}

	e.sequenceNumber++

	return []*rtp.Packet{pkt}, nil
}
