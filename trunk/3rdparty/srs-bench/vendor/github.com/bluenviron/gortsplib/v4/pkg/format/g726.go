package format

import (
	"strconv"
	"strings"

	"github.com/pion/rtp"
)

// G726 is the RTP format for the G726 codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc3551
type G726 struct {
	PayloadTyp uint8
	BitRate    int
	BigEndian  bool
}

func (f *G726) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	if strings.HasPrefix(ctx.codec, "aal2-") {
		f.BigEndian = true
	}

	switch {
	case strings.HasSuffix(ctx.codec, "-16"):
		f.BitRate = 16
	case strings.HasSuffix(ctx.codec, "-24"):
		f.BitRate = 24
	case strings.HasSuffix(ctx.codec, "-32"):
		f.BitRate = 32
	default:
		f.BitRate = 40
	}

	return nil
}

// Codec implements Format.
func (f *G726) Codec() string {
	return "G726"
}

// ClockRate implements Format.
func (f *G726) ClockRate() int {
	return 8000
}

// PayloadType implements Format.
func (f *G726) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *G726) RTPMap() string {
	codec := ""

	if f.BigEndian {
		codec += "AAL2-"
	}

	return codec + "G726-" + strconv.FormatInt(int64(f.BitRate), 10) + "/8000"
}

// FMTP implements Format.
func (f *G726) FMTP() map[string]string {
	return nil
}

// PTSEqualsDTS implements Format.
func (f *G726) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}
