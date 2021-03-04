// +build !js

package webrtc

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
	"github.com/pion/sdp/v3"
)

const (
	// MimeTypeH264 H264 MIME type.
	// Note: Matching should be case insensitive.
	MimeTypeH264 = "video/h264"
	// MimeTypeOpus Opus MIME type
	// Note: Matching should be case insensitive.
	MimeTypeOpus = "audio/opus"
	// MimeTypeVP8 VP8 MIME type
	// Note: Matching should be case insensitive.
	MimeTypeVP8 = "video/vp8"
	// MimeTypeVP9 VP9 MIME type
	// Note: Matching should be case insensitive.
	MimeTypeVP9 = "video/vp9"
	// MimeTypeG722 G722 MIME type
	// Note: Matching should be case insensitive.
	MimeTypeG722 = "audio/G722"
	// MimeTypePCMU PCMU MIME type
	// Note: Matching should be case insensitive.
	MimeTypePCMU = "audio/PCMU"
	// MimeTypePCMA PCMA MIME type
	// Note: Matching should be case insensitive.
	MimeTypePCMA = "audio/PCMA"
)

type mediaEngineHeaderExtension struct {
	uri              string
	isAudio, isVideo bool

	// If set only Transceivers of this direction are allowed
	allowedDirections []RTPTransceiverDirection
}

// A MediaEngine defines the codecs supported by a PeerConnection, and the
// configuration of those codecs. A MediaEngine must not be shared between
// PeerConnections.
type MediaEngine struct {
	// If we have attempted to negotiate a codec type yet.
	negotiatedVideo, negotiatedAudio bool

	videoCodecs, audioCodecs                     []RTPCodecParameters
	negotiatedVideoCodecs, negotiatedAudioCodecs []RTPCodecParameters

	headerExtensions           []mediaEngineHeaderExtension
	negotiatedHeaderExtensions map[int]mediaEngineHeaderExtension
}

// RegisterDefaultCodecs registers the default codecs supported by Pion WebRTC.
// RegisterDefaultCodecs is not safe for concurrent use.
func (m *MediaEngine) RegisterDefaultCodecs() error {
	// Default Pion Audio Codecs
	for _, codec := range []RTPCodecParameters{
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeOpus, 48000, 2, "minptime=10;useinbandfec=1", nil},
			PayloadType:        111,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeG722, 8000, 0, "", nil},
			PayloadType:        9,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypePCMU, 8000, 0, "", nil},
			PayloadType:        0,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypePCMA, 8000, 0, "", nil},
			PayloadType:        8,
		},
	} {
		if err := m.RegisterCodec(codec, RTPCodecTypeAudio); err != nil {
			return err
		}
	}

	// Default Pion Audio Header Extensions
	for _, extension := range []string{
		"urn:ietf:params:rtp-hdrext:sdes:mid",
		"urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id",
		"urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id",
	} {
		if err := m.RegisterHeaderExtension(RTPHeaderExtensionCapability{extension}, RTPCodecTypeAudio); err != nil {
			return err
		}
	}

	videoRTCPFeedback := []RTCPFeedback{{"goog-remb", ""}, {"ccm", "fir"}, {"nack", ""}, {"nack", "pli"}}
	for _, codec := range []RTPCodecParameters{
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeVP8, 90000, 0, "", videoRTCPFeedback},
			PayloadType:        96,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=96", nil},
			PayloadType:        97,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeVP9, 90000, 0, "profile-id=0", videoRTCPFeedback},
			PayloadType:        98,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=98", nil},
			PayloadType:        99,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeVP9, 90000, 0, "profile-id=1", videoRTCPFeedback},
			PayloadType:        100,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=100", nil},
			PayloadType:        101,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f", videoRTCPFeedback},
			PayloadType:        102,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=102", nil},
			PayloadType:        121,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f", videoRTCPFeedback},
			PayloadType:        127,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=127", nil},
			PayloadType:        120,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f", videoRTCPFeedback},
			PayloadType:        125,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=125", nil},
			PayloadType:        107,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f", videoRTCPFeedback},
			PayloadType:        108,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=108", nil},
			PayloadType:        109,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f", videoRTCPFeedback},
			PayloadType:        127,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=127", nil},
			PayloadType:        120,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeH264, 90000, 0, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640032", videoRTCPFeedback},
			PayloadType:        123,
		},
		{
			RTPCodecCapability: RTPCodecCapability{"video/rtx", 90000, 0, "apt=123", nil},
			PayloadType:        118,
		},

		{
			RTPCodecCapability: RTPCodecCapability{"video/ulpfec", 90000, 0, "", nil},
			PayloadType:        116,
		},
	} {
		if err := m.RegisterCodec(codec, RTPCodecTypeVideo); err != nil {
			return err
		}
	}

	// Default Pion Video Header Extensions
	for _, extension := range []string{
		"urn:ietf:params:rtp-hdrext:sdes:mid",
		"urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id",
		"urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id",
	} {
		if err := m.RegisterHeaderExtension(RTPHeaderExtensionCapability{extension}, RTPCodecTypeVideo); err != nil {
			return err
		}
	}

	return nil
}

