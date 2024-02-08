// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"errors"
	"fmt"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"sync/atomic"

	"github.com/pion/ice/v2"
	"github.com/pion/logging"
	"github.com/pion/sdp/v3"
)

// trackDetails represents any media source that can be represented in a SDP
// This isn't keyed by SSRC because it also needs to support rid based sources
type trackDetails struct {
	mid        string
	kind       RTPCodecType
	streamID   string
	id         string
	ssrcs      []SSRC
	repairSsrc *SSRC
	rids       []string
}

func trackDetailsForSSRC(trackDetails []trackDetails, ssrc SSRC) *trackDetails {
	for i := range trackDetails {
		for j := range trackDetails[i].ssrcs {
			if trackDetails[i].ssrcs[j] == ssrc {
				return &trackDetails[i]
			}
		}
	}
	return nil
}

func trackDetailsForRID(trackDetails []trackDetails, mid, rid string) *trackDetails {
	for i := range trackDetails {
		if trackDetails[i].mid != mid {
			continue
		}

		for j := range trackDetails[i].rids {
			if trackDetails[i].rids[j] == rid {
				return &trackDetails[i]
			}
		}
	}
	return nil
}

func filterTrackWithSSRC(incomingTracks []trackDetails, ssrc SSRC) []trackDetails {
	filtered := []trackDetails{}
	doesTrackHaveSSRC := func(t trackDetails) bool {
		for i := range t.ssrcs {
			if t.ssrcs[i] == ssrc {
				return true
			}
		}

		return false
	}

	for i := range incomingTracks {
		if !doesTrackHaveSSRC(incomingTracks[i]) {
			filtered = append(filtered, incomingTracks[i])
		}
	}

	return filtered
}

