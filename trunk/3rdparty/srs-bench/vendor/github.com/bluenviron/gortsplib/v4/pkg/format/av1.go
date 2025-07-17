package format //nolint:dupl

import (
	"fmt"
	"strconv"

	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpav1"
)

// AV1 is the RTP format for the AV1 codec.
// Specification: https://aomediacodec.github.io/av1-rtp-spec/
type AV1 struct {
	PayloadTyp uint8
	LevelIdx   *int
	Profile    *int
	Tier       *int
}

func (f *AV1) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	for key, val := range ctx.fmtp {
		switch key {
		case "level-idx":
			n, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid level-idx: %v", val)
			}

			v2 := int(n)
			f.LevelIdx = &v2

		case "profile":
			n, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid profile: %v", val)
			}

			v2 := int(n)
			f.Profile = &v2

		case "tier":
			n, err := strconv.ParseUint(val, 10, 31)
			if err != nil {
				return fmt.Errorf("invalid tier: %v", val)
			}

			v2 := int(n)
			f.Tier = &v2
		}
	}

	return nil
}

// Codec implements Format.
func (f *AV1) Codec() string {
	return "AV1"
}

// ClockRate implements Format.
func (f *AV1) ClockRate() int {
	return 90000
}

// PayloadType implements Format.
func (f *AV1) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *AV1) RTPMap() string {
	return "AV1/90000"
}

// FMTP implements Format.
func (f *AV1) FMTP() map[string]string {
	fmtp := make(map[string]string)

	if f.LevelIdx != nil {
		fmtp["level-idx"] = strconv.FormatInt(int64(*f.LevelIdx), 10)
	}
	if f.Profile != nil {
		fmtp["profile"] = strconv.FormatInt(int64(*f.Profile), 10)
	}
	if f.Tier != nil {
		fmtp["tier"] = strconv.FormatInt(int64(*f.Tier), 10)
	}

	return fmtp
}

// PTSEqualsDTS implements Format.
func (f *AV1) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *AV1) CreateDecoder() (*rtpav1.Decoder, error) {
	d := &rtpav1.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *AV1) CreateEncoder() (*rtpav1.Encoder, error) {
	e := &rtpav1.Encoder{
		PayloadType: f.PayloadTyp,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
