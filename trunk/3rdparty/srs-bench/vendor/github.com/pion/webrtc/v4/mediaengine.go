// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
	"github.com/pion/sdp/v3"
	"github.com/pion/webrtc/v4/internal/fmtp"
)

type mediaEngineHeaderExtension struct {
	uri              string
	isAudio, isVideo bool

	// If set only Transceivers of this direction are allowed
	allowedDirections []RTPTransceiverDirection
}

// A MediaEngine defines the codecs supported by a PeerConnection, and the
// configuration of those codecs.
type MediaEngine struct {
	// If we have attempted to negotiate a codec type yet.
	negotiatedVideo, negotiatedAudio bool
	negotiateMultiCodecs             bool

	videoCodecs, audioCodecs                     []RTPCodecParameters
	negotiatedVideoCodecs, negotiatedAudioCodecs []RTPCodecParameters

	headerExtensions           []mediaEngineHeaderExtension
	negotiatedHeaderExtensions map[int]mediaEngineHeaderExtension

	mu sync.RWMutex
}

// setMultiCodecNegotiation enables or disables the negotiation of multiple codecs.
func (m *MediaEngine) setMultiCodecNegotiation(negotiateMultiCodecs bool) {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.negotiateMultiCodecs = negotiateMultiCodecs
}

// multiCodecNegotiation returns the current state of the negotiation of multiple codecs.
func (m *MediaEngine) multiCodecNegotiation() bool {
	m.mu.RLock()
	defer m.mu.RUnlock()

	return m.negotiateMultiCodecs
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
			PayloadType:        rtp.PayloadTypeG722,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypePCMU, 8000, 0, "", nil},
			PayloadType:        rtp.PayloadTypePCMU,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypePCMA, 8000, 0, "", nil},
			PayloadType:        rtp.PayloadTypePCMA,
		},
	} {
		if err := m.RegisterCodec(codec, RTPCodecTypeAudio); err != nil {
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
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=96", nil},
			PayloadType:        97,
		},

		{
			RTPCodecCapability: RTPCodecCapability{
				MimeTypeH264, 90000, 0,
				"level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f",
				videoRTCPFeedback,
			},
			PayloadType: 102,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=102", nil},
			PayloadType:        103,
		},

		{
			RTPCodecCapability: RTPCodecCapability{
				MimeTypeH264, 90000, 0,
				"level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f",
				videoRTCPFeedback,
			},
			PayloadType: 104,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=104", nil},
			PayloadType:        105,
		},

		{
			RTPCodecCapability: RTPCodecCapability{
				MimeTypeH264, 90000, 0,
				"level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f",
				videoRTCPFeedback,
			},
			PayloadType: 106,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=106", nil},
			PayloadType:        107,
		},

		{
			RTPCodecCapability: RTPCodecCapability{
				MimeTypeH264, 90000, 0,
				"level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f",
				videoRTCPFeedback,
			},
			PayloadType: 108,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=108", nil},
			PayloadType:        109,
		},

		{
			RTPCodecCapability: RTPCodecCapability{
				MimeTypeH264, 90000, 0,
				"level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f",
				videoRTCPFeedback,
			},
			PayloadType: 127,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=127", nil},
			PayloadType:        125,
		},

		{
			RTPCodecCapability: RTPCodecCapability{
				MimeTypeH264,
				90000, 0,
				"level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=4d001f",
				videoRTCPFeedback,
			},
			PayloadType: 39,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=39", nil},
			PayloadType:        40,
		},
		{
			RTPCodecCapability: RTPCodecCapability{
				MimeType:     MimeTypeH265,
				ClockRate:    90000,
				RTCPFeedback: videoRTCPFeedback,
			},
			PayloadType: 116,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=116", nil},
			PayloadType:        117,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeAV1, 90000, 0, "", videoRTCPFeedback},
			PayloadType:        45,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=45", nil},
			PayloadType:        46,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeVP9, 90000, 0, "profile-id=0", videoRTCPFeedback},
			PayloadType:        98,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=98", nil},
			PayloadType:        99,
		},

		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeVP9, 90000, 0, "profile-id=2", videoRTCPFeedback},
			PayloadType:        100,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=100", nil},
			PayloadType:        101,
		},

		{
			RTPCodecCapability: RTPCodecCapability{
				MimeTypeH264, 90000, 0,
				"level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f",
				videoRTCPFeedback,
			},
			PayloadType: 112,
		},
		{
			RTPCodecCapability: RTPCodecCapability{MimeTypeRTX, 90000, 0, "apt=112", nil},
			PayloadType:        113,
		},
	} {
		if err := m.RegisterCodec(codec, RTPCodecTypeVideo); err != nil {
			return err
		}
	}

	return nil
}