// extract all trackDetails from an SDP.
func trackDetailsFromSDP(log logging.LeveledLogger, s *sdp.SessionDescription) (incomingTracks []trackDetails) { // nolint:gocognit
	for _, media := range s.MediaDescriptions {
		tracksInMediaSection := []trackDetails{}
		rtxRepairFlows := map[uint64]uint64{}

		// Plan B can have multiple tracks in a signle media section
		streamID := ""
		trackID := ""

		// If media section is recvonly or inactive skip
		if _, ok := media.Attribute(sdp.AttrKeyRecvOnly); ok {
			continue
		} else if _, ok := media.Attribute(sdp.AttrKeyInactive); ok {
			continue
		}

		midValue := getMidValue(media)
		if midValue == "" {
			continue
		}

		codecType := NewRTPCodecType(media.MediaName.Media)
		if codecType == 0 {
			continue
		}

		for _, attr := range media.Attributes {
			switch attr.Key {
			case sdp.AttrKeySSRCGroup:
				split := strings.Split(attr.Value, " ")
				if split[0] == sdp.SemanticTokenFlowIdentification {
					// Add rtx ssrcs to blacklist, to avoid adding them as tracks
					// Essentially lines like `a=ssrc-group:FID 2231627014 632943048` are processed by this section
					// as this declares that the second SSRC (632943048) is a rtx repair flow (RFC4588) for the first
					// (2231627014) as specified in RFC5576
					if len(split) == 3 {
						baseSsrc, err := strconv.ParseUint(split[1], 10, 32)
						if err != nil {
							log.Warnf("Failed to parse SSRC: %v", err)
							continue
						}
						rtxRepairFlow, err := strconv.ParseUint(split[2], 10, 32)
						if err != nil {
							log.Warnf("Failed to parse SSRC: %v", err)
							continue
						}
						rtxRepairFlows[rtxRepairFlow] = baseSsrc
						tracksInMediaSection = filterTrackWithSSRC(tracksInMediaSection, SSRC(rtxRepairFlow)) // Remove if rtx was added as track before
					}
				}

			// Handle `a=msid:<stream_id> <track_label>` for Unified plan. The first value is the same as MediaStream.id
			// in the browser and can be used to figure out which tracks belong to the same stream. The browser should
			// figure this out automatically when an ontrack event is emitted on RTCPeerConnection.
			case sdp.AttrKeyMsid:
				split := strings.Split(attr.Value, " ")
				if len(split) == 2 {
					streamID = split[0]
					trackID = split[1]
				}

			case sdp.AttrKeySSRC:
				split := strings.Split(attr.Value, " ")
				ssrc, err := strconv.ParseUint(split[0], 10, 32)
				if err != nil {
					log.Warnf("Failed to parse SSRC: %v", err)
					continue
				}

				if _, ok := rtxRepairFlows[ssrc]; ok {
					continue // This ssrc is a RTX repair flow, ignore
				}

				if len(split) == 3 && strings.HasPrefix(split[1], "msid:") {
					streamID = split[1][len("msid:"):]
					trackID = split[2]
				}

				isNewTrack := true
				trackDetails := &trackDetails{}
				for i := range tracksInMediaSection {
					for j := range tracksInMediaSection[i].ssrcs {
						if tracksInMediaSection[i].ssrcs[j] == SSRC(ssrc) {
							trackDetails = &tracksInMediaSection[i]
							isNewTrack = false
						}
					}
				}

				trackDetails.mid = midValue
				trackDetails.kind = codecType
				trackDetails.streamID = streamID
				trackDetails.id = trackID
				trackDetails.ssrcs = []SSRC{SSRC(ssrc)}

				for r, baseSsrc := range rtxRepairFlows {
					if baseSsrc == ssrc {
						repairSsrc := SSRC(r)
						trackDetails.repairSsrc = &repairSsrc
					}
				}

				if isNewTrack {
					tracksInMediaSection = append(tracksInMediaSection, *trackDetails)
				}
			}
		}

		if rids := getRids(media); len(rids) != 0 && trackID != "" && streamID != "" {
			simulcastTrack := trackDetails{
				mid:      midValue,
				kind:     codecType,
				streamID: streamID,
				id:       trackID,
				rids:     []string{},
			}
			for rid := range rids {
				simulcastTrack.rids = append(simulcastTrack.rids, rid)
			}

			tracksInMediaSection = []trackDetails{simulcastTrack}
		}

		incomingTracks = append(incomingTracks, tracksInMediaSection...)
	}

	return incomingTracks
}

func trackDetailsToRTPReceiveParameters(t *trackDetails) RTPReceiveParameters {
	encodingSize := len(t.ssrcs)
	if len(t.rids) >= encodingSize {
		encodingSize = len(t.rids)
	}

	encodings := make([]RTPDecodingParameters, encodingSize)
	for i := range encodings {
		if len(t.rids) > i {
			encodings[i].RID = t.rids[i]
		}
		if len(t.ssrcs) > i {
			encodings[i].SSRC = t.ssrcs[i]
		}

		if t.repairSsrc != nil {
			encodings[i].RTX.SSRC = *t.repairSsrc
		}
	}

	return RTPReceiveParameters{Encodings: encodings}
}

func getRids(media *sdp.MediaDescription) map[string]string {
	rids := map[string]string{}
	for _, attr := range media.Attributes {
		if attr.Key == sdpAttributeRid {
			split := strings.Split(attr.Value, " ")
			rids[split[0]] = attr.Value
		}
	}
	return rids
}

func addCandidatesToMediaDescriptions(candidates []ICECandidate, m *sdp.MediaDescription, iceGatheringState ICEGatheringState) error {
	appendCandidateIfNew := func(c ice.Candidate, attributes []sdp.Attribute) {
		marshaled := c.Marshal()
		for _, a := range attributes {
			if marshaled == a.Value {
				return
			}
		}

		m.WithValueAttribute("candidate", marshaled)
	}

	for _, c := range candidates {
		candidate, err := c.toICE()
		if err != nil {
			return err
		}

		candidate.SetComponent(1)
		appendCandidateIfNew(candidate, m.Attributes)

		candidate.SetComponent(2)
		appendCandidateIfNew(candidate, m.Attributes)
	}

	if iceGatheringState != ICEGatheringStateComplete {
		return nil
	}
	for _, a := range m.Attributes {
		if a.Key == "end-of-candidates" {
			return nil
		}
	}

	m.WithPropertyAttribute("end-of-candidates")
	return nil
}

