package format

import (
	"encoding/base64"
	"fmt"
	"strconv"
	"strings"

	"github.com/pion/rtp"
)

// Vorbis is the RTP format for the Vorbis codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc5215
type Vorbis struct {
	PayloadTyp    uint8
	SampleRate    int
	ChannelCount  int
	Configuration []byte
}

func (f *Vorbis) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	tmp := strings.SplitN(ctx.clock, "/", 2)
	if len(tmp) != 2 {
		return fmt.Errorf("invalid clock (%v)", ctx.clock)
	}

	sampleRate, err := strconv.ParseUint(tmp[0], 10, 31)
	if err != nil {
		return err
	}
	f.SampleRate = int(sampleRate)

	channelCount, err := strconv.ParseUint(tmp[1], 10, 31)
	if err != nil {
		return err
	}
	f.ChannelCount = int(channelCount)

	for key, val := range ctx.fmtp {
		if key == "configuration" {
			conf, err := base64.StdEncoding.DecodeString(val)
			if err != nil {
				return fmt.Errorf("invalid config: %v", val)
			}

			f.Configuration = conf
		}
	}

	if f.Configuration == nil {
		return fmt.Errorf("config is missing")
	}

	return nil
}

// Codec implements Format.
func (f *Vorbis) Codec() string {
	return "Vorbis"
}

// ClockRate implements Format.
func (f *Vorbis) ClockRate() int {
	return f.SampleRate
}

// PayloadType implements Format.
func (f *Vorbis) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *Vorbis) RTPMap() string {
	return "VORBIS/" + strconv.FormatInt(int64(f.SampleRate), 10) +
		"/" + strconv.FormatInt(int64(f.ChannelCount), 10)
}

// FMTP implements Format.
func (f *Vorbis) FMTP() map[string]string {
	fmtp := map[string]string{
		"configuration": base64.StdEncoding.EncodeToString(f.Configuration),
	}

	return fmtp
}

// PTSEqualsDTS implements Format.
func (f *Vorbis) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}
