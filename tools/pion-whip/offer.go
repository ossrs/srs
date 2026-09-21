// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"fmt"
	"strings"

	"github.com/pion/webrtc/v4"
)

// newMediaEngine makes the media engine of the publisher. With rtx it registers pion's default codecs, an rtx
// payload with apt for every video codec included, so the offer carries rtx and a FID group like a browser's and
// SRS may answer RTX. Without it, the engine has those same codecs minus the rtx entries, so pion allocates no RTX
// SSRC and the offer carries no rtx, no apt and no FID group, the offer of a peer without RTX: SRS can answer only
// plain retransmission whatever its nack_prefer_rtx says.
func newMediaEngine(rtx bool) (*webrtc.MediaEngine, error) {
	m := &webrtc.MediaEngine{}
	if rtx {
		if err := m.RegisterDefaultCodecs(); err != nil {
			return nil, fmt.Errorf("register codecs: %w", err)
		}
		return m, nil
	}

	for _, kind := range []webrtc.RTPCodecType{webrtc.RTPCodecTypeVideo, webrtc.RTPCodecTypeAudio} {
		codecs, err := defaultCodecs(kind)
		if err != nil {
			return nil, err
		}
		for _, codec := range codecs {
			if strings.EqualFold(codec.MimeType, webrtc.MimeTypeRTX) {
				continue
			}
			if err := m.RegisterCodec(codec, kind); err != nil {
				return nil, fmt.Errorf("register %s without rtx: %w", codec.MimeType, err)
			}
		}
	}
	return m, nil
}

// defaultCodecs returns pion's default codecs of one kind for sending. The media engine does not list what it
// registered, but a sender's parameters do, so they are read from a scratch peer connection that never connects.
func defaultCodecs(kind webrtc.RTPCodecType) ([]webrtc.RTPCodecParameters, error) {
	scratch := &webrtc.MediaEngine{}
	if err := scratch.RegisterDefaultCodecs(); err != nil {
		return nil, fmt.Errorf("register codecs: %w", err)
	}
	pc, err := webrtc.NewAPI(webrtc.WithMediaEngine(scratch)).NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return nil, fmt.Errorf("list %s codecs: %w", kind, err)
	}
	defer pc.Close()
	transceiver, err := pc.AddTransceiverFromKind(kind, webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionSendonly})
	if err != nil {
		return nil, fmt.Errorf("list %s codecs: %w", kind, err)
	}
	return transceiver.Sender().GetParameters().Codecs, nil
}

// newTracks makes the two tracks the publisher sends: H.264 constrained baseline with packetization mode 1, the
// profile every WebRTC stack accepts and pion's default payload type 106, and Opus 48 kHz stereo, payload type 111.
func newTracks() (video, audio *webrtc.TrackLocalStaticSample, err error) {
	video, err = webrtc.NewTrackLocalStaticSample(webrtc.RTPCodecCapability{
		MimeType: webrtc.MimeTypeH264, ClockRate: 90000,
		SDPFmtpLine: "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f",
	}, "video", "pion-whip")
	if err != nil {
		return nil, nil, fmt.Errorf("video track: %w", err)
	}
	audio, err = webrtc.NewTrackLocalStaticSample(webrtc.RTPCodecCapability{
		MimeType: webrtc.MimeTypeOpus, ClockRate: 48000, Channels: 2, SDPFmtpLine: "minptime=10;useinbandfec=1",
	}, "audio", "pion-whip")
	if err != nil {
		return nil, nil, fmt.Errorf("audio track: %w", err)
	}
	return video, audio, nil
}

// addSendonlyTracks adds each track as a sendonly transceiver, as a WHIP publisher offers, and returns the senders
// whose RTCP the caller must drain for the interceptors to see the NACKs.
func addSendonlyTracks(pc *webrtc.PeerConnection, tracks ...webrtc.TrackLocal) ([]*webrtc.RTPSender, error) {
	var senders []*webrtc.RTPSender
	for _, track := range tracks {
		transceiver, err := pc.AddTransceiverFromTrack(track, webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionSendonly})
		if err != nil {
			return nil, fmt.Errorf("add %s track: %w", track.Kind(), err)
		}
		senders = append(senders, transceiver.Sender())
	}
	return senders, nil
}

// rtxOfAnswer returns the rtx payload type and its apt from the video section of a publish answer, or ok false when
// the answer kept no rtx payload and the session uses plain retransmission. A publish answer carries no FID group:
// the publisher named the SSRCs in its offer, so the rtpmap and apt lines alone say which format SRS chose.
func rtxOfAnswer(sdp string) (rtxPayloadType, apt uint8, ok bool) {
	start := strings.Index(sdp, "m=video ")
	if start < 0 {
		return 0, 0, false
	}
	video := sdp[start:]
	if next := strings.Index(video[1:], "\nm="); next >= 0 {
		video = video[:next+1]
	}
	for _, line := range strings.Split(video, "\n") {
		var pt, associated int
		if _, err := fmt.Sscanf(strings.TrimSpace(line), "a=fmtp:%d apt=%d", &pt, &associated); err != nil {
			continue
		}
		if strings.Contains(video, fmt.Sprintf("a=rtpmap:%d rtx/90000", pt)) {
			return uint8(pt), uint8(associated), true
		}
	}
	return 0, 0, false
}