func addDataMediaSection(d *sdp.SessionDescription, shouldAddCandidates bool, dtlsFingerprints []DTLSFingerprint, midValue string, iceParams ICEParameters, candidates []ICECandidate, dtlsRole sdp.ConnectionRole, iceGatheringState ICEGatheringState) error {
	media := (&sdp.MediaDescription{
		MediaName: sdp.MediaName{
			Media:   mediaSectionApplication,
			Port:    sdp.RangedPort{Value: 9},
			Protos:  []string{"UDP", "DTLS", "SCTP"},
			Formats: []string{"webrtc-datachannel"},
		},
		ConnectionInformation: &sdp.ConnectionInformation{
			NetworkType: "IN",
			AddressType: "IP4",
			Address: &sdp.Address{
				Address: "0.0.0.0",
			},
		},
	}).
		WithValueAttribute(sdp.AttrKeyConnectionSetup, dtlsRole.String()).
		WithValueAttribute(sdp.AttrKeyMID, midValue).
		WithPropertyAttribute(RTPTransceiverDirectionSendrecv.String()).
		WithPropertyAttribute("sctp-port:5000").
		WithICECredentials(iceParams.UsernameFragment, iceParams.Password)

	for _, f := range dtlsFingerprints {
		media = media.WithFingerprint(f.Algorithm, strings.ToUpper(f.Value))
	}

	if shouldAddCandidates {
		if err := addCandidatesToMediaDescriptions(candidates, media, iceGatheringState); err != nil {
			return err
		}
	}

	d.WithMedia(media)
	return nil
}

func populateLocalCandidates(sessionDescription *SessionDescription, i *ICEGatherer, iceGatheringState ICEGatheringState) *SessionDescription {
	if sessionDescription == nil || i == nil {
		return sessionDescription
	}

	candidates, err := i.GetLocalCandidates()
	if err != nil {
		return sessionDescription
	}

	parsed := sessionDescription.parsed
	if len(parsed.MediaDescriptions) > 0 {
		m := parsed.MediaDescriptions[0]
		if err = addCandidatesToMediaDescriptions(candidates, m, iceGatheringState); err != nil {
			return sessionDescription
		}
	}

	sdp, err := parsed.Marshal()
	if err != nil {
		return sessionDescription
	}

	return &SessionDescription{
		SDP:    string(sdp),
		Type:   sessionDescription.Type,
		parsed: parsed,
	}
}

func addSenderSDP(
	mediaSection mediaSection,
	isPlanB bool,
	media *sdp.MediaDescription,
) {
	for _, mt := range mediaSection.transceivers {
		sender := mt.Sender()
		if sender == nil {
			continue
		}

		track := sender.Track()
		if track == nil {
			continue
		}

		sendParameters := sender.GetParameters()
		for _, encoding := range sendParameters.Encodings {
			media = media.WithMediaSource(uint32(encoding.SSRC), track.StreamID() /* cname */, track.StreamID() /* streamLabel */, track.ID())
			if !isPlanB {
				media = media.WithPropertyAttribute("msid:" + track.StreamID() + " " + track.ID())
			}
		}

		if len(sendParameters.Encodings) > 1 {
			sendRids := make([]string, 0, len(sendParameters.Encodings))

			for _, encoding := range sendParameters.Encodings {
				media.WithValueAttribute(sdpAttributeRid, encoding.RID+" send")
				sendRids = append(sendRids, encoding.RID)
			}
			// Simulcast
			media.WithValueAttribute("simulcast", "send "+strings.Join(sendRids, ";"))
		}

		if !isPlanB {
			break
		}
	}
}