// RegisterCodec adds codec to the MediaEngine
// These are the list of codecs supported by this PeerConnection.
// RegisterCodec is not safe for concurrent use.
func (m *MediaEngine) RegisterCodec(codec RTPCodecParameters, typ RTPCodecType) error {
	codec.statsID = fmt.Sprintf("RTPCodec-%d", time.Now().UnixNano())
	switch typ {
	case RTPCodecTypeAudio:
		m.audioCodecs = append(m.audioCodecs, codec)
	case RTPCodecTypeVideo:
		m.videoCodecs = append(m.videoCodecs, codec)
	default:
		return ErrUnknownType
	}
	return nil
}

// RegisterHeaderExtension adds a header extension to the MediaEngine
// To determine the negotiated value use `GetHeaderExtensionID` after signaling is complete
func (m *MediaEngine) RegisterHeaderExtension(extension RTPHeaderExtensionCapability, typ RTPCodecType, allowedDirections ...RTPTransceiverDirection) error {
	if m.negotiatedHeaderExtensions == nil {
		m.negotiatedHeaderExtensions = map[int]mediaEngineHeaderExtension{}
	}

	if len(allowedDirections) == 0 {
		allowedDirections = []RTPTransceiverDirection{RTPTransceiverDirectionRecvonly, RTPTransceiverDirectionSendonly}
	}

	for _, direction := range allowedDirections {
		if direction != RTPTransceiverDirectionRecvonly && direction != RTPTransceiverDirectionSendonly {
			return ErrRegisterHeaderExtensionInvalidDirection
		}
	}

	extensionIndex := -1
	for i := range m.headerExtensions {
		if extension.URI == m.headerExtensions[i].uri {
			extensionIndex = i
		}
	}

	if extensionIndex == -1 {
		m.headerExtensions = append(m.headerExtensions, mediaEngineHeaderExtension{})
		extensionIndex = len(m.headerExtensions) - 1
	}

	if typ == RTPCodecTypeAudio {
		m.headerExtensions[extensionIndex].isAudio = true
	} else if typ == RTPCodecTypeVideo {
		m.headerExtensions[extensionIndex].isVideo = true
	}

	m.headerExtensions[extensionIndex].uri = extension.URI
	m.headerExtensions[extensionIndex].allowedDirections = allowedDirections

	return nil
}

// RegisterFeedback adds feedback mechanism to already registered codecs.
func (m *MediaEngine) RegisterFeedback(feedback RTCPFeedback, typ RTPCodecType) {
	switch typ {
	case RTPCodecTypeVideo:
		for i, v := range m.videoCodecs {
			v.RTCPFeedback = append(v.RTCPFeedback, feedback)
			m.videoCodecs[i] = v
		}
	case RTPCodecTypeAudio:
		for i, v := range m.audioCodecs {
			v.RTCPFeedback = append(v.RTCPFeedback, feedback)
			m.audioCodecs[i] = v
		}
	}
}

