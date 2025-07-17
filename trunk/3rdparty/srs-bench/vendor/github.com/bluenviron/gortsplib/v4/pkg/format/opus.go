package format

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpsimpleaudio"
)

// Opus is the RTP format for the Opus codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc7587
// Specification: https://webrtc-review.googlesource.com/c/src/+/129768
type Opus struct {
	PayloadTyp   uint8
	ChannelCount int

	//
	// Deprecated: replaced by ChannelCount.
	IsStereo bool
}

func (f *Opus) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType

	if ctx.codec == "opus" {
		tmp := strings.SplitN(ctx.clock, "/", 2)
		if len(tmp) != 2 {
			return fmt.Errorf("invalid clock (%v)", ctx.clock)
		}

		sampleRate, err := strconv.ParseUint(tmp[0], 10, 31)
		if err != nil || sampleRate != 48000 {
			return fmt.Errorf("invalid sample rate: '%s", tmp[0])
		}

		channelCount, err := strconv.ParseUint(tmp[1], 10, 31)
		if err != nil || channelCount != 2 {
			return fmt.Errorf("invalid channel count: '%s'", tmp[1])
		}

		// assume mono
		f.ChannelCount = 1
		f.IsStereo = false

		for key, val := range ctx.fmtp {
			if key == "sprop-stereo" {
				if val == "1" {
					f.ChannelCount = 2
					f.IsStereo = true
				}
			}
		}
	} else {
		tmp := strings.SplitN(ctx.clock, "/", 2)
		if len(tmp) != 2 {
			return fmt.Errorf("invalid clock (%v)", ctx.clock)
		}

		sampleRate, err := strconv.ParseUint(tmp[0], 10, 31)
		if err != nil || sampleRate != 48000 {
			return fmt.Errorf("invalid sample rate: '%s'", tmp[0])
		}

		channelCount, err := strconv.ParseUint(tmp[1], 10, 31)
		if err != nil {
			return fmt.Errorf("invalid channel count: '%s'", tmp[1])
		}

		f.ChannelCount = int(channelCount)
	}

	return nil
}

// Codec implements Format.
func (f *Opus) Codec() string {
	return "Opus"
}

// ClockRate implements Format.
func (f *Opus) ClockRate() int {
	// RFC7587: the RTP timestamp is incremented with a 48000 Hz
	// clock rate for all modes of Opus and all sampling rates.
	return 48000
}

// PayloadType implements Format.
func (f *Opus) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *Opus) RTPMap() string {
	if f.ChannelCount <= 2 {
		// RFC7587: The RTP clock rate in "a=rtpmap" MUST be 48000, and the
		// number of channels MUST be 2.
		return "opus/48000/2"
	}

	return "multiopus/48000/" + strconv.FormatUint(uint64(f.ChannelCount), 10)
}

// FMTP implements Format.
func (f *Opus) FMTP() map[string]string {
	if f.ChannelCount <= 2 {
		return map[string]string{
			"sprop-stereo": func() string {
				if f.ChannelCount == 2 || (f.ChannelCount == 0 && f.IsStereo) {
					return "1"
				}
				return "0"
			}(),
		}
	}

	switch f.ChannelCount {
	case 3:
		return map[string]string{
			"num_streams":          "2",
			"coupled_streams":      "1",
			"channel_mapping":      "0,2,1",
			"sprop-maxcapturerate": "48000",
		}

	case 4:
		return map[string]string{
			"num_streams":          "2",
			"coupled_streams":      "2",
			"channel_mapping":      "0,1,2,3",
			"sprop-maxcapturerate": "48000",
		}

	case 5:
		return map[string]string{
			"num_streams":          "3",
			"coupled_streams":      "2",
			"channel_mapping":      "0,4,1,2,3",
			"sprop-maxcapturerate": "48000",
		}

	case 6:
		return map[string]string{
			"num_streams":          "4",
			"coupled_streams":      "2",
			"channel_mapping":      "0,4,1,2,3,5",
			"sprop-maxcapturerate": "48000",
		}

	case 7:
		return map[string]string{
			"num_streams":          "4",
			"coupled_streams":      "3",
			"channel_mapping":      "0,4,1,2,3,5,6",
			"sprop-maxcapturerate": "48000",
		}

	default: // assume 8
		return map[string]string{
			"num_streams":          "5",
			"coupled_streams":      "3",
			"channel_mapping":      "0,6,1,4,5,2,3,7",
			"sprop-maxcapturerate": "48000",
		}
	}
}

// PTSEqualsDTS implements Format.
func (f *Opus) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *Opus) CreateDecoder() (*rtpsimpleaudio.Decoder, error) {
	d := &rtpsimpleaudio.Decoder{}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *Opus) CreateEncoder() (*rtpsimpleaudio.Encoder, error) {
	e := &rtpsimpleaudio.Encoder{
		PayloadType: f.PayloadTyp,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}