func addTransceiverSDP(
	d *sdp.SessionDescription,
	isPlanB bool,
	shouldAddCandidates bool,
	dtlsFingerprints []DTLSFingerprint,
	mediaEngine *MediaEngine,
	midValue string,
	iceParams ICEParameters,
	candidates []ICECandidate,
	dtlsRole sdp.ConnectionRole,
	iceGatheringState ICEGatheringState,
	mediaSection mediaSection,
) (bool, error) {
	transceivers := mediaSection.transceivers
	if len(transceivers) < 1 {
		return false, errSDPZeroTransceivers
	}
	// Use the first transceiver to generate the section attributes
	t := transceivers[0]
	media := sdp.NewJSEPMediaDescription(t.kind.String(), []string{}).
		WithValueAttribute(sdp.AttrKeyConnectionSetup, dtlsRole.String()).
		WithValueAttribute(sdp.AttrKeyMID, midValue).
		WithICECredentials(iceParams.UsernameFragment, iceParams.Password).
		WithPropertyAttribute(sdp.AttrKeyRTCPMux).
		WithPropertyAttribute(sdp.AttrKeyRTCPRsize)

	codecs := t.getCodecs()
	for _, codec := range codecs {
		name := strings.TrimPrefix(codec.MimeType, "audio/")
		name = strings.TrimPrefix(name, "video/")
		media.WithCodec(uint8(codec.PayloadType), name, codec.ClockRate, codec.Channels, codec.SDPFmtpLine)

		for _, feedback := range codec.RTPCodecCapability.RTCPFeedback {
			media.WithValueAttribute("rtcp-fb", fmt.Sprintf("%d %s %s", codec.PayloadType, feedback.Type, feedback.Parameter))
		}
	}
	if len(codecs) == 0 {
		// If we are sender and we have no codecs throw an error early
		if t.Sender() != nil {
			return false, ErrSenderWithNoCodecs
		}

		// Explicitly reject track if we don't have the codec
		// We need to include connection information even if we're rejecting a track, otherwise Firefox will fail to
		// parse the SDP with an error like:
		// SIPCC Failed to parse SDP: SDP Parse Error on line 50:  c= connection line not specified for every media level, validation failed.
		// In addition this makes our SDP compliant with RFC 4566 Section 5.7: https://datatracker.ietf.org/doc/html/rfc4566#section-5.7
		d.WithMedia(&sdp.MediaDescription{
			MediaName: sdp.MediaName{
				Media:   t.kind.String(),
				Port:    sdp.RangedPort{Value: 0},
				Protos:  []string{"UDP", "TLS", "RTP", "SAVPF"},
				Formats: []string{"0"},
			},
			ConnectionInformation: &sdp.ConnectionInformation{
				NetworkType: "IN",
				AddressType: "IP4",
				Address: &sdp.Address{
					Address: "0.0.0.0",
				},
			},
		})
		return false, nil
	}

	directions := []RTPTransceiverDirection{}
	if t.Sender() != nil {
		directions = append(directions, RTPTransceiverDirectionSendonly)
	}
	if t.Receiver() != nil {
		directions = append(directions, RTPTransceiverDirectionRecvonly)
	}

	parameters := mediaEngine.getRTPParametersByKind(t.kind, directions)
	for _, rtpExtension := range parameters.HeaderExtensions {
		extURL, err := url.Parse(rtpExtension.URI)
		if err != nil {
			return false, err
		}
		media.WithExtMap(sdp.ExtMap{Value: rtpExtension.ID, URI: extURL})
	}

	if len(mediaSection.ridMap) > 0 {
		recvRids := make([]string, 0, len(mediaSection.ridMap))

		for rid := range mediaSection.ridMap {
			media.WithValueAttribute(sdpAttributeRid, rid+" recv")
			recvRids = append(recvRids, rid)
		}
		// Simulcast
		media.WithValueAttribute("simulcast", "recv "+strings.Join(recvRids, ";"))
	}

	addSenderSDP(mediaSection, isPlanB, media)

	media = media.WithPropertyAttribute(t.Direction().String())

	for _, fingerprint := range dtlsFingerprints {
		media = media.WithFingerprint(fingerprint.Algorithm, strings.ToUpper(fingerprint.Value))
	}

	if shouldAddCandidates {
		if err := addCandidatesToMediaDescriptions(candidates, media, iceGatheringState); err != nil {
			return false, err
		}
	}

	d.WithMedia(media)

	return true, nil
}

