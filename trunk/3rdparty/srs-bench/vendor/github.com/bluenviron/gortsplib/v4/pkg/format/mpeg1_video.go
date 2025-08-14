package format //nolint:dupl

import (
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpmpeg1video"
)

// MPEG1Video is the RTP format for a MPEG-1/2 Video codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc2250
type MPEG1Video struct {
	// in Go, empty structs share the same pointer,
	// therefore they cannot be used as map keys
	// or in equality operations. Prevent this.
	unused int //nolint:unused
}

func (f *MPEG1Video) unmarshal(_ *unmarshalContext) error {
	return nil
}

// Codec implements Format.
func (f *MPEG1Video) Codec() string {
	return "MPEG-1/2 Video"
}

// ClockRate implements Format.
func (f *MPEG1Video) ClockRate() int {
	return 90000
}

// PayloadType implements Format.
func (f *MPEG1Video) PayloadType() uint8 {
	return 32
}

// RTPMap implements Format.
func (f *MPEG1Video) RTPMap() string {
	return ""
}

// FMTP implements Format.
func (f *MPEG1Video) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *MPEG1Video) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *MPEG1Video) CreateDecoder() (*rtpmpeg1video.Decoder, error) {
	d := &rtpmpeg1video.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *MPEG1Video) CreateEncoder() (*rtpmpeg1video.Encoder, error) {
	e := &rtpmpeg1video.Encoder{}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