// getHeaderExtensionID returns the negotiated ID for a header extension.
// If the Header Extension isn't enabled ok will be false
func (m *MediaEngine) getHeaderExtensionID(extension RTPHeaderExtensionCapability) (val int, audioNegotiated, videoNegotiated bool) {
	if m.negotiatedHeaderExtensions == nil {
		return 0, false, false
	}

	for id, h := range m.negotiatedHeaderExtensions {
		if extension.URI == h.uri {
			return id, h.isAudio, h.isVideo
		}
	}

	return
}

func (m *MediaEngine) getCodecByPayload(payloadType PayloadType) (RTPCodecParameters, RTPCodecType, error) {
	for _, codec := range m.negotiatedVideoCodecs {
		if codec.PayloadType == payloadType {
			return codec, RTPCodecTypeVideo, nil
		}
	}
	for _, codec := range m.negotiatedAudioCodecs {
		if codec.PayloadType == payloadType {
			return codec, RTPCodecTypeAudio, nil
		}
	}

	return RTPCodecParameters{}, 0, ErrCodecNotFound
}

func (m *MediaEngine) collectStats(collector *statsReportCollector) {
	statsLoop := func(codecs []RTPCodecParameters) {
		for _, codec := range codecs {
			collector.Collecting()
			stats := CodecStats{
				Timestamp:   statsTimestampFrom(time.Now()),
				Type:        StatsTypeCodec,
				ID:          codec.statsID,
				PayloadType: codec.PayloadType,
				MimeType:    codec.MimeType,
				ClockRate:   codec.ClockRate,
				Channels:    uint8(codec.Channels),
				SDPFmtpLine: codec.SDPFmtpLine,
			}

			collector.Collect(stats.ID, stats)
		}
	}

	statsLoop(m.videoCodecs)
	statsLoop(m.audioCodecs)
}

// Look up a codec and enable if it exists
func (m *MediaEngine) updateCodecParameters(remoteCodec RTPCodecParameters, typ RTPCodecType) error {
	codecs := m.videoCodecs
	if typ == RTPCodecTypeAudio {
		codecs = m.audioCodecs
	}

	pushCodec := func(codec RTPCodecParameters) error {
		if typ == RTPCodecTypeAudio {
			m.negotiatedAudioCodecs = append(m.negotiatedAudioCodecs, codec)
		} else if typ == RTPCodecTypeVideo {
			m.negotiatedVideoCodecs = append(m.negotiatedVideoCodecs, codec)
		}
		return nil
	}

	if strings.HasPrefix(remoteCodec.RTPCodecCapability.SDPFmtpLine, "apt=") {
		payloadType, err := strconv.Atoi(strings.TrimPrefix(remoteCodec.RTPCodecCapability.SDPFmtpLine, "apt="))
		if err != nil {
			return err
		}

		if _, _, err = m.getCodecByPayload(PayloadType(payloadType)); err != nil {
			return nil // not an error, we just ignore this codec we don't support
		}
	}

	if _, err := codecParametersFuzzySearch(remoteCodec, codecs); err == nil {
		return pushCodec(remoteCodec)
	}

	return nil
}

// Look up a header extension and enable if it exists
func (m *MediaEngine) updateHeaderExtension(id int, extension string, typ RTPCodecType) error {
	if m.negotiatedHeaderExtensions == nil {
		return nil
	}

	for _, localExtension := range m.headerExtensions {
		if localExtension.uri == extension {
			h := mediaEngineHeaderExtension{uri: extension, allowedDirections: localExtension.allowedDirections}
			if existingValue, ok := m.negotiatedHeaderExtensions[id]; ok {
				h = existingValue
			}

			switch {
			case localExtension.isAudio && typ == RTPCodecTypeAudio:
				h.isAudio = true
			case localExtension.isVideo && typ == RTPCodecTypeVideo:
				h.isVideo = true
			}

			m.negotiatedHeaderExtensions[id] = h
		}
	}
	return nil
}