type mediaSection struct {
	id           string
	transceivers []*RTPTransceiver
	data         bool
	ridMap       map[string]string
}

// populateSDP serializes a PeerConnections state into an SDP
func populateSDP(d *sdp.SessionDescription, isPlanB bool, dtlsFingerprints []DTLSFingerprint, mediaDescriptionFingerprint bool, isICELite bool, isExtmapAllowMixed bool, mediaEngine *MediaEngine, connectionRole sdp.ConnectionRole, candidates []ICECandidate, iceParams ICEParameters, mediaSections []mediaSection, iceGatheringState ICEGatheringState) (*sdp.SessionDescription, error) {
	var err error
	mediaDtlsFingerprints := []DTLSFingerprint{}

	if mediaDescriptionFingerprint {
		mediaDtlsFingerprints = dtlsFingerprints
	}

	bundleValue := "BUNDLE"
	bundleCount := 0
	appendBundle := func(midValue string) {
		bundleValue += " " + midValue
		bundleCount++
	}

	for i, m := range mediaSections {
		if m.data && len(m.transceivers) != 0 {
			return nil, errSDPMediaSectionMediaDataChanInvalid
		} else if !isPlanB && len(m.transceivers) > 1 {
			return nil, errSDPMediaSectionMultipleTrackInvalid
		}

		shouldAddID := true
		shouldAddCandidates := i == 0
		if m.data {
			if err = addDataMediaSection(d, shouldAddCandidates, mediaDtlsFingerprints, m.id, iceParams, candidates, connectionRole, iceGatheringState); err != nil {
				return nil, err
			}
		} else {
			shouldAddID, err = addTransceiverSDP(d, isPlanB, shouldAddCandidates, mediaDtlsFingerprints, mediaEngine, m.id, iceParams, candidates, connectionRole, iceGatheringState, m)
			if err != nil {
				return nil, err
			}
		}

		if shouldAddID {
			appendBundle(m.id)
		}
	}

	if !mediaDescriptionFingerprint {
		for _, fingerprint := range dtlsFingerprints {
			d.WithFingerprint(fingerprint.Algorithm, strings.ToUpper(fingerprint.Value))
		}
	}

	if isICELite {
		// RFC 5245 S15.3
		d = d.WithValueAttribute(sdp.AttrKeyICELite, "")
	}

	if isExtmapAllowMixed {
		d = d.WithPropertyAttribute(sdp.AttrKeyExtMapAllowMixed)
	}

	return d.WithValueAttribute(sdp.AttrKeyGroup, bundleValue), nil
}

func getMidValue(media *sdp.MediaDescription) string {
	for _, attr := range media.Attributes {
		if attr.Key == "mid" {
			return attr.Value
		}
	}
	return ""
}

// SessionDescription contains a MediaSection with Multiple SSRCs, it is Plan-B
func descriptionIsPlanB(desc *SessionDescription, log logging.LeveledLogger) bool {
	if desc == nil || desc.parsed == nil {
		return false
	}

	// Store all MIDs that already contain a track
	midWithTrack := map[string]bool{}

	for _, trackDetail := range trackDetailsFromSDP(log, desc.parsed) {
		if _, ok := midWithTrack[trackDetail.mid]; ok {
			return true
		}
		midWithTrack[trackDetail.mid] = true
	}

	return false
}

