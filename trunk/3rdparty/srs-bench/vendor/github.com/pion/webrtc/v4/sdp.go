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

	"github.com/pion/ice/v4"
	"github.com/pion/logging"
	"github.com/pion/sdp/v3"
)

// trackDetails represents any media source that can be represented in a SDP
// This isn't keyed by SSRC because it also needs to support rid based sources.
type trackDetails struct {
	mid      string
	kind     RTPCodecType
	streamID string
	id       string
	ssrcs    []SSRC
	rtxSsrc  *SSRC
	fecSsrc  *SSRC
	rids     []string
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
//
//nolint:gocognit,gocyclo,cyclop
func trackDetailsFromSDP(
	log logging.LeveledLogger,
	s *sdp.SessionDescription,
) (incomingTracks []trackDetails) {
	for _, media := range s.MediaDescriptions {
		tracksInMediaSection := []trackDetails{}
		rtxRepairFlows := map[uint64]uint64{}
		fecRepairFlows := map[uint64]uint64{}

		// Plan B can have multiple tracks in a single media section
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
				if split[0] == sdp.SemanticTokenFlowIdentification { //nolint:nestif
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
						tracksInMediaSection = filterTrackWithSSRC(
							tracksInMediaSection,
							SSRC(rtxRepairFlow),
						) // Remove if rtx was added as track before
						for i := range tracksInMediaSection {
							if tracksInMediaSection[i].ssrcs[0] == SSRC(baseSsrc) {
								repairSsrc := SSRC(rtxRepairFlow)
								tracksInMediaSection[i].rtxSsrc = &repairSsrc
							}
						}
					}
				} else if split[0] == sdp.SemanticTokenForwardErrorCorrectionFramework {
					// Similar to above, lines like `a=ssrc-group:FEC-FR aaaaa bbbbb`
					// means for video ssrc aaaaa, there's a FEC track bbbbb
					if len(split) == 3 {
						baseSsrc, err := strconv.ParseUint(split[1], 10, 32)
						if err != nil {
							log.Warnf("Failed to parse SSRC: %v", err)

							continue
						}
						fecRepairFlow, err := strconv.ParseUint(split[2], 10, 32)
						if err != nil {
							log.Warnf("Failed to parse SSRC: %v", err)

							continue
						}
						fecRepairFlows[fecRepairFlow] = baseSsrc
						tracksInMediaSection = filterTrackWithSSRC(
							tracksInMediaSection,
							SSRC(fecRepairFlow),
						) // Remove if fec was added as track before
						for i := range tracksInMediaSection {
							if tracksInMediaSection[i].ssrcs[0] == SSRC(baseSsrc) {
								repairSsrc := SSRC(fecRepairFlow)
								tracksInMediaSection[i].fecSsrc = &repairSsrc
							}
						}
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
				if _, ok := fecRepairFlows[ssrc]; ok {
					continue // This ssrc is a FEC repair flow, ignore
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
						repairSsrc := SSRC(r) //nolint:gosec // G115
						trackDetails.rtxSsrc = &repairSsrc
					}
				}
				for r, baseSsrc := range fecRepairFlows {
					if baseSsrc == ssrc {
						fecSsrc := SSRC(r) //nolint:gosec // G115
						trackDetails.fecSsrc = &fecSsrc
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
			for _, rid := range rids {
				simulcastTrack.rids = append(simulcastTrack.rids, rid.id)
			}

			tracksInMediaSection = []trackDetails{simulcastTrack}
		}

		incomingTracks = append(incomingTracks, tracksInMediaSection...)
	}

	return incomingTracks
}

func trackDetailsToRTPReceiveParameters(trackDetails *trackDetails) RTPReceiveParameters {
	encodingSize := len(trackDetails.ssrcs)
	if len(trackDetails.rids) >= encodingSize {
		encodingSize = len(trackDetails.rids)
	}

	encodings := make([]RTPDecodingParameters, encodingSize)
	for i := range encodings {
		if len(trackDetails.rids) > i {
			encodings[i].RID = trackDetails.rids[i]
		}
		if len(trackDetails.ssrcs) > i {
			encodings[i].SSRC = trackDetails.ssrcs[i]
		}

		if trackDetails.rtxSsrc != nil {
			encodings[i].RTX.SSRC = *trackDetails.rtxSsrc
		}

		if trackDetails.fecSsrc != nil {
			encodings[i].FEC.SSRC = *trackDetails.fecSsrc
		}
	}

	return RTPReceiveParameters{Encodings: encodings}
}

func getRids(media *sdp.MediaDescription) []*simulcastRid {
	rids := []*simulcastRid{}
	var simulcastAttr string
	for _, attr := range media.Attributes {
		if attr.Key == sdpAttributeRid {
			split := strings.Split(attr.Value, " ")
			rids = append(rids, &simulcastRid{id: split[0], attrValue: attr.Value})
		} else if attr.Key == sdpAttributeSimulcast {
			simulcastAttr = attr.Value
		}
	}
	// process paused stream like "a=simulcast:send 1;~2;~3"
	if simulcastAttr != "" {
		if space := strings.Index(simulcastAttr, " "); space > 0 {
			simulcastAttr = simulcastAttr[space+1:]
		}
		ridStates := strings.Split(simulcastAttr, ";")
		for _, ridState := range ridStates {
			if ridState[:1] == "~" {
				ridID := ridState[1:]
				for _, rid := range rids {
					if rid.id == ridID {
						rid.paused = true

						break
					}
				}
			}
		}
	}

	return rids
}

func addCandidatesToMediaDescriptions(
	candidates []ICECandidate,
	mediaDescr *sdp.MediaDescription,
	iceGatheringState ICEGatheringState,
) error {
	appendCandidateIfNew := func(c ice.Candidate, attributes []sdp.Attribute) {
		marshaled := c.Marshal()
		for _, a := range attributes {
			if marshaled == a.Value {
				return
			}
		}

		mediaDescr.WithValueAttribute("candidate", marshaled)
	}

	for _, c := range candidates {
		candidate, err := c.ToICE()
		if err != nil {
			return err
		}

		candidate.SetComponent(1)
		appendCandidateIfNew(candidate, mediaDescr.Attributes)

		candidate.SetComponent(2)
		appendCandidateIfNew(candidate, mediaDescr.Attributes)
	}

	if iceGatheringState != ICEGatheringStateComplete {
		return nil
	}
	for _, a := range mediaDescr.Attributes {
		if a.Key == "end-of-candidates" {
			return nil
		}
	}

	mediaDescr.WithPropertyAttribute("end-of-candidates")

	return nil
}

func addDataMediaSection(
	descr *sdp.SessionDescription,
	shouldAddCandidates bool,
	dtlsFingerprints []DTLSFingerprint,
	midValue string,
	iceParams ICEParameters,
	candidates []ICECandidate,
	dtlsRole sdp.ConnectionRole,
	iceGatheringState ICEGatheringState,
	sctpMaxMessageSize uint32,
) error {
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
		WithValueAttribute("max-message-size", fmt.Sprintf("%d", sctpMaxMessageSize)).
		WithICECredentials(iceParams.UsernameFragment, iceParams.Password)

	for _, f := range dtlsFingerprints {
		media = media.WithFingerprint(f.Algorithm, strings.ToUpper(f.Value))
	}

	if shouldAddCandidates {
		if err := addCandidatesToMediaDescriptions(candidates, media, iceGatheringState); err != nil {
			return err
		}
	}

	descr.WithMedia(media)

	return nil
}

func populateLocalCandidates(
	sessionDescription *SessionDescription,
	i *ICEGatherer,
	iceGatheringState ICEGatheringState,
) *SessionDescription {
	if sessionDescription == nil || i == nil {
		return sessionDescription
	}

	candidates, err := i.GetLocalCandidates()
	if err != nil {
		return sessionDescription
	}

	parsed := sessionDescription.parsed
	if len(parsed.MediaDescriptions) > 0 {
		mediaDescr := parsed.MediaDescriptions[0]
		if err = addCandidatesToMediaDescriptions(candidates, mediaDescr, iceGatheringState); err != nil {
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

//nolint:gocognit,cyclop
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
			if encoding.RTX.SSRC != 0 {
				media = media.WithValueAttribute(
					"ssrc-group",
					fmt.Sprintf(
						"%s %d %d",
						sdp.SemanticTokenFlowIdentification,
						encoding.SSRC,
						encoding.RTX.SSRC,
					),
				)
			}
			if encoding.FEC.SSRC != 0 {
				media = media.WithValueAttribute(
					"ssrc-group",
					fmt.Sprintf(
						"%s %d %d",
						sdp.SemanticTokenForwardErrorCorrectionFramework,
						encoding.SSRC,
						encoding.FEC.SSRC,
					),
				)
			}

			media = media.WithMediaSource(
				uint32(encoding.SSRC),
				track.StreamID(), /* cname */
				track.StreamID(), /* streamLabel */
				track.ID(),
			)

			if !isPlanB {
				if encoding.RTX.SSRC != 0 {
					media = media.WithMediaSource(
						uint32(encoding.RTX.SSRC),
						track.StreamID(), /* cname */
						track.StreamID(), /* streamLabel */
						track.ID(),
					)
				}
				if encoding.FEC.SSRC != 0 {
					media = media.WithMediaSource(
						uint32(encoding.FEC.SSRC),
						track.StreamID(), /* cname */
						track.StreamID(), /* streamLabel */
						track.ID(),
					)
				}

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
			media.WithValueAttribute(sdpAttributeSimulcast, "send "+strings.Join(sendRids, ";"))
		}

		if !isPlanB {
			break
		}
	}
}

//nolint:cyclop
func addTransceiverSDP(
	descr *sdp.SessionDescription,
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
	transceiver := transceivers[0]
	media := sdp.NewJSEPMediaDescription(transceiver.kind.String(), []string{}).
		WithValueAttribute(sdp.AttrKeyConnectionSetup, dtlsRole.String()).
		WithValueAttribute(sdp.AttrKeyMID, midValue).
		WithICECredentials(iceParams.UsernameFragment, iceParams.Password).
		WithPropertyAttribute(sdp.AttrKeyRTCPMux).
		WithPropertyAttribute(sdp.AttrKeyRTCPRsize)

	codecs := transceiver.getCodecs()
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
		if transceiver.Sender() != nil {
			return false, ErrSenderWithNoCodecs
		}

		// Explicitly reject track if we don't have the codec
		// We need to include connection information even if we're rejecting a track, otherwise Firefox will fail to
		// parse the SDP with an error like:
		// SIPCC Failed to parse SDP: SDP Parse Error on line 50:  c= connection line not specified for every media level,
		// validation failed.
		// In addition this makes our SDP compliant with RFC 4566 Section 5.7:
		// https://datatracker.ietf.org/doc/html/rfc4566#section-5.7
		descr.WithMedia(&sdp.MediaDescription{
			MediaName: sdp.MediaName{
				Media:   transceiver.kind.String(),
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
	if transceiver.Sender() != nil {
		directions = append(directions, RTPTransceiverDirectionSendonly)
	}
	if transceiver.Receiver() != nil {
		directions = append(directions, RTPTransceiverDirectionRecvonly)
	}

	parameters := mediaEngine.getRTPParametersByKind(transceiver.kind, directions)
	for _, rtpExtension := range parameters.HeaderExtensions {
		if mediaSection.matchExtensions != nil {
			if _, enabled := mediaSection.matchExtensions[rtpExtension.URI]; !enabled {
				continue
			}
		}
		extURL, err := url.Parse(rtpExtension.URI)
		if err != nil {
			return false, err
		}
		media.WithExtMap(sdp.ExtMap{Value: rtpExtension.ID, URI: extURL})
	}

	if len(mediaSection.rids) > 0 {
		recvRids := make([]string, 0, len(mediaSection.rids))

		for _, rid := range mediaSection.rids {
			ridID := rid.id
			media.WithValueAttribute(sdpAttributeRid, ridID+" recv")
			if rid.paused {
				ridID = "~" + ridID
			}
			recvRids = append(recvRids, ridID)
		}
		// Simulcast
		media.WithValueAttribute(sdpAttributeSimulcast, "recv "+strings.Join(recvRids, ";"))
	}

	addSenderSDP(mediaSection, isPlanB, media)

	media = media.WithPropertyAttribute(transceiver.Direction().String())

	for _, fingerprint := range dtlsFingerprints {
		media = media.WithFingerprint(fingerprint.Algorithm, strings.ToUpper(fingerprint.Value))
	}

	if shouldAddCandidates {
		if err := addCandidatesToMediaDescriptions(candidates, media, iceGatheringState); err != nil {
			return false, err
		}
	}

	descr.WithMedia(media)

	return true, nil
}

type simulcastRid struct {
	id        string
	attrValue string
	paused    bool
}

type mediaSection struct {
	id              string
	transceivers    []*RTPTransceiver
	data            bool
	matchExtensions map[string]int
	rids            []*simulcastRid
}

func bundleMatchFromRemote(matchBundleGroup *string) func(mid string) bool {
	if matchBundleGroup == nil {
		return func(string) bool {
			return true
		}
	}
	bundleTags := strings.Split(*matchBundleGroup, " ")

	return func(midValue string) bool {
		for _, tag := range bundleTags {
			if tag == midValue {
				return true
			}
		}

		return false
	}
}

// populateSDP serializes a PeerConnections state into an SDP.
//
//nolint:cyclop
func populateSDP(
	descr *sdp.SessionDescription,
	isPlanB bool,
	dtlsFingerprints []DTLSFingerprint,
	mediaDescriptionFingerprint bool,
	isICELite bool,
	isExtmapAllowMixed bool,
	mediaEngine *MediaEngine,
	connectionRole sdp.ConnectionRole,
	candidates []ICECandidate,
	iceParams ICEParameters,
	mediaSections []mediaSection,
	iceGatheringState ICEGatheringState,
	matchBundleGroup *string,
	sctpMaxMessageSize uint32,
) (*sdp.SessionDescription, error) {
	var err error
	mediaDtlsFingerprints := []DTLSFingerprint{}

	if mediaDescriptionFingerprint {
		mediaDtlsFingerprints = dtlsFingerprints
	}

	bundleValue := "BUNDLE"
	bundleCount := 0

	bundleMatch := bundleMatchFromRemote(matchBundleGroup)
	appendBundle := func(midValue string) {
		bundleValue += " " + midValue
		bundleCount++
	}

	for i, section := range mediaSections {
		if section.data && len(section.transceivers) != 0 {
			return nil, errSDPMediaSectionMediaDataChanInvalid
		} else if !isPlanB && len(section.transceivers) > 1 {
			return nil, errSDPMediaSectionMultipleTrackInvalid
		}

		shouldAddID := true
		shouldAddCandidates := i == 0
		if section.data {
			if err = addDataMediaSection(
				descr,
				shouldAddCandidates,
				mediaDtlsFingerprints,
				section.id,
				iceParams,
				candidates,
				connectionRole,
				iceGatheringState,
				sctpMaxMessageSize,
			); err != nil {
				return nil, err
			}
		} else {
			shouldAddID, err = addTransceiverSDP(
				descr,
				isPlanB,
				shouldAddCandidates,
				mediaDtlsFingerprints,
				mediaEngine,
				section.id,
				iceParams,
				candidates,
				connectionRole,
				iceGatheringState,
				section,
			)
			if err != nil {
				return nil, err
			}
		}

		if shouldAddID {
			if bundleMatch(section.id) {
				appendBundle(section.id)
			} else {
				descr.MediaDescriptions[len(descr.MediaDescriptions)-1].MediaName.Port = sdp.RangedPort{Value: 0}
			}
		}
	}

	if !mediaDescriptionFingerprint {
		for _, fingerprint := range dtlsFingerprints {
			descr.WithFingerprint(fingerprint.Algorithm, strings.ToUpper(fingerprint.Value))
		}
	}

	if isICELite {
		// RFC 5245 S15.3
		descr = descr.WithValueAttribute(sdp.AttrKeyICELite, "")
	}

	if isExtmapAllowMixed {
		descr = descr.WithPropertyAttribute(sdp.AttrKeyExtMapAllowMixed)
	}

	if bundleCount > 0 {
		descr = descr.WithValueAttribute(sdp.AttrKeyGroup, bundleValue)
	}

	return descr, nil
}

func getMidValue(media *sdp.MediaDescription) string {
	for _, attr := range media.Attributes {
		if attr.Key == "mid" {
			return attr.Value
		}
	}

	return ""
}

// SessionDescription contains a MediaSection with Multiple SSRCs, it is Plan-B.
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
// set fallback mode assume it is Plan-B.
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
		if direction := NewRTPTransceiverDirection(a.Key); direction != RTPTransceiverDirectionUnknown {
			return direction
		}
	}

	return RTPTransceiverDirectionUnknown
}

func extractBundleID(desc *sdp.SessionDescription) string {
	groupAttribute, _ := desc.Attribute(sdp.AttrKeyGroup)

	isBundled := strings.Contains(groupAttribute, "BUNDLE")

	if !isBundled {
		return ""
	}

	bundleIDs := strings.Split(groupAttribute, " ")

	if len(bundleIDs) < 2 {
		return ""
	}

	return bundleIDs[1]
}

func extractFingerprint(desc *sdp.SessionDescription) (string, string, error) { //nolint:gocognit,cyclop
	fingerprint := ""

	// Fingerprint on session level has highest priority
	if sessionFingerprint, haveFingerprint := desc.Attribute("fingerprint"); haveFingerprint {
		fingerprint = sessionFingerprint
	}

	if fingerprint == "" { //nolint:nestif
		bundleID := extractBundleID(desc)
		if bundleID != "" {
			// Locate the fingerprint of the bundled media section
			for _, mediaDescr := range desc.MediaDescriptions {
				if mid, haveMid := mediaDescr.Attribute("mid"); haveMid {
					if mid == bundleID && fingerprint == "" {
						if mediaFingerprint, haveFingerprint := mediaDescr.Attribute("fingerprint"); haveFingerprint {
							fingerprint = mediaFingerprint
						}
					}
				}
			}
		} else {
			// Take the fingerprint from the first media section which has one.
			// Note: According to Bundle spec each media section would have it's own transport
			//       with it's own cert and fingerprint each, so we would need to return a list.
			for _, mediaDescr := range desc.MediaDescriptions {
				mediaFingerprint, haveFingerprint := mediaDescr.Attribute("fingerprint")
				if haveFingerprint && fingerprint == "" {
					fingerprint = mediaFingerprint
				}
			}
		}
	}

	if fingerprint == "" {
		return "", "", ErrSessionDescriptionNoFingerprint
	}

	parts := strings.Split(fingerprint, " ")
	if len(parts) != 2 {
		return "", "", ErrSessionDescriptionInvalidFingerprint
	}

	return parts[1], parts[0], nil
}

// identifiedMediaDescription contains a MediaDescription with sdpMid and sdpMLineIndex.
type identifiedMediaDescription struct {
	MediaDescription *sdp.MediaDescription
	SDPMid           string
	SDPMLineIndex    uint16
}

func extractICEDetailsFromMedia(
	media *identifiedMediaDescription,
	log logging.LeveledLogger,
) (string, string, []ICECandidate, error) {
	remoteUfrag := ""
	remotePwd := ""
	candidates := []ICECandidate{}
	descr := media.MediaDescription

	if ufrag, haveUfrag := descr.Attribute("ice-ufrag"); haveUfrag {
		remoteUfrag = ufrag
	}
	if pwd, havePwd := descr.Attribute("ice-pwd"); havePwd {
		remotePwd = pwd
	}
	for _, a := range descr.Attributes {
		if a.IsICECandidate() {
			c, err := ice.UnmarshalCandidate(a.Value)
			if err != nil {
				if errors.Is(err, ice.ErrUnknownCandidateTyp) || errors.Is(err, ice.ErrDetermineNetworkType) {
					log.Warnf("Discarding remote candidate: %s", err)

					continue
				}

				return "", "", nil, err
			}

			candidate, err := newICECandidateFromICE(c, media.SDPMid, media.SDPMLineIndex)
			if err != nil {
				return "", "", nil, err
			}

			candidates = append(candidates, candidate)
		}
	}

	return remoteUfrag, remotePwd, candidates, nil
}

type sdpICEDetails struct {
	Ufrag      string
	Password   string
	Candidates []ICECandidate
}

func extractICEDetails(
	desc *sdp.SessionDescription,
	log logging.LeveledLogger,
) (*sdpICEDetails, error) { // nolint:gocognit
	details := &sdpICEDetails{
		Candidates: []ICECandidate{},
	}

	// Ufrag and Pw are allow at session level and thus have highest prio
	if ufrag, haveUfrag := desc.Attribute("ice-ufrag"); haveUfrag {
		details.Ufrag = ufrag
	}
	if pwd, havePwd := desc.Attribute("ice-pwd"); havePwd {
		details.Password = pwd
	}

	mediaDescr, ok := selectCandidateMediaSection(desc)
	if ok {
		ufrag, pwd, candidates, err := extractICEDetailsFromMedia(mediaDescr, log)
		if err != nil {
			return nil, err
		}

		if details.Ufrag == "" && ufrag != "" {
			details.Ufrag = ufrag
			details.Password = pwd
		}

		details.Candidates = candidates
	}

	if details.Ufrag == "" {
		return nil, ErrSessionDescriptionMissingIceUfrag
	} else if details.Password == "" {
		return nil, ErrSessionDescriptionMissingIcePwd
	}

	return details, nil
}

// Select the first media section or the first bundle section
// Currently Pion uses the first media section to gather candidates.
// https://github.com/pion/webrtc/pull/2950
func selectCandidateMediaSection(sessionDescription *sdp.SessionDescription) (
	descr *identifiedMediaDescription,
	ok bool,
) {
	bundleID := extractBundleID(sessionDescription)

	for mLineIndex, mediaDescr := range sessionDescription.MediaDescriptions {
		mid := getMidValue(mediaDescr)
		// If bundled, only take ICE detail from bundle master section
		if bundleID != "" {
			if mid == bundleID {
				return &identifiedMediaDescription{
					MediaDescription: mediaDescr,
					SDPMid:           mid,
					SDPMLineIndex:    uint16(mLineIndex), //nolint:gosec // G115
				}, true
			}
		} else {
			// For not-bundled, take ICE details from the first media section
			return &identifiedMediaDescription{
				MediaDescription: mediaDescr,
				SDPMid:           mid,
				SDPMLineIndex:    uint16(mLineIndex), //nolint:gosec // G115
			}, true
		}
	}

	return nil, false
}

func getByMid(searchMid string, desc *SessionDescription) *sdp.MediaDescription {
	for _, m := range desc.parsed.MediaDescriptions {
		if mid, ok := m.Attribute(sdp.AttrKeyMID); ok && mid == searchMid {
			return m
		}
	}

	return nil
}

// haveDataChannel return MediaDescription with MediaName equal application.
func haveDataChannel(desc *SessionDescription) *sdp.MediaDescription {
	for _, d := range desc.parsed.MediaDescriptions {
		if d.MediaName.Media == mediaSectionApplication {
			return d
		}
	}

	return nil
}

func codecsFromMediaDescription(mediaDescr *sdp.MediaDescription) (out []RTPCodecParameters, err error) {
	s := &sdp.SessionDescription{
		MediaDescriptions: []*sdp.MediaDescription{mediaDescr},
	}

	for _, payloadStr := range mediaDescr.MediaName.Formats {
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
			RTPCodecCapability: RTPCodecCapability{
				mediaDescr.MediaName.Media + "/" + codec.Name,
				codec.ClockRate,
				channels,
				codec.Fmtp,
				feedback,
			},
			PayloadType: PayloadType(payloadType),
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
func updateSDPOrigin(origin *sdp.Origin, descr *sdp.SessionDescription) {
	if atomic.CompareAndSwapUint64(&origin.SessionVersion, 0, descr.Origin.SessionVersion) { // store
		atomic.StoreUint64(&origin.SessionID, descr.Origin.SessionID)
	} else { // load
		for { // awaiting for saving session id
			descr.Origin.SessionID = atomic.LoadUint64(&origin.SessionID)
			if descr.Origin.SessionID != 0 {
				break
			}
		}
		descr.Origin.SessionVersion = atomic.AddUint64(&origin.SessionVersion, 1)
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

func getMaxMessageSize(desc *sdp.MediaDescription) uint32 {
	for _, a := range desc.Attributes {
		if strings.TrimSpace(a.Key) == "max-message-size" {
			if v, err := strconv.ParseUint(a.Value, 10, 32); err == nil {
				return uint32(v)
			}
		}
	}

	return 0
}
