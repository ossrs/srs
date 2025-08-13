// Package format contains RTP format definitions, decoders and encoders.
package format

import (
	"regexp"
	"strconv"
	"strings"

	"github.com/pion/rtp"
	psdp "github.com/pion/sdp/v3"
)

var (
	smartPayloadTypeRegexp = regexp.MustCompile("^smart/[0-9]/[0-9]+$")
	smartRtpmapRegexp      = regexp.MustCompile("^([0-9]+) (.+)/[0-9]+$")
)

func replaceSmartPayloadType(payloadType string, attributes []psdp.Attribute) string {
	re1 := smartPayloadTypeRegexp.FindStringSubmatch(payloadType)
	if re1 != nil {
		for _, attr := range attributes {
			if attr.Key == "rtpmap" {
				re2 := smartRtpmapRegexp.FindStringSubmatch(attr.Value)
				if re2 != nil {
					return re2[1]
				}
			}
		}
	}
	return payloadType
}

func getFormatAttribute(attributes []psdp.Attribute, payloadType uint8, key string) string {
	for _, attr := range attributes {
		if attr.Key == key {
			v := strings.TrimSpace(attr.Value)
			if parts := strings.SplitN(v, " ", 2); len(parts) == 2 {
				if tmp, err := strconv.ParseUint(parts[0], 10, 8); err == nil && uint8(tmp) == payloadType {
					return parts[1]
				}
			}
		}
	}
	return ""
}

func getCodecAndClock(rtpMap string) (string, string) {
	parts2 := strings.SplitN(rtpMap, "/", 2)
	if len(parts2) != 2 {
		return "", ""
	}

	return strings.ToLower(parts2[0]), parts2[1]
}

func decodeFMTP(enc string) map[string]string {
	if enc == "" {
		return nil
	}

	ret := make(map[string]string)

	for _, kv := range strings.Split(enc, ";") {
		kv = strings.Trim(kv, " ")

		if len(kv) == 0 {
			continue
		}

		tmp := strings.SplitN(kv, "=", 2)
		if len(tmp) != 2 {
			continue
		}

		ret[strings.ToLower(tmp[0])] = tmp[1]
	}

	return ret
}

type unmarshalContext struct {
	mediaType   string
	payloadType uint8
	clock       string
	codec       string
	rtpMap      string
	fmtp        map[string]string
}

// Format is a media format.
// It defines the payload type of RTP packets and how to encode/decode them.
type Format interface {
	unmarshal(ctx *unmarshalContext) error

	// Codec returns the codec name.
	Codec() string

	// ClockRate returns the clock rate.
	ClockRate() int

	// PayloadType returns the payload type.
	PayloadType() uint8

	// RTPMap returns the rtpmap attribute.
	RTPMap() string

	// FMTP returns the fmtp attribute.
	FMTP() map[string]string

	// PTSEqualsDTS checks whether PTS is equal to DTS in RTP packets.
	PTSEqualsDTS(*rtp.Packet) bool
}

// Unmarshal decodes a format from a media description.
func Unmarshal(md *psdp.MediaDescription, payloadTypeStr string) (Format, error) {
	mediaType := md.MediaName.Media
	payloadTypeStr = replaceSmartPayloadType(payloadTypeStr, md.Attributes)

	tmp, err := strconv.ParseUint(payloadTypeStr, 10, 8)
	if err != nil {
		return nil, err
	}
	payloadType := uint8(tmp)

	rtpMap := getFormatAttribute(md.Attributes, payloadType, "rtpmap")
	fmtp := decodeFMTP(getFormatAttribute(md.Attributes, payloadType, "fmtp"))
	codec, clock := getCodecAndClock(rtpMap)

	format := func() Format {
		switch {
		/*
		* dynamic payload types
		**/

		// video

		case codec == "av1" && clock == "90000" && payloadType >= 96 && payloadType <= 127:
			return &AV1{}

		case codec == "vp9" && clock == "90000" && payloadType >= 96 && payloadType <= 127:
			return &VP9{}

		case codec == "vp8" && clock == "90000" && payloadType >= 96 && payloadType <= 127:
			return &VP8{}

		case codec == "h265" && clock == "90000" && payloadType >= 96 && payloadType <= 127:
			return &H265{}

		case codec == "h264" && clock == "90000" && ((payloadType >= 96 && payloadType <= 127) || payloadType == 35):
			return &H264{}

		case codec == "mp4v-es" && clock == "90000" && payloadType >= 96 && payloadType <= 127:
			return &MPEG4Video{}

		// audio

		case codec == "opus", codec == "multiopus" && payloadType >= 96 && payloadType <= 127:
			return &Opus{}

		case codec == "vorbis" && payloadType >= 96 && payloadType <= 127:
			return &Vorbis{}

		case (codec == "mpeg4-generic" || codec == "mp4a-latm") && payloadType >= 96 && payloadType <= 127:
			return &MPEG4Audio{}

		case codec == "ac3" && payloadType >= 96 && payloadType <= 127:
			return &AC3{}

		case codec == "speex" && payloadType >= 96 && payloadType <= 127:
			return &Speex{}

		case (codec == "g726-16" ||
			codec == "g726-24" ||
			codec == "g726-32" ||
			codec == "g726-40" ||
			codec == "aal2-g726-16" ||
			codec == "aal2-g726-24" ||
			codec == "aal2-g726-32" ||
			codec == "aal2-g726-40") && clock == "8000" && payloadType >= 96 && payloadType <= 127:
			return &G726{}

		case codec == "pcma", codec == "pcmu" && payloadType >= 96 && payloadType <= 127:
			return &G711{}

		case codec == "l8", codec == "l16", codec == "l24" && payloadType >= 96 && payloadType <= 127:
			return &LPCM{}

		/*
		* static payload types
		**/

		// video

		case payloadType == 32:
			return &MPEG1Video{}

		case payloadType == 26:
			return &MJPEG{}

		case payloadType == 33:
			return &MPEGTS{}

		// audio

		case payloadType == 14:
			return &MPEG1Audio{}

		case payloadType == 9:
			return &G722{}

		case payloadType == 0, payloadType == 8:
			return &G711{}

		case payloadType == 10, payloadType == 11:
			return &LPCM{}
		}

		return &Generic{}
	}()

	err = format.unmarshal(&unmarshalContext{
		mediaType:   mediaType,
		payloadType: payloadType,
		clock:       clock,
		codec:       codec,
		rtpMap:      rtpMap,
		fmtp:        fmtp,
	})
	if err != nil {
		return nil, err
	}

	return format, nil
}
