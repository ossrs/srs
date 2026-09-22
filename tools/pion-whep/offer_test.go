// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"strings"
	"testing"

	"github.com/pion/interceptor"
	"github.com/pion/webrtc/v4"
)

// offerFor builds the recvonly offer as play does, with pion's default codecs and interceptors, and returns its SDP.
// CreateOffer needs no network, so this runs offline.
func offerFor(t *testing.T, rtx bool) string {
	t.Helper()
	mediaEngine := &webrtc.MediaEngine{}
	if err := mediaEngine.RegisterDefaultCodecs(); err != nil {
		t.Fatalf("register codecs: %v", err)
	}
	registry := &interceptor.Registry{}
	if err := webrtc.RegisterDefaultInterceptors(mediaEngine, registry); err != nil {
		t.Fatalf("register interceptors: %v", err)
	}
	api := webrtc.NewAPI(webrtc.WithMediaEngine(mediaEngine), webrtc.WithInterceptorRegistry(registry))
	pc, err := api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		t.Fatalf("create peer connection: %v", err)
	}
	defer pc.Close()
	if err := addRecvTransceivers(pc, rtx); err != nil {
		t.Fatalf("add transceivers: %v", err)
	}
	offer, err := pc.CreateOffer(nil)
	if err != nil {
		t.Fatalf("create offer: %v", err)
	}
	return offer.SDP
}

// videoSection returns the m=video section of sdp, from its m= line to the next m= line or the end.
func videoSection(t *testing.T, sdp string) string {
	t.Helper()
	start := strings.Index(sdp, "m=video ")
	if start < 0 {
		t.Fatalf("offer has no video section: %q", sdp)
	}
	section := sdp[start:]
	if next := strings.Index(section[1:], "\nm="); next >= 0 {
		section = section[:next+1]
	}
	return section
}

// payloadTypes returns the payload types listed on the m= line that starts section.
func payloadTypes(section string) []string {
	line := section
	if i := strings.IndexAny(line, "\r\n"); i >= 0 {
		line = line[:i]
	}
	fields := strings.Fields(line)
	if len(fields) < 4 {
		return nil
	}
	return fields[3:]
}

// With RTX on, the default, the offer carries an rtx payload with apt for every video codec, as a browser's does,
// so SRS may answer RTX.
func TestOfferWithRtxCarriesRtxForEveryVideoCodec(t *testing.T) {
	video := videoSection(t, offerFor(t, true))
	rtx := strings.Count(video, " rtx/90000")
	apt := strings.Count(video, " apt=")
	media := strings.Count(video, "a=rtpmap:") - rtx
	if rtx == 0 || rtx != apt || rtx != media {
		t.Errorf("rtx offer: %d rtx payloads, %d apt lines, %d media codecs, want one rtx with apt per codec", rtx, apt, media)
	}
	if !strings.Contains(video, "a=rtpmap:106 H264/90000") || !strings.Contains(video, "a=rtpmap:107 rtx/90000") ||
		!strings.Contains(video, "a=fmtp:107 apt=106") {
		t.Errorf("rtx offer lacks H264 106 with rtx 107 apt=106:\n%s", video)
	}
	if want := strings.Count(video, "a=rtpmap:"); len(payloadTypes(video)) != want {
		t.Errorf("m=video lists %d payload types, want %d: %v", len(payloadTypes(video)), want, payloadTypes(video))
	}
}

// With RTX off, the offer keeps pion's default codecs and their nack feedback but carries no rtx payload and no
// apt, on the m= line included, so SRS cannot negotiate RTX even under nack_prefer_rtx on and every
// retransmission is plain on the media SSRC. The audio section does not change.
func TestOfferWithoutRtxKeepsCodecsAndNackButNoRtx(t *testing.T) {
	withRtx := offerFor(t, true)
	without := offerFor(t, false)
	video := videoSection(t, without)

	if strings.Contains(video, " rtx/90000") || strings.Contains(video, " apt=") {
		t.Errorf("plain offer carries rtx:\n%s", video)
	}
	if !strings.Contains(video, "a=rtpmap:106 H264/90000") || !strings.Contains(video, "a=rtcp-fb:106 nack ") ||
		!strings.Contains(video, "a=rtcp-fb:106 nack pli") {
		t.Errorf("plain offer lacks H264 106 with nack:\n%s", video)
	}

	withRtxVideo := videoSection(t, withRtx)
	wantCodecs := strings.Count(withRtxVideo, "a=rtpmap:") - strings.Count(withRtxVideo, " rtx/90000")
	if got := strings.Count(video, "a=rtpmap:"); got != wantCodecs {
		t.Errorf("plain offer has %d video codecs, want the %d of the rtx offer minus rtx", got, wantCodecs)
	}
	if pts := payloadTypes(video); len(pts) != wantCodecs {
		t.Errorf("m=video lists %d payload types, want %d: %v", len(pts), wantCodecs, pts)
	}

	audio := without[strings.Index(without, "m=audio "):]
	if !strings.Contains(audio, "a=rtpmap:111 opus/48000/2") {
		t.Errorf("plain offer lacks opus:\n%s", audio)
	}
}
