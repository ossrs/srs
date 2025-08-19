package format

import (
	"encoding/hex"
	"fmt"
	"strconv"
	"strings"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format/rtpmpeg4audio"
)

// MPEG4Audio is the RTP format for a MPEG-4 Audio codec.
// Specification: https://datatracker.ietf.org/doc/html/rfc3640
// Specification: https://datatracker.ietf.org/doc/html/rfc6416#section-7.3
type MPEG4Audio struct {
	// payload type of packets.
	PayloadTyp uint8

	// use LATM format (RFC6416) instead of generic format (RFC3640).
	LATM bool

	// profile level ID.
	ProfileLevelID int

	// generic only
	Config           *mpeg4audio.Config
	SizeLength       int
	IndexLength      int
	IndexDeltaLength int

	// LATM only
	Bitrate         *int
	CPresent        bool
	StreamMuxConfig *mpeg4audio.StreamMuxConfig
	SBREnabled      *bool
}

func (f *MPEG4Audio) unmarshal(ctx *unmarshalContext) error {
	f.PayloadTyp = ctx.payloadType
	f.LATM = (ctx.codec != "mpeg4-generic")

	if !f.LATM {
		for key, val := range ctx.fmtp {
			switch key {
			case "streamtype":
				if val != "5" { // AudioStream in ISO 14496-1
					return fmt.Errorf("streamtype of AAC must be 5")
				}

			case "mode":
				if strings.ToLower(val) != "aac-hbr" && strings.ToLower(val) != "aac_hbr" {
					return fmt.Errorf("unsupported AAC mode: %v", val)
				}

			case "profile-level-id":
				tmp, err := strconv.ParseUint(val, 10, 31)
				if err != nil {
					return fmt.Errorf("invalid profile-level-id: %v", val)
				}

				f.ProfileLevelID = int(tmp)

			case "config":
				enc, err := hex.DecodeString(val)
				if err != nil {
					return fmt.Errorf("invalid AAC config: %v", val)
				}

				f.Config = &mpeg4audio.Config{}
				err = f.Config.Unmarshal(enc)
				if err != nil {
					return fmt.Errorf("invalid AAC config: %v", val)
				}

			case "sizelength":
				n, err := strconv.ParseUint(val, 10, 31)
				if err != nil || n > 100 {
					return fmt.Errorf("invalid AAC SizeLength: %v", val)
				}
				f.SizeLength = int(n)

			case "indexlength":
				n, err := strconv.ParseUint(val, 10, 31)
				if err != nil || n > 100 {
					return fmt.Errorf("invalid AAC IndexLength: %v", val)
				}
				f.IndexLength = int(n)

			case "indexdeltalength":
				n, err := strconv.ParseUint(val, 10, 31)
				if err != nil || n > 100 {
					return fmt.Errorf("invalid AAC IndexDeltaLength: %v", val)
				}
				f.IndexDeltaLength = int(n)
			}
		}

		if f.Config == nil {
			return fmt.Errorf("config is missing")
		}

		if f.SizeLength == 0 {
			return fmt.Errorf("sizelength is missing")
		}
	} else {
		// default value set by specification
		f.ProfileLevelID = 30
		f.CPresent = true

		for key, val := range ctx.fmtp {
			switch key {
			case "profile-level-id":
				tmp, err := strconv.ParseUint(val, 10, 31)
				if err != nil {
					return fmt.Errorf("invalid profile-level-id: %v", val)
				}

				f.ProfileLevelID = int(tmp)

			case "bitrate":
				tmp, err := strconv.ParseUint(val, 10, 31)
				if err != nil {
					return fmt.Errorf("invalid bitrate: %v", val)
				}

				v := int(tmp)
				f.Bitrate = &v

			case "cpresent":
				f.CPresent = (val == "1")

			case "config":
				enc, err := hex.DecodeString(val)
				if err != nil {
					return fmt.Errorf("invalid AAC config: %v", val)
				}

				f.StreamMuxConfig = &mpeg4audio.StreamMuxConfig{}
				err = f.StreamMuxConfig.Unmarshal(enc)
				if err != nil {
					return fmt.Errorf("invalid AAC config: %w", err)
				}

			case "sbr-enabled":
				v := (val == "1")
				f.SBREnabled = &v
			}
		}

		if f.CPresent {
			if f.StreamMuxConfig != nil {
				return fmt.Errorf("config and cpresent can't be used at the same time")
			}
		} else {
			if f.StreamMuxConfig == nil {
				return fmt.Errorf("config is missing")
			}
		}
	}

	return nil
}

// Codec implements Format.
func (f *MPEG4Audio) Codec() string {
	return "MPEG-4 Audio"
}

// ClockRate implements Format.
func (f *MPEG4Audio) ClockRate() int {
	if !f.LATM {
		return f.Config.SampleRate
	}
	if f.CPresent {
		return 16000
	}
	return f.StreamMuxConfig.Programs[0].Layers[0].AudioSpecificConfig.SampleRate
}

