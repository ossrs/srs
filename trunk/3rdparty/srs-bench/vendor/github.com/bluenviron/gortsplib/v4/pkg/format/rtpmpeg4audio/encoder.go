package rtpmpeg4audio

import (
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

// Encoder is a RTP/MPEG-4 audio encoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc3640
// Specification: https://datatracker.ietf.org/doc/html/rfc6416#section-7.3
type Encoder struct {
	// payload type of packets.
	PayloadType uint8

	// use RFC6416 (LATM) instead of RFC3640 (generic).
	LATM bool

	// The number of bits in which the AU-size field is encoded in the AU-header.
	SizeLength int

	// The number of bits in which the AU-Index is encoded in the first AU-header.
	IndexLength int

	// The number of bits in which the AU-Index-delta field is encoded in any non-first AU-header.
	IndexDeltaLength int

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

// Encode encodes AUs into RTP packets.
func (e *Encoder) Encode(aus [][]byte) ([]*rtp.Packet, error) {
	if !e.LATM {
		return e.encodeGeneric(aus)
	}
	return e.encodeLATM(aus)
}
