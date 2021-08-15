package webrtc

import (
	"strings"
)

// RTPCodecType determines the type of a codec
type RTPCodecType int

const (

	// RTPCodecTypeAudio indicates this is an audio codec
	RTPCodecTypeAudio RTPCodecType = iota + 1

	// RTPCodecTypeVideo indicates this is a video codec
	RTPCodecTypeVideo
)

func (t RTPCodecType) String() string {
	switch t {
	case RTPCodecTypeAudio:
		return "audio"
	case RTPCodecTypeVideo:
		return "video" //nolint: goconst
	default:
		return ErrUnknownType.Error()
	}
}

// NewRTPCodecType creates a RTPCodecType from a string
func NewRTPCodecType(r string) RTPCodecType {
	switch {
	case strings.EqualFold(r, RTPCodecTypeAudio.String()):
		return RTPCodecTypeAudio
	case strings.EqualFold(r, RTPCodecTypeVideo.String()):
		return RTPCodecTypeVideo
	default:
		return RTPCodecType(0)
	}
}

// RTPCodecCapability provides information about codec capabilities.
//
// https://w3c.github.io/webrtc-pc/#dictionary-rtcrtpcodeccapability-members
type RTPCodecCapability struct {
	MimeType     string
	ClockRate    uint32
	Channels     uint16
	SDPFmtpLine  string
	RTCPFeedback []RTCPFeedback
}

// RTPHeaderExtensionCapability is used to define a RFC5285 RTP header extension supported by the codec.
//
// https://w3c.github.io/webrtc-pc/#dom-rtcrtpcapabilities-headerextensions
type RTPHeaderExtensionCapability struct {
	URI string
}

// RTPHeaderExtensionParameter represents a negotiated RFC5285 RTP header extension.
//
// https://w3c.github.io/webrtc-pc/#dictionary-rtcrtpheaderextensionparameters-members
type RTPHeaderExtensionParameter struct {
	URI string
	ID  int
}

// RTPCodecParameters is a sequence containing the media codecs that an RtpSender
// will choose from, as well as entries for RTX, RED and FEC mechanisms. This also
// includes the PayloadType that has been negotiated
//
// https://w3c.github.io/webrtc-pc/#rtcrtpcodecparameters
type RTPCodecParameters struct {
	RTPCodecCapability
	PayloadType PayloadType

	statsID string
}

// RTPParameters is a list of negotiated codecs and header extensions
//
// https://w3c.github.io/webrtc-pc/#dictionary-rtcrtpparameters-members
type RTPParameters struct {
	HeaderExtensions []RTPHeaderExtensionParameter
	Codecs           []RTPCodecParameters
}

type codecMatchType int

const (
	codecMatchNone    codecMatchType = 0
	codecMatchPartial codecMatchType = 1
	codecMatchExact   codecMatchType = 2
)

// Do a fuzzy find for a codec in the list of codecs
// Used for lookup up a codec in an existing list to find a match
// Returns codecMatchExact, codecMatchPartial, or codecMatchNone
func codecParametersFuzzySearch(needle RTPCodecParameters, haystack []RTPCodecParameters) (RTPCodecParameters, codecMatchType) {
	// First attempt to match on MimeType + SDPFmtpLine
	// Exact matches means fmtp line cannot be empty
	for _, c := range haystack {
		if strings.EqualFold(c.RTPCodecCapability.MimeType, needle.RTPCodecCapability.MimeType) &&
			c.RTPCodecCapability.SDPFmtpLine == needle.RTPCodecCapability.SDPFmtpLine {
			return c, codecMatchExact
		}
	}

	// Fallback to just MimeType if either haystack or needle does not have fmtpline set
	for _, c := range haystack {
		if strings.EqualFold(c.RTPCodecCapability.MimeType, needle.RTPCodecCapability.MimeType) &&
			(c.RTPCodecCapability.SDPFmtpLine == "" || needle.RTPCodecCapability.SDPFmtpLine == "") {
			return c, codecMatchPartial
		}
	}

	return RTPCodecParameters{}, codecMatchNone
}
