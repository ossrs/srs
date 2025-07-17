package rtptime

import (
	"crypto/rand"
	"time"
)

func divCeil(n, d uint64) uint64 {
	v := n / d
	if (n % d) != 0 {
		v++
	}
	return v
}

func randUint32() (uint32, error) {
	var b [4]byte
	_, err := rand.Read(b[:])
	if err != nil {
		return 0, err
	}
	return uint32(b[0])<<24 | uint32(b[1])<<16 | uint32(b[2])<<8 | uint32(b[3]), nil
}

// Encoder is a RTP timestamp encoder.
//
// Deprecated: not used anymore.
type Encoder struct {
	// Clock rate.
	ClockRate int

	// (optional) initial timestamp.
	// It defaults to a random value.
	InitialTimestamp *uint32

	clockRateTD        time.Duration
	initialTimestampTD time.Duration
}

// Initialize initializes an Encoder.
func (e *Encoder) Initialize() error {
	e.clockRateTD = time.Duration(e.ClockRate)

	if e.InitialTimestamp == nil {
		v, err := randUint32()
		if err != nil {
			return err
		}
		e.InitialTimestamp = &v
	}

	e.initialTimestampTD = time.Duration(divCeil(uint64(*e.InitialTimestamp)*uint64(time.Second), uint64(e.ClockRate)))

	return nil
}

// Encode encodes a timestamp.
func (e *Encoder) Encode(ts time.Duration) uint32 {
	ts += e.initialTimestampTD
	return uint32(multiplyAndDivide(ts, e.clockRateTD, time.Second))
}
