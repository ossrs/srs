package format //nolint:dupl

import (
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpmpeg1audio"
)

// MPEG1Audio is the RTP format for a MPEG-1/2 Audio codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc2250
type MPEG1Audio struct {
	// in Go, empty structs share the same pointer,
	// therefore they cannot be used as map keys
	// or in equality operations. Prevent this.
	unused int //nolint:unused
}

func (f *MPEG1Audio) unmarshal(_ *unmarshalContext) error {
	return nil
}

// Codec implements Format.
func (f *MPEG1Audio) Codec() string {
	return "MPEG-1/2 Audio"
}

// ClockRate implements Format.
func (f *MPEG1Audio) ClockRate() int {
	return 90000
}

// PayloadType implements Format.
func (f *MPEG1Audio) PayloadType() uint8 {
	return 14
}

// RTPMap implements Format.
func (f *MPEG1Audio) RTPMap() string {
	return ""
}

// FMTP implements Format.
func (f *MPEG1Audio) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *MPEG1Audio) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *MPEG1Audio) CreateDecoder() (*rtpmpeg1audio.Decoder, error) {
	d := &rtpmpeg1audio.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *MPEG1Audio) CreateEncoder() (*rtpmpeg1audio.Encoder, error) {
	e := &rtpmpeg1audio.Encoder{}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