// addCodec will append codec if it not exists.
func (m *MediaEngine) addCodec(codecs []RTPCodecParameters, codec RTPCodecParameters) ([]RTPCodecParameters, error) {
	for _, c := range codecs {
		if c.PayloadType == codec.PayloadType {
			if strings.EqualFold(c.MimeType, codec.MimeType) &&
				fmtp.ClockRateEqual(c.MimeType, c.ClockRate, codec.ClockRate) &&
				fmtp.ChannelsEqual(c.MimeType, c.Channels, codec.Channels) {
				return codecs, nil
			}

			return codecs, ErrCodecAlreadyRegistered
		}
	}

	return append(codecs, codec), nil
}

// RegisterCodec adds codec to the MediaEngine
// These are the list of codecs supported by this PeerConnection.
func (m *MediaEngine) RegisterCodec(codec RTPCodecParameters, typ RTPCodecType) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var err error
	codec.statsID = fmt.Sprintf("RTPCodec-%d", time.Now().UnixNano())
	switch typ {
	case RTPCodecTypeAudio:
		m.audioCodecs, err = m.addCodec(m.audioCodecs, codec)
	case RTPCodecTypeVideo:
		m.videoCodecs, err = m.addCodec(m.videoCodecs, codec)
	default:
		return ErrUnknownType
	}

	return err
}

// RegisterHeaderExtension adds a header extension to the MediaEngine
// To determine the negotiated value use `GetHeaderExtensionID` after signaling is complete.
//
//nolint:cyclop
func (m *MediaEngine) RegisterHeaderExtension(
	extension RTPHeaderExtensionCapability,
	typ RTPCodecType,
	allowedDirections ...RTPTransceiverDirection,
) error {
	m.mu.Lock()
	defer m.mu.Unlock()

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
	m.mu.Lock()
	defer m.mu.Unlock()

	if typ == RTPCodecTypeVideo {
		for i, v := range m.videoCodecs {
			v.RTCPFeedback = append(v.RTCPFeedback, feedback)
			m.videoCodecs[i] = v
		}
	} else if typ == RTPCodecTypeAudio {
		for i, v := range m.audioCodecs {
			v.RTCPFeedback = append(v.RTCPFeedback, feedback)
			m.audioCodecs[i] = v
		}
	}
}

