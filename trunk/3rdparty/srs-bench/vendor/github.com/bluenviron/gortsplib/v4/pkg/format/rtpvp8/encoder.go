package rtpvp8

import (
	"crypto/rand"
	"fmt"

	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
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

// Encoder is a RTP/VP8 encoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc7741
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

	sequenceNumber uint16
	vp             codecs.VP8Payloader
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

// Encode encodes a VP8 frame into RTP/VP8 packets.
func (e *Encoder) Encode(frame []byte) ([]*rtp.Packet, error) {
	payloads := e.vp.Payload(uint16(e.PayloadMaxSize), frame)
	if payloads == nil {
		return nil, fmt.Errorf("payloader failed")
	}

	plen := len(payloads)
	ret := make([]*rtp.Packet, plen)

	for i, payload := range payloads {
		ret[i] = &rtp.Packet{
			Header: rtp.Header{
				Version:        rtpVersion,
				PayloadType:    e.PayloadType,
				SequenceNumber: e.sequenceNumber,
				SSRC:           *e.SSRC,
				Marker:         i == (plen - 1),
			},
			Payload: payload,
		}

		e.sequenceNumber++
	}

	return ret, nil
}
