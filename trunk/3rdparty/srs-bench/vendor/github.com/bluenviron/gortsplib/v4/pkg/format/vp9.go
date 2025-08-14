package format //nolint:dupl

import (
	"fmt"
	"strconv"

	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpvp9"
)

// VP9 is the RTP format for the VP9 codec.
// Specification: https://datatracker.ietf.org/doc/html/draft-ietf-payload-vp9-16
type VP9 struct {
	PayloadTyp uint8
	MaxFR      *int
	MaxFS      *int
	ProfileID  *int
}

func (f *VP9) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	for key, val := range ctx.fmtp {
		switch key {
		case "max-fr":
			n, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid max-fr: %v", val)
			}

			v2 := int(n)
			f.MaxFR = &v2

		case "max-fs":
			n, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid max-fs: %v", val)
			}

			v2 := int(n)
			f.MaxFS = &v2

		case "profile-id":
			n, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid profile-id: %v", val)
			}

			v2 := int(n)
			f.ProfileID = &v2
		}
	}

	return nil
}

// Codec implements Format.
func (f *VP9) Codec() string {
	return "VP9"
}

// ClockRate implements Format.
func (f *VP9) ClockRate() int {
	return 90000
}

// PayloadType implements Format.
func (f *VP9) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *VP9) RTPMap() string {
	return "VP9/90000"
}

// FMTP implements Format.
func (f *VP9) FMTP() map[string]string {
	fmtp := make(map[string]string)

	if f.MaxFR != nil {
		fmtp["max-fr"] = strconv.FormatInt(int64(*f.MaxFR), 10)
	}
	if f.MaxFS != nil {
		fmtp["max-fs"] = strconv.FormatInt(int64(*f.MaxFS), 10)
	}
	if f.ProfileID != nil {
		fmtp["profile-id"] = strconv.FormatInt(int64(*f.ProfileID), 10)
	}

	return fmtp
}

// PTSEqualsDTS implements Format.
func (f *VP9) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *VP9) CreateDecoder() (*rtpvp9.Decoder, error) {
	d := &rtpvp9.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *VP9) CreateEncoder() (*rtpvp9.Encoder, error) {
	e := &rtpvp9.Encoder{
		PayloadType: f.PayloadTyp,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