// getHeaderExtensionID returns the negotiated ID for a header extension.
// If the Header Extension isn't enabled ok will be false.
func (m *MediaEngine) getHeaderExtensionID(extension RTPHeaderExtensionCapability) (
	val int,
	audioNegotiated, videoNegotiated bool,
) {
	m.mu.RLock()
	defer m.mu.RUnlock()

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

// copy copies any user modifiable state of the MediaEngine
// all internal state is reset.
func (m *MediaEngine) copy() *MediaEngine {
	m.mu.Lock()
	defer m.mu.Unlock()
	cloned := &MediaEngine{
		videoCodecs:      append([]RTPCodecParameters{}, m.videoCodecs...),
		audioCodecs:      append([]RTPCodecParameters{}, m.audioCodecs...),
		headerExtensions: append([]mediaEngineHeaderExtension{}, m.headerExtensions...),
	}
	if len(m.headerExtensions) > 0 {
		cloned.negotiatedHeaderExtensions = map[int]mediaEngineHeaderExtension{}
	}

	return cloned
}

func findCodecByPayload(codecs []RTPCodecParameters, payloadType PayloadType) *RTPCodecParameters {
	for _, codec := range codecs {
		if codec.PayloadType == payloadType {
			return &codec
		}
	}

	return nil
}

func (m *MediaEngine) getCodecByPayload(payloadType PayloadType) (RTPCodecParameters, RTPCodecType, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	// if we've negotiated audio or video, check the negotiated types before our
	// built-in payload types, to ensure we pick the codec the other side wants.
	if m.negotiatedVideo {
		if codec := findCodecByPayload(m.negotiatedVideoCodecs, payloadType); codec != nil {
			return *codec, RTPCodecTypeVideo, nil
		}
	}
	if m.negotiatedAudio {
		if codec := findCodecByPayload(m.negotiatedAudioCodecs, payloadType); codec != nil {
			return *codec, RTPCodecTypeAudio, nil
		}
	}
	if !m.negotiatedVideo {
		if codec := findCodecByPayload(m.videoCodecs, payloadType); codec != nil {
			return *codec, RTPCodecTypeVideo, nil
		}
	}
	if !m.negotiatedAudio {
		if codec := findCodecByPayload(m.audioCodecs, payloadType); codec != nil {
			return *codec, RTPCodecTypeAudio, nil
		}
	}

	return RTPCodecParameters{}, 0, ErrCodecNotFound
}

func (m *MediaEngine) collectStats(collector *statsReportCollector) {
	m.mu.RLock()
	defer m.mu.RUnlock()

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
				Channels:    uint8(codec.Channels), //nolint:gosec // G115
				SDPFmtpLine: codec.SDPFmtpLine,
			}

			collector.Collect(stats.ID, stats)
		}
	}

	statsLoop(m.videoCodecs)
	statsLoop(m.audioCodecs)
}

// Look up a codec and enable if it exists.
//
//nolint:cyclop
func (m *MediaEngine) matchRemoteCodec(
	remoteCodec RTPCodecParameters,
	typ RTPCodecType,
	exactMatches, partialMatches []RTPCodecParameters,
) (RTPCodecParameters, codecMatchType, error) {
	codecs := m.videoCodecs
	if typ == RTPCodecTypeAudio {
		codecs = m.audioCodecs
	}

	remoteFmtp := fmtp.Parse(
		remoteCodec.RTPCodecCapability.MimeType,
		remoteCodec.RTPCodecCapability.ClockRate,
		remoteCodec.RTPCodecCapability.Channels,
		remoteCodec.RTPCodecCapability.SDPFmtpLine)

	if apt, hasApt := remoteFmtp.Parameter("apt"); hasApt { //nolint:nestif
		payloadType, err := strconv.ParseUint(apt, 10, 8)
		if err != nil {
			return RTPCodecParameters{}, codecMatchNone, err
		}

		aptMatch := codecMatchNone
		var aptCodec RTPCodecParameters
		for _, codec := range exactMatches {
			if codec.PayloadType == PayloadType(payloadType) {
				aptMatch = codecMatchExact
				aptCodec = codec

				break
			}
		}

		if aptMatch == codecMatchNone {
			for _, codec := range partialMatches {
				if codec.PayloadType == PayloadType(payloadType) {
					aptMatch = codecMatchPartial
					aptCodec = codec

					break
				}
			}
		}

		if aptMatch == codecMatchNone {
			return RTPCodecParameters{}, codecMatchNone, nil // not an error, we just ignore this codec we don't support
		}

		// replace the apt value with the original codec's payload type
		toMatchCodec := remoteCodec
		if aptMatched, mt := codecParametersFuzzySearch(aptCodec, codecs); mt == aptMatch {
			toMatchCodec.SDPFmtpLine = strings.Replace(
				toMatchCodec.SDPFmtpLine,
				fmt.Sprintf("apt=%d", payloadType),
				fmt.Sprintf("apt=%d", aptMatched.PayloadType),
				1,
			)
		}

		// if apt's media codec is partial match, then apt codec must be partial match too.
		localCodec, matchType := codecParametersFuzzySearch(toMatchCodec, codecs)
		if matchType == codecMatchExact && aptMatch == codecMatchPartial {
			matchType = codecMatchPartial
		}

		return localCodec, matchType, nil
	}

	localCodec, matchType := codecParametersFuzzySearch(remoteCodec, codecs)

	return localCodec, matchType, nil
}

