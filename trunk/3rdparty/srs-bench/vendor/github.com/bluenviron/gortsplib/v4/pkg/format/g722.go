package format

import (
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpsimpleaudio"
)

// G722 is the RTP format for the G722 codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc3551
type G722 struct {
	// in Go, empty structs share the same pointer,
	// therefore they cannot be used as map keys
	// or in equality operations. Prevent this.
	unused int //nolint:unused
}

func (f *G722) unmarshal(_ *unmarshalContext) error {
	return nil
}

// Codec implements Format.
func (f *G722) Codec() string {
	return "G722"
}

// ClockRate implements Format.
func (f *G722) ClockRate() int {
	return 8000
}

// PayloadType implements Format.
func (f *G722) PayloadType() uint8 {
	return 9
}

// RTPMap implements Format.
func (f *G722) RTPMap() string {
	return "G722/8000"
}

// FMTP implements Format.
func (f *G722) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *G722) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *G722) CreateDecoder() (*rtpsimpleaudio.Decoder, error) {
	d := &rtpsimpleaudio.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *G722) CreateEncoder() (*rtpsimpleaudio.Encoder, error) {
	e := &rtpsimpleaudio.Encoder{
		PayloadType: 9,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