// PayloadType implements Format.
func (f *MPEG4Audio) PayloadType() uint8 {
	return f.PayloadTyp
}

// RTPMap implements Format.
func (f *MPEG4Audio) RTPMap() string {
	if !f.LATM {
		sampleRate := f.Config.SampleRate
		if f.Config.ExtensionSampleRate != 0 {
			sampleRate = f.Config.ExtensionSampleRate
		}

		channelCount := f.Config.ChannelCount
		if f.Config.ExtensionType == mpeg4audio.ObjectTypePS {
			channelCount = 2
		}

		return "mpeg4-generic/" + strconv.FormatInt(int64(sampleRate), 10) +
			"/" + strconv.FormatInt(int64(channelCount), 10)
	}

	if f.CPresent {
		return "MP4A-LATM/16000/1"
	}

	aoc := f.StreamMuxConfig.Programs[0].Layers[0].AudioSpecificConfig

	sampleRate := aoc.SampleRate
	if aoc.ExtensionSampleRate != 0 {
		sampleRate = aoc.ExtensionSampleRate
	}

	channelCount := aoc.ChannelCount
	if aoc.ExtensionType == mpeg4audio.ObjectTypePS {
		channelCount = 2
	}

	return "MP4A-LATM/" + strconv.FormatInt(int64(sampleRate), 10) +
		"/" + strconv.FormatInt(int64(channelCount), 10)
}

// FMTP implements Format.
func (f *MPEG4Audio) FMTP() map[string]string {
	if !f.LATM {
		enc, err := f.Config.Marshal()
		if err != nil {
			return nil
		}

		profileLevelID := f.ProfileLevelID
		if profileLevelID == 0 { // support legacy definition which didn't include profile-level-id
			profileLevelID = 1
		}

		fmtp := map[string]string{
			"streamtype":       "5",
			"mode":             "AAC-hbr",
			"profile-level-id": strconv.FormatInt(int64(profileLevelID), 10),
		}

		if f.SizeLength > 0 {
			fmtp["sizelength"] = strconv.FormatInt(int64(f.SizeLength), 10)
		}

		if f.IndexLength > 0 {
			fmtp["indexlength"] = strconv.FormatInt(int64(f.IndexLength), 10)
		}

		if f.IndexDeltaLength > 0 {
			fmtp["indexdeltalength"] = strconv.FormatInt(int64(f.IndexDeltaLength), 10)
		}

		fmtp["config"] = hex.EncodeToString(enc)

		return fmtp
	}

	fmtp := map[string]string{
		"profile-level-id": strconv.FormatInt(int64(f.ProfileLevelID), 10),
	}

	if f.Bitrate != nil {
		fmtp["bitrate"] = strconv.FormatInt(int64(*f.Bitrate), 10)
	}

	if f.CPresent {
		fmtp["cpresent"] = "1"
	} else {
		fmtp["cpresent"] = "0"

		enc, err := f.StreamMuxConfig.Marshal()
		if err != nil {
			return nil
		}

		fmtp["config"] = hex.EncodeToString(enc)
		fmtp["object"] = strconv.FormatInt(int64(f.StreamMuxConfig.Programs[0].Layers[0].AudioSpecificConfig.Type), 10)
	}

	if f.SBREnabled != nil {
		if *f.SBREnabled {
			fmtp["SBR-enabled"] = "1"
		} else {
			fmtp["SBR-enabled"] = "0"
		}
	}

	return fmtp
}

// PTSEqualsDTS implements Format.
func (f *MPEG4Audio) PTSEqualsDTS(*rtp.Packet) bool {
	return true
}

// CreateDecoder creates a decoder able to decode the content of the format.
func (f *MPEG4Audio) CreateDecoder() (*rtpmpeg4audio.Decoder, error) {
	d := &rtpmpeg4audio.Decoder{
		LATM:             f.LATM,
		SizeLength:       f.SizeLength,
		IndexLength:      f.IndexLength,
		IndexDeltaLength: f.IndexDeltaLength,
	}

	err := d.Init()
	if err != nil {
		return nil, err
	}

	return d, nil
}

// CreateEncoder creates an encoder able to encode the content of the format.
func (f *MPEG4Audio) CreateEncoder() (*rtpmpeg4audio.Encoder, error) {
	e := &rtpmpeg4audio.Encoder{
		LATM:             f.LATM,
		PayloadType:      f.PayloadTyp,
		SizeLength:       f.SizeLength,
		IndexLength:      f.IndexLength,
		IndexDeltaLength: f.IndexDeltaLength,
	}

	err := e.Init()
	if err != nil {
		return nil, err
	}

	return e, nil
}

// GetConfig returns the MPEG-4 Audio configuration.
func (f *MPEG4Audio) GetConfig() *mpeg4audio.Config {
	if !f.LATM {
		return f.Config
	}
	if f.CPresent {
		return nil
	}
	return f.StreamMuxConfig.Programs[0].Layers[0].AudioSpecificConfig
}