// Update header extensions from a remote media section.
func (m *MediaEngine) updateHeaderExtensionFromMediaSection(media *sdp.MediaDescription) error {
	var typ RTPCodecType
	switch {
	case strings.EqualFold(media.MediaName.Media, "audio"):
		typ = RTPCodecTypeAudio
	case strings.EqualFold(media.MediaName.Media, "video"):
		typ = RTPCodecTypeVideo
	default:
		return nil
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

	return nil
}

// Look up a header extension and enable if it exists.
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

func (m *MediaEngine) pushCodecs(codecs []RTPCodecParameters, typ RTPCodecType) error {
	var joinedErr error
	for _, codec := range codecs {
		var err error
		if typ == RTPCodecTypeAudio {
			m.negotiatedAudioCodecs, err = m.addCodec(m.negotiatedAudioCodecs, codec)
		} else if typ == RTPCodecTypeVideo {
			m.negotiatedVideoCodecs, err = m.addCodec(m.negotiatedVideoCodecs, codec)
		}
		if err != nil {
			joinedErr = errors.Join(joinedErr, err)
		}
	}

	return joinedErr
}

// Update the MediaEngine from a remote description.
func (m *MediaEngine) updateFromRemoteDescription(desc sdp.SessionDescription) error { //nolint:cyclop,gocognit
	m.mu.Lock()
	defer m.mu.Unlock()

	for _, media := range desc.MediaDescriptions {
		var typ RTPCodecType

		switch {
		case strings.EqualFold(media.MediaName.Media, "audio"):
			typ = RTPCodecTypeAudio
		case strings.EqualFold(media.MediaName.Media, "video"):
			typ = RTPCodecTypeVideo
		}

		switch {
		case !m.negotiatedAudio && typ == RTPCodecTypeAudio:
			m.negotiatedAudio = true
		case !m.negotiatedVideo && typ == RTPCodecTypeVideo:
			m.negotiatedVideo = true
		default:
			// update header extesions from remote sdp if codec is negotiated, Firefox
			// would send updated header extension in renegotiation.
			// e.g. publish first track without simucalst ->negotiated-> publish second track with simucalst
			// then the two media secontions have different rtp header extensions in offer
			if err := m.updateHeaderExtensionFromMediaSection(media); err != nil {
				return err
			}

			if !m.negotiateMultiCodecs || (typ != RTPCodecTypeAudio && typ != RTPCodecTypeVideo) {
				continue
			}
		}

		codecs, err := codecsFromMediaDescription(media)
		if err != nil {
			return err
		}

		exactMatches := make([]RTPCodecParameters, 0, len(codecs))
		partialMatches := make([]RTPCodecParameters, 0, len(codecs))

		for _, remoteCodec := range codecs {
			localCodec, matchType, mErr := m.matchRemoteCodec(remoteCodec, typ, exactMatches, partialMatches)
			if mErr != nil {
				return mErr
			}

			remoteCodec.RTCPFeedback = rtcpFeedbackIntersection(localCodec.RTCPFeedback, remoteCodec.RTCPFeedback)

			if matchType == codecMatchExact {
				exactMatches = append(exactMatches, remoteCodec)
			} else if matchType == codecMatchPartial {
				partialMatches = append(partialMatches, remoteCodec)
			}
		}

		// use exact matches when they exist, otherwise fall back to partial
		switch {
		case len(exactMatches) > 0:
			err = m.pushCodecs(exactMatches, typ)
		case len(partialMatches) > 0:
			err = m.pushCodecs(partialMatches, typ)
		default:
			// no match, not negotiated
			continue
		}
		if err != nil {
			return err
		}

		if err := m.updateHeaderExtensionFromMediaSection(media); err != nil {
			return err
		}
	}

	return nil
}

func (m *MediaEngine) getCodecsByKind(typ RTPCodecType) []RTPCodecParameters {
	m.mu.RLock()
	defer m.mu.RUnlock()

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

//nolint:gocognit,cyclop
func (m *MediaEngine) getRTPParametersByKind(typ RTPCodecType, directions []RTPTransceiverDirection) RTPParameters {
	headerExtensions := make([]RTPHeaderExtensionParameter, 0)

	// perform before locking to prevent recursive RLocks
	foundCodecs := m.getCodecsByKind(typ)

	m.mu.RLock()
	defer m.mu.RUnlock()

	//nolint:nestif
	if (m.negotiatedVideo && typ == RTPCodecTypeVideo) || (m.negotiatedAudio && typ == RTPCodecTypeAudio) {
		for id, e := range m.negotiatedHeaderExtensions {
			if haveRTPTransceiverDirectionIntersection(e.allowedDirections, directions) &&
				(e.isAudio && typ == RTPCodecTypeAudio || e.isVideo && typ == RTPCodecTypeVideo) {
				headerExtensions = append(headerExtensions, RTPHeaderExtensionParameter{ID: id, URI: e.uri})
			}
		}
	} else {
		mediaHeaderExtensions := make(map[int]mediaEngineHeaderExtension)
		for _, ext := range m.headerExtensions {
			usingNegotiatedID := false
			for id := range m.negotiatedHeaderExtensions {
				if m.negotiatedHeaderExtensions[id].uri == ext.uri {
					usingNegotiatedID = true
					mediaHeaderExtensions[id] = ext

					break
				}
			}
			if !usingNegotiatedID {
				for id := 1; id < 15; id++ {
					idAvailable := true
					if _, ok := mediaHeaderExtensions[id]; ok {
						idAvailable = false
					}
					if _, taken := m.negotiatedHeaderExtensions[id]; idAvailable && !taken {
						mediaHeaderExtensions[id] = ext

						break
					}
				}
			}
		}

		for id, e := range mediaHeaderExtensions {
			if haveRTPTransceiverDirectionIntersection(e.allowedDirections, directions) &&
				(e.isAudio && typ == RTPCodecTypeAudio || e.isVideo && typ == RTPCodecTypeVideo) {
				headerExtensions = append(headerExtensions, RTPHeaderExtensionParameter{ID: id, URI: e.uri})
			}
		}
	}

	return RTPParameters{
		HeaderExtensions: headerExtensions,
		Codecs:           foundCodecs,
	}
}

func (m *MediaEngine) getRTPParametersByPayloadType(payloadType PayloadType) (RTPParameters, error) {
	codec, typ, err := m.getCodecByPayload(payloadType)
	if err != nil {
		return RTPParameters{}, err
	}

	m.mu.RLock()
	defer m.mu.RUnlock()
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
	case strings.ToLower(MimeTypeH265):
		return &codecs.H265Payloader{}, nil
	case strings.ToLower(MimeTypeOpus):
		return &codecs.OpusPayloader{}, nil
	case strings.ToLower(MimeTypeVP8):
		return &codecs.VP8Payloader{
			EnablePictureID: true,
		}, nil
	case strings.ToLower(MimeTypeVP9):
		return &codecs.VP9Payloader{}, nil
	case strings.ToLower(MimeTypeAV1):
		return &codecs.AV1Payloader{}, nil
	case strings.ToLower(MimeTypeG722):
		return &codecs.G722Payloader{}, nil
	case strings.ToLower(MimeTypePCMU), strings.ToLower(MimeTypePCMA):
		return &codecs.G711Payloader{}, nil
	default:
		return nil, ErrNoPayloaderForCodec
	}
}

func (m *MediaEngine) isRTXEnabled(typ RTPCodecType, directions []RTPTransceiverDirection) bool {
	for _, p := range m.getRTPParametersByKind(typ, directions).Codecs {
		if strings.EqualFold(p.MimeType, MimeTypeRTX) {
			return true
		}
	}

	return false
}

func (m *MediaEngine) isFECEnabled(typ RTPCodecType, directions []RTPTransceiverDirection) bool {
	for _, p := range m.getRTPParametersByKind(typ, directions).Codecs {
		if strings.Contains(strings.ToLower(p.MimeType), MimeTypeFlexFEC) {
			return true
		}
	}

	return false
}
