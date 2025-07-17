package format

import (
	"strconv"
	"strings"

	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpac3"
)

// AC3 is the RTP format for the AC-3 codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc4184
type AC3 struct {
	PayloadTyp   uint8
	SampleRate   int
	ChannelCount int
}

func (f *AC3) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	tmp := strings.SplitN(ctx.clock, "/", 2)

	tmp1, err := strconv.ParseUint(tmp[0], 10, 31)
	if err != nil {
		return err
	}
	f.SampleRate = int(tmp1)

	if len(tmp) >= 2 {
		tmp1, err := strconv.ParseUint(tmp[1], 10, 31)
		if err != nil {
			return err
		}
		f.ChannelCount = int(tmp1)
	} else {
		// RFC4184: If the "channels" parameter
		// is omitted, a default maximum value of 6 is implied.
		f.ChannelCount = 6
	}

	return nil
}

// Codec implements Format.
func (f *AC3) Codec() string {
	return "AC-3"
}

// ClockRate implements Format.
func (f *AC3) ClockRate() int {
	return f.SampleRate
}

// PayloadType implements Format.
func (f *AC3) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *AC3) RTPMap() string {
	return "AC3/" + strconv.FormatInt(int64(f.SampleRate), 10) +
		"/" + strconv.FormatInt(int64(f.ChannelCount), 10)
}

// FMTP implements Format.
func (f *AC3) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *AC3) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *AC3) CreateDecoder() (*rtpac3.Decoder, error) {
	d := &rtpac3.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *AC3) CreateEncoder() (*rtpac3.Encoder, error) {
	e := &rtpac3.Encoder{
		PayloadType: f.PayloadTyp,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