// Update the MediaEngine from a remote description
func (m *MediaEngine) updateFromRemoteDescription(desc sdp.SessionDescription) error {
	for _, media := range desc.MediaDescriptions {
		var typ RTPCodecType
		switch {
		case !m.negotiatedAudio && strings.EqualFold(media.MediaName.Media, "audio"):
			m.negotiatedAudio = true
			typ = RTPCodecTypeAudio
		case !m.negotiatedVideo && strings.EqualFold(media.MediaName.Media, "video"):
			m.negotiatedVideo = true
			typ = RTPCodecTypeVideo
		default:
			continue
		}

		codecs, err := codecsFromMediaDescription(media)
		if err != nil {
			return err
		}

		for _, codec := range codecs {
			if err = m.updateCodecParameters(codec, typ); err != nil {
				return err
			}
		}

		extensions, err := rtpExtensionsFromMediaDescription(media)
		if err != nil {
			return err
		}

		for extension, id := range extensions {
			if err = m.updateHeaderExtension(id, extension, typ); err != nil {
				return err
			}
		}
	}
	return nil
}

func (m *MediaEngine) getCodecsByKind(typ RTPCodecType) []RTPCodecParameters {
	if typ == RTPCodecTypeVideo {
		if m.negotiatedVideo {
			return m.negotiatedVideoCodecs
		}

		return m.videoCodecs
	} else if typ == RTPCodecTypeAudio {
		if m.negotiatedAudio {
			return m.negotiatedAudioCodecs
		}

		return m.audioCodecs
	}

	return nil
}

func (m *MediaEngine) getRTPParametersByKind(typ RTPCodecType, directions []RTPTransceiverDirection) RTPParameters {
	headerExtensions := make([]RTPHeaderExtensionParameter, 0)

	if m.negotiatedVideo && typ == RTPCodecTypeVideo ||
		m.negotiatedAudio && typ == RTPCodecTypeAudio {
		for id, e := range m.negotiatedHeaderExtensions {
			if haveRTPTransceiverDirectionIntersection(e.allowedDirections, directions) && (e.isAudio && typ == RTPCodecTypeAudio || e.isVideo && typ == RTPCodecTypeVideo) {
				headerExtensions = append(headerExtensions, RTPHeaderExtensionParameter{ID: id, URI: e.uri})
			}
		}
	} else {
		for id, e := range m.headerExtensions {
			if haveRTPTransceiverDirectionIntersection(e.allowedDirections, directions) && (e.isAudio && typ == RTPCodecTypeAudio || e.isVideo && typ == RTPCodecTypeVideo) {
				headerExtensions = append(headerExtensions, RTPHeaderExtensionParameter{ID: id + 1, URI: e.uri})
			}
		}
	}

	return RTPParameters{
		HeaderExtensions: headerExtensions,
		Codecs:           m.getCodecsByKind(typ),
	}
}

func (m *MediaEngine) getRTPParametersByPayloadType(payloadType PayloadType) (RTPParameters, error) {
	codec, typ, err := m.getCodecByPayload(payloadType)
	if err != nil {
		return RTPParameters{}, err
	}

	headerExtensions := make([]RTPHeaderExtensionParameter, 0)
	for id, e := range m.negotiatedHeaderExtensions {
		if e.isAudio && typ == RTPCodecTypeAudio || e.isVideo && typ == RTPCodecTypeVideo {
			headerExtensions = append(headerExtensions, RTPHeaderExtensionParameter{ID: id, URI: e.uri})
		}
	}

	return RTPParameters{
		HeaderExtensions: headerExtensions,
		Codecs:           []RTPCodecParameters{codec},
	}, nil
}

func payloaderForCodec(codec RTPCodecCapability) (rtp.Payloader, error) {
	switch strings.ToLower(codec.MimeType) {
	case strings.ToLower(MimeTypeH264):
		return &codecs.H264Payloader{}, nil
	case strings.ToLower(MimeTypeOpus):
		return &codecs.OpusPayloader{}, nil
	case strings.ToLower(MimeTypeVP8):
		return &codecs.VP8Payloader{}, nil
	case strings.ToLower(MimeTypeVP9):
		return &codecs.VP9Payloader{}, nil
	case strings.ToLower(MimeTypeG722):
		return &codecs.G722Payloader{}, nil
	case strings.ToLower(MimeTypePCMU), strings.ToLower(MimeTypePCMA):
		return &codecs.G711Payloader{}, nil
	default:
		return nil, ErrNoPayloaderForCodec
	}
}
