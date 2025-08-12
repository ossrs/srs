package format

import (
	"strconv"
	"strings"

	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtplpcm"
)

// LPCM is the RTP format for the LPCM codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc3190
// Specification: https://datatracker.ietf.org/doc/html/rfc3551
type LPCM struct {
	PayloadTyp   uint8
	BitDepth     int
	SampleRate   int
	ChannelCount int
}

func (f *LPCM) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	if ctx.payloadType == 10 {
		f.BitDepth = 16
		f.SampleRate = 44100
		f.ChannelCount = 2
		return nil
	}

	if ctx.payloadType == 11 {
		f.BitDepth = 16
		f.SampleRate = 44100
		f.ChannelCount = 1
		return nil
	}

	switch ctx.codec {
	case "l8":
		f.BitDepth = 8

	case "l16":
		f.BitDepth = 16

	case "l24":
		f.BitDepth = 24
	}

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
		f.ChannelCount = 1
	}

	return nil
}

// Codec implements Format.
func (f *LPCM) Codec() string {
	return "LPCM"
}

// ClockRate implements Format.
func (f *LPCM) ClockRate() int {
	return f.SampleRate
}

// PayloadType implements Format.
func (f *LPCM) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *LPCM) RTPMap() string {
	var codec string
	switch f.BitDepth {
	case 8:
		codec = "L8"

	case 16:
		codec = "L16"

	case 24:
		codec = "L24"
	}

	return codec + "/" + strconv.FormatInt(int64(f.SampleRate), 10) +
		"/" + strconv.FormatInt(int64(f.ChannelCount), 10)
}

// FMTP implements Format.
func (f *LPCM) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *LPCM) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *LPCM) CreateDecoder() (*rtplpcm.Decoder, error) {
	d := &rtplpcm.Decoder{
		BitDepth:     f.BitDepth,
		ChannelCount: f.ChannelCount,
	}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *LPCM) CreateEncoder() (*rtplpcm.Encoder, error) {
	e := &rtplpcm.Encoder{
		PayloadType:  f.PayloadTyp,
		BitDepth:     f.BitDepth,
		ChannelCount: f.ChannelCount,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
