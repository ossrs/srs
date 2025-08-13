package format //nolint:dupl

import (
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpmjpeg"
)

// MJPEG is the RTP format for the Motion-JPEG codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc2435
type MJPEG struct {
	// in Go, empty structs share the same pointer,
	// therefore they cannot be used as map keys
	// or in equality operations. Prevent this.
	unused int //nolint:unused
}

func (f *MJPEG) unmarshal(_ *unmarshalContext) error {
	return nil
}

// Codec implements Format.
func (f *MJPEG) Codec() string {
	return "M-JPEG"
}

// ClockRate implements Format.
func (f *MJPEG) ClockRate() int {
	return 90000
}

// PayloadType implements Format.
func (f *MJPEG) PayloadType() uint8 {
	return 26
}

// RTPMap implements Format.
func (f *MJPEG) RTPMap() string {
	return "JPEG/90000"
}

// FMTP implements Format.
func (f *MJPEG) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *MJPEG) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *MJPEG) CreateDecoder() (*rtpmjpeg.Decoder, error) {
	d := &rtpmjpeg.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *MJPEG) CreateEncoder() (*rtpmjpeg.Encoder, error) {
	e := &rtpmjpeg.Encoder{}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