// SessionDescription contains a MediaSection with name `audio`, `video` or `data`
// If only one SSRC is set we can't know if it is Plan-B or Unified. If users have
// set fallback mode assume it is Plan-B
func descriptionPossiblyPlanB(desc *SessionDescription) bool {
	if desc == nil || desc.parsed == nil {
		return false
	}

	detectionRegex := regexp.MustCompile(`(?i)^(audio|video|data)$`)
	for _, media := range desc.parsed.MediaDescriptions {
		if len(detectionRegex.FindStringSubmatch(getMidValue(media))) == 2 {
			return true
		}
	}
	return false
}

func getPeerDirection(media *sdp.MediaDescription) RTPTransceiverDirection {
	for _, a := range media.Attributes {
		if direction := NewRTPTransceiverDirection(a.Key); direction != RTPTransceiverDirection(Unknown) {
			return direction
		}
	}
	return RTPTransceiverDirection(Unknown)
}

func extractFingerprint(desc *sdp.SessionDescription) (string, string, error) {
	fingerprints := []string{}

	if fingerprint, haveFingerprint := desc.Attribute("fingerprint"); haveFingerprint {
		fingerprints = append(fingerprints, fingerprint)
	}

	for _, m := range desc.MediaDescriptions {
		if fingerprint, haveFingerprint := m.Attribute("fingerprint"); haveFingerprint {
			fingerprints = append(fingerprints, fingerprint)
		}
	}

	if len(fingerprints) < 1 {
		return "", "", ErrSessionDescriptionNoFingerprint
	}

	for _, m := range fingerprints {
		if m != fingerprints[0] {
			return "", "", ErrSessionDescriptionConflictingFingerprints
		}
	}

	parts := strings.Split(fingerprints[0], " ")
	if len(parts) != 2 {
		return "", "", ErrSessionDescriptionInvalidFingerprint
	}
	return parts[1], parts[0], nil
}

func extractICEDetails(desc *sdp.SessionDescription, log logging.LeveledLogger) (string, string, []ICECandidate, error) { // nolint:gocognit
	candidates := []ICECandidate{}
	remotePwds := []string{}
	remoteUfrags := []string{}

	if ufrag, haveUfrag := desc.Attribute("ice-ufrag"); haveUfrag {
		remoteUfrags = append(remoteUfrags, ufrag)
	}
	if pwd, havePwd := desc.Attribute("ice-pwd"); havePwd {
		remotePwds = append(remotePwds, pwd)
	}

	for _, m := range desc.MediaDescriptions {
		if ufrag, haveUfrag := m.Attribute("ice-ufrag"); haveUfrag {
			remoteUfrags = append(remoteUfrags, ufrag)
		}
		if pwd, havePwd := m.Attribute("ice-pwd"); havePwd {
			remotePwds = append(remotePwds, pwd)
		}

		for _, a := range m.Attributes {
			if a.IsICECandidate() {
				c, err := ice.UnmarshalCandidate(a.Value)
				if err != nil {
					if errors.Is(err, ice.ErrUnknownCandidateTyp) || errors.Is(err, ice.ErrDetermineNetworkType) {
						log.Warnf("Discarding remote candidate: %s", err)
						continue
					}
					return "", "", nil, err
				}

				candidate, err := newICECandidateFromICE(c)
				if err != nil {
					return "", "", nil, err
				}

				candidates = append(candidates, candidate)
			}
		}
	}

	if len(remoteUfrags) == 0 {
		return "", "", nil, ErrSessionDescriptionMissingIceUfrag
	} else if len(remotePwds) == 0 {
		return "", "", nil, ErrSessionDescriptionMissingIcePwd
	}

	for _, m := range remoteUfrags {
		if m != remoteUfrags[0] {
			return "", "", nil, ErrSessionDescriptionConflictingIceUfrag
		}
	}

	for _, m := range remotePwds {
		if m != remotePwds[0] {
			return "", "", nil, ErrSessionDescriptionConflictingIcePwd
		}
	}

	return remoteUfrags[0], remotePwds[0], candidates, nil
}

