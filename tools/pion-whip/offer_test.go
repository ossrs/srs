// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"regexp"
	"strings"
	"testing"

	"github.com/pion/interceptor"
	"github.com/pion/webrtc/v4"
)

// offerFor builds the sendonly offer as publish does, with the media engine for rtx, pion's default interceptors
// and the two tracks, and returns its SDP. CreateOffer needs no network, so this runs offline.
func offerFor(t *testing.T, rtx bool) string {
	t.Helper()
	mediaEngine, err := newMediaEngine(rtx)
	if err != nil {
		t.Fatalf("media engine: %v", err)
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
	video, audio, err := newTracks()
	if err != nil {
		t.Fatalf("tracks: %v", err)
	}
	if _, err := addSendonlyTracks(pc, video, audio); err != nil {
		t.Fatalf("add tracks: %v", err)
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

// ssrcsOf returns the distinct SSRCs the a=ssrc: lines of section declare.
func ssrcsOf(section string) []string {
	seen := map[string]bool{}
	var ssrcs []string
	for _, m := range regexp.MustCompile(`(?m)^a=ssrc:(\d+) `).FindAllStringSubmatch(section, -1) {
		if !seen[m[1]] {
			seen[m[1]] = true
			ssrcs = append(ssrcs, m[1])
		}
	}
	return ssrcs
}

// With RTX on, the default, the offer is sendonly and carries an rtx payload with apt for every video codec and an
// a=ssrc-group:FID binding the video SSRC to an RTX SSRC, as a browser's or FFmpeg's WHIP offer does, so SRS may
// answer RTX.
func TestOfferWithRtxCarriesRtxAndFidForTheVideo(t *testing.T) {
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
	if !strings.Contains(video, "a=sendonly") {
		t.Errorf("rtx offer is not sendonly:\n%s", video)
	}
	ssrcs := ssrcsOf(video)
	if len(ssrcs) != 2 || !strings.Contains(video, "a=ssrc-group:FID "+ssrcs[0]+" "+ssrcs[1]) {
		t.Errorf("rtx offer has no FID group binding its two SSRCs %v:\n%s", ssrcs, video)
	}
	if want := strings.Count(video, "a=rtpmap:"); len(payloadTypes(video)) != want {
		t.Errorf("m=video lists %d payload types, want %d: %v", len(payloadTypes(video)), want, payloadTypes(video))
	}
}

// With RTX off, the offer is that of a peer without RTX: pion's default codecs and their nack feedback, but no rtx
// payload, no apt, one video SSRC and no FID group, so SRS cannot negotiate RTX even under nack_prefer_rtx on and
// every retransmission is plain on the media SSRC. The audio section does not change.
func TestOfferWithoutRtxKeepsCodecsAndNackButNoRtxOrFid(t *testing.T) {
	withRtx := offerFor(t, true)
	without := offerFor(t, false)
	video := videoSection(t, without)

	if strings.Contains(video, " rtx/90000") || strings.Contains(video, " apt=") || strings.Contains(video, "a=ssrc-group:FID") {
		t.Errorf("plain offer carries rtx or a FID group:\n%s", video)
	}
	if ssrcs := ssrcsOf(video); len(ssrcs) != 1 {
		t.Errorf("plain offer declares %d video SSRCs %v, want the media SSRC alone", len(ssrcs), ssrcs)
	}
	if !strings.Contains(video, "a=rtpmap:106 H264/90000") || !strings.Contains(video, "a=rtcp-fb:106 nack ") ||
		!strings.Contains(video, "a=rtcp-fb:106 nack pli") || !strings.Contains(video, "a=sendonly") {
		t.Errorf("plain offer lacks sendonly H264 106 with nack:\n%s", video)
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

// rtxOfAnswer reads the RTX payload SRS kept in a publish answer, which carries no FID group of its own: the rtx
// rtpmap and its apt name the format, and their absence means the session uses plain retransmission.
func TestRtxOfAnswer(t *testing.T) {
	rtx := "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\n" +
		"m=video 9 UDP/TLS/RTP/SAVPF 106 107\r\na=rtpmap:106 H264/90000\r\na=rtcp-fb:106 nack\r\n" +
		"a=rtpmap:107 rtx/90000\r\na=fmtp:107 apt=106\r\n"
	pt, apt, ok := rtxOfAnswer(rtx)
	if !ok || pt != 107 || apt != 106 {
		t.Errorf("rtxOfAnswer(rtx answer) = %d, %d, %v, want 107, 106, true", pt, apt, ok)
	}

	plain := "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\n" +
		"m=video 9 UDP/TLS/RTP/SAVPF 106\r\na=rtpmap:106 H264/90000\r\na=rtcp-fb:106 nack\r\n"
	if _, _, ok := rtxOfAnswer(plain); ok {
		t.Errorf("rtxOfAnswer(plain answer) = ok, want false")
	}
}
