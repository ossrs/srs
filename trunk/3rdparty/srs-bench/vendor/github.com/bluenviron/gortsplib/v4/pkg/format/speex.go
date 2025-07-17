package format

import (
	"fmt"
	"strconv"

	"github.com/pion/rtp"
)

// Speex is the RTP format for the Speex codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc5574
type Speex struct {
	PayloadTyp uint8
	SampleRate int
	VBR        *bool
}

func (f *Speex) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	sampleRate, err := strconv.ParseUint(ctx.clock, 10, 31)
	if err != nil {
		return err
	}
	f.SampleRate = int(sampleRate)

	for key, val := range ctx.fmtp {
		if key == "vbr" {
			if val != "on" && val != "off" {
				return fmt.Errorf("invalid vbr value: %v", val)
			}

			v := (val == "on")
			f.VBR = &v
		}
	}

	return nil
}

// Codec implements Format.
func (f *Speex) Codec() string {
	return "Speex"
}

// ClockRate implements Format.
func (f *Speex) ClockRate() int {
	return f.SampleRate
}

// PayloadType implements Format.
func (f *Speex) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *Speex) RTPMap() string {
	return "speex/" + strconv.FormatInt(int64(f.SampleRate), 10)
}

// FMTP implements Format.
func (f *Speex) FMTP() map[string]string {
	fmtp := make(map[string]string)

	if f.VBR != nil {
		if *f.VBR {
			fmtp["vbr"] = "on"
		} else {
			fmtp["vbr"] = "off"
		}
	}

	return fmtp
}

// PTSEqualsDTS implements Format.
func (f *Speex) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}