func haveApplicationMediaSection(desc *sdp.SessionDescription) bool {
	for _, m := range desc.MediaDescriptions {
		if m.MediaName.Media == mediaSectionApplication {
			return true
		}
	}

	return false
}

func getByMid(searchMid string, desc *SessionDescription) *sdp.MediaDescription {
	for _, m := range desc.parsed.MediaDescriptions {
		if mid, ok := m.Attribute(sdp.AttrKeyMID); ok && mid == searchMid {
			return m
		}
	}
	return nil
}

// haveDataChannel return MediaDescription with MediaName equal application
func haveDataChannel(desc *SessionDescription) *sdp.MediaDescription {
	for _, d := range desc.parsed.MediaDescriptions {
		if d.MediaName.Media == mediaSectionApplication {
			return d
		}
	}
	return nil
}

func codecsFromMediaDescription(m *sdp.MediaDescription) (out []RTPCodecParameters, err error) {
	s := &sdp.SessionDescription{
		MediaDescriptions: []*sdp.MediaDescription{m},
	}

	for _, payloadStr := range m.MediaName.Formats {
		payloadType, err := strconv.ParseUint(payloadStr, 10, 8)
		if err != nil {
			return nil, err
		}

		codec, err := s.GetCodecForPayloadType(uint8(payloadType))
		if err != nil {
			if payloadType == 0 {
				continue
			}
			return nil, err
		}

		channels := uint16(0)
		val, err := strconv.ParseUint(codec.EncodingParameters, 10, 16)
		if err == nil {
			channels = uint16(val)
		}

		feedback := []RTCPFeedback{}
		for _, raw := range codec.RTCPFeedback {
			split := strings.Split(raw, " ")
			entry := RTCPFeedback{Type: split[0]}
			if len(split) == 2 {
				entry.Parameter = split[1]
			}

			feedback = append(feedback, entry)
		}

		out = append(out, RTPCodecParameters{
			RTPCodecCapability: RTPCodecCapability{m.MediaName.Media + "/" + codec.Name, codec.ClockRate, channels, codec.Fmtp, feedback},
			PayloadType:        PayloadType(payloadType),
		})
	}

	return out, nil
}

func rtpExtensionsFromMediaDescription(m *sdp.MediaDescription) (map[string]int, error) {
	out := map[string]int{}

	for _, a := range m.Attributes {
		if a.Key == sdp.AttrKeyExtMap {
			e := sdp.ExtMap{}
			if err := e.Unmarshal(a.String()); err != nil {
				return nil, err
			}

			out[e.URI.String()] = e.Value
		}
	}

	return out, nil
}

// updateSDPOrigin saves sdp.Origin in PeerConnection when creating 1st local SDP;
// for subsequent calling, it updates Origin for SessionDescription from saved one
// and increments session version by one.
// https://tools.ietf.org/html/draft-ietf-rtcweb-jsep-25#section-5.2.2
func updateSDPOrigin(origin *sdp.Origin, d *sdp.SessionDescription) {
	if atomic.CompareAndSwapUint64(&origin.SessionVersion, 0, d.Origin.SessionVersion) { // store
		atomic.StoreUint64(&origin.SessionID, d.Origin.SessionID)
	} else { // load
		for { // awaiting for saving session id
			d.Origin.SessionID = atomic.LoadUint64(&origin.SessionID)
			if d.Origin.SessionID != 0 {
				break
			}
		}
		d.Origin.SessionVersion = atomic.AddUint64(&origin.SessionVersion, 1)
	}
}

func isIceLiteSet(desc *sdp.SessionDescription) bool {
	for _, a := range desc.Attributes {
		if strings.TrimSpace(a.Key) == sdp.AttrKeyICELite {
			return true
		}
	}

	return false
}

func isExtMapAllowMixedSet(desc *sdp.SessionDescription) bool {
	for _, a := range desc.Attributes {
		if strings.TrimSpace(a.Key) == sdp.AttrKeyExtMapAllowMixed {
			return true
		}
	}

	return false
}
