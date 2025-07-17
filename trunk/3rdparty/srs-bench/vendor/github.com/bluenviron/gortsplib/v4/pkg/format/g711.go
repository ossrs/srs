package format

import (
	"strconv"
	"strings"

	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtplpcm"
)

// G711 is the RTP format for the G711 codec, encoded with mu-law or A-law.
// Specification: https://datatracker.ietf.org/doc/html/rfc3551
type G711 struct {
	PayloadTyp   uint8
	MULaw        bool
	SampleRate   int
	ChannelCount int
}

func (f *G711) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	if ctx.payloadType == 0 {
		f.MULaw = true
		f.SampleRate = 8000
		f.ChannelCount = 1
		return nil
	}

	if ctx.payloadType == 8 {
		f.MULaw = false
		f.SampleRate = 8000
		f.ChannelCount = 1
		return nil
	}

	f.MULaw = (ctx.codec == "pcmu")

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
func (f *G711) Codec() string {
	return "G711"
}

// ClockRate implements Format.
func (f *G711) ClockRate() int {
	return f.SampleRate
}

// PayloadType implements Format.
func (f *G711) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *G711) RTPMap() string {
	ret := ""

	if f.MULaw {
		ret += "PCMU"
	} else {
		ret += "PCMA"
	}

	ret += "/" + strconv.FormatInt(int64(f.SampleRate), 10)

	if f.ChannelCount != 1 {
		ret += "/" + strconv.FormatInt(int64(f.ChannelCount), 10)
	}

	return ret
}

// FMTP implements Format.
func (f *G711) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *G711) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *G711) CreateDecoder() (*rtplpcm.Decoder, error) {
	d := &rtplpcm.Decoder{
		BitDepth:     8,
		ChannelCount: f.ChannelCount,
	}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *G711) CreateEncoder() (*rtplpcm.Encoder, error) {
	e := &rtplpcm.Encoder{
		PayloadType:  f.PayloadType(),
		BitDepth:     8,
		ChannelCount: f.ChannelCount,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
