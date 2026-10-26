// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"encoding/json"
	"fmt"
	"slices"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/pion/rtcp"

	"srs-integ/internal/check"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// rtcDrop is how many RTP packets a recovery case has SRS's simulator drop.
const rtcDrop = 4

// rtcNackQuiet is how long a case waits to see SRS sends no NACK: SRS
// checks for lost packets every 20ms, and NACKs one 10ms after it misses it.
const rtcNackQuiet = 100 * time.Millisecond

// rtcTWCCQuiet is how long a case waits to see SRS sends no TWCC feedback,
// which it sends every 100ms.
const rtcTWCCQuiet = 150 * time.Millisecond

const (
	h264Mime = "video/H264"
	rtxMime  = "video/rtx"
)

// rtcRecovery adds the loss recovery cases to TestRtc_Common: SRS's NACK
// simulator (/rtc/v1/nack/, built with --simulator=on) drops packets a
// player would get, or a publisher sent, and SRS asks the publisher for
// them again, or sends them to the player again on the media stream, since
// Common keeps nack_prefer_rtx off; and the TWCC feedback a publisher gets.
// The recovery cases publish only video, so each packet the simulator drops
// is one NACK covers.
func rtcRecovery(t *testing.T) {
	// NackApiInvalidDrop asks the simulator to drop no packets, a negative
	// number or not a number: the API answers code 5023 (RtcInvalidParams)
	// and echoes the query.
	check.Case(t, "NackApiInvalidDrop", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		drops := []string{"", "0", "-1", "x"}
		answers := make([]nackAnswer, len(drops))
		var wg sync.WaitGroup
		for i, drop := range drops {
			wg.Go(func() { answers[i] = getNackAPI(srs, "username=nobody:nobody&drop="+drop) })
		}
		wg.Wait()
		for i, drop := range drops {
			a := answers[i]
			c.Must(a.err, "drop="+drop)
			c.CheckEqual(a.status, 200, "drop="+drop+" status")
			c.CheckEqual(a.Code, 5023, "drop="+drop+" code")
			c.CheckEqual(a.Query["username"], "nobody:nobody", "drop="+drop+" query username")
			c.CheckEqual(a.Query["drop"], drop, "drop="+drop+" query drop")
		}
	})

	// NackApiNoSession asks the simulator to drop packets of a session that
	// doesn't exist: code 5022 (RtcNoSession).
	check.Case(t, "NackApiNoSession", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		code, _ := nackAPI(c, srs, "username=nobody:"+streamName(c)+"&drop=1")
		c.CheckEqual(code, 5022, "code")
	})

	// NackPlayerPlain drops packets SRS sends a player: the player's NACK
	// generator asks for them, and SRS sends each again on the media stream.
	check.Case(t, "NackPlayerPlain", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		_, player, more := rtcRecoveryStart(c, srs, nil, nil)
		nackDrop(c, srs, &player.Session)
		more()
		rec := waitPlayerRecovered(c, player, false)
		checkPlayerNacked(c, rec)
		c.Check(rec.Received(rtxMime) == nil, "an RTX stream, want none with nack_prefer_rtx off")
	})

	// NackPlayerRtxNotPreferred plays with a player that offers RTX: Common
	// keeps nack_prefer_rtx off, so the answer has no rtx, and the lost
	// packets come again on the media stream.
	check.Case(t, "NackPlayerRtxNotPreferred", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		_, player, more := rtcRecoveryStart(c, srs, nil, &rtc.Options{RTX: true})
		c.Check(strings.Contains(player.Offer, "rtx/90000"), "the offer has no rtx")
		c.Check(!strings.Contains(player.Answer, "rtx/"), "the answer has rtx, want none with nack_prefer_rtx off")
		nackDrop(c, srs, &player.Session)
		more()
		rec := waitPlayerRecovered(c, player, false)
		checkPlayerNacked(c, rec)
		c.Check(rec.Received(rtxMime) == nil, "an RTX stream, want none")
	})

	// NackPublisherPlain drops packets of a publisher: SRS asks for them by
	// NACK, the publisher's NACK responder sends each again on the media
	// stream, and the player gets them, late, where they belong.
	check.Case(t, "NackPublisherPlain", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		pub, player, more := rtcRecoveryStart(c, srs, nil, nil)
		nackDrop(c, srs, &pub.Session)
		more()
		nacked := waitPublisherResent(c, pub, false)
		checkConsecutive(c, nacked, "the NACKed")
		rec := waitPlayerRecovered(c, player, false)
		c.CheckEqual(len(rec.Received(h264Mime).Missed), rtcDrop, "the player's missed packets")
	})

	// TwccFeedback publishes with the transport-wide sequence number in
	// each packet: the answer takes the extension, and SRS sends TWCC
	// feedback about the packets it received, numbered from 0.
	check.Case(t, "TwccFeedback", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TWCC: true})
		for _, kind := range []string{"video", "audio"} {
			c.Check(strings.Contains(sdpSection(pub.Answer, kind), twccExtension), "the %v answer has no TWCC extension", kind)
		}
		sendRTCSamples(c, rtcSamples(c), pub)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		rec, err := pub.Recorder.Until(ctx, func(rec *rtc.Record) bool { return len(rtc.TWCCFeedback(rec.RTCPIn)) >= 2 })
		c.Must(err, "two TWCC feedbacks")
		c.Check(len(rec.TWCC) > 0, "no transport-wide sequence number sent")
		fb := rtc.TWCCFeedback(rec.RTCPIn)
		for i, f := range fb {
			c.CheckEqual(int(f.FbPktCount), i, fmt.Sprintf("feedback %d count", i))
			c.Check(f.PacketStatusCount > 0, "feedback %d covers no packet", i)
			c.Check(slices.Contains(rec.TWCC, f.BaseSequenceNumber), "feedback %d base %v was not sent", i, f.BaseSequenceNumber)
			last := f.BaseSequenceNumber + f.PacketStatusCount - 1
			c.Check(slices.Contains(rec.TWCC, last), "feedback %d ends at %v, which was not sent", i, last)
			c.Check(len(f.RecvDeltas) > 0, "feedback %d has no packet received", i)
		}
		if len(fb) > 0 {
			c.Check(fb[0].BaseSequenceNumber == rec.TWCC[0], "the first feedback starts at %v, want the first packet %v",
				fb[0].BaseSequenceNumber, rec.TWCC[0])
		}
	})
}

// sharedFmp4KeysRTC are the WebRTC settings of the shared Fmp4Keys server.
var sharedFmp4KeysRTC = server.RTC{DTLSRole: "active", StunStrictCheck: true, NoAPICandidates: true, NackPreferRTX: true}

// TestRtc_PreferRtx tests nack_prefer_rtx on: SRS sends a packet a player
// asks for again on the RTX stream (RFC 4588) when the player offers RTX,
// and on the media stream when it doesn't; a publisher that offers RTX
// sends the packets SRS asks for on its RTX stream. It shares the Fmp4Keys
// server, whose HLS and DASH settings don't touch WebRTC sessions.
func TestRtc_PreferRtx(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())
	rtcLossRtx(t, srs)

	// PlayerRtx plays with a player that offers RTX: the answer has rtx for
	// H.264 and an RTX stream, and each packet the simulator drops comes
	// again on it, with its sequence number in the RTX payload.
	check.Case(t, "PlayerRtx", check.Normal, func(c *check.C) {
		_, player, more := rtcRecoveryStart(c, srs, nil, &rtc.Options{RTX: true})
		checkRtxAnswer(c, player.Answer, true)
		nackDrop(c, srs, &player.Session)
		more()
		rec := waitPlayerRecovered(c, player, true)
		checkPlayerNacked(c, rec)
		v := rec.Received(h264Mime)
		c.CheckEqual(len(v.Late), 0, "packets late on the media stream")
	})

	// PlayerWithoutRtxPlain plays with a player that doesn't offer RTX: the
	// answer has none, and the lost packets come again on the media stream.
	check.Case(t, "PlayerWithoutRtxPlain", check.Normal, func(c *check.C) {
		_, player, more := rtcRecoveryStart(c, srs, nil, nil)
		checkRtxAnswer(c, player.Answer, false)
		nackDrop(c, srs, &player.Session)
		more()
		rec := waitPlayerRecovered(c, player, false)
		checkPlayerNacked(c, rec)
		c.Check(rec.Received(rtxMime) == nil, "an RTX stream, want none")
	})

	// PublisherRtx publishes with RTX: the answer takes rtx, the publisher
	// sends the packets SRS asks for on its RTX stream, not again on the
	// media stream, and SRS gives them to the player on the media stream.
	check.Case(t, "PublisherRtx", check.Normal, func(c *check.C) {
		pub, player, more := rtcRecoveryStart(c, srs, &rtc.Options{RTX: true}, nil)
		checkRtxAnswer(c, pub.Answer, true)
		nackDrop(c, srs, &pub.Session)
		more()
		nacked := waitPublisherResent(c, pub, true)
		checkConsecutive(c, nacked, "the NACKed")
		c.CheckEqual(len(pub.Recorder.Record().Sent(h264Mime).Repeated), 0, "packets sent again on the media stream")
		rec := waitPlayerRecovered(c, player, false)
		c.CheckEqual(len(rec.Received(h264Mime).Missed), rtcDrop, "the player's missed packets")
	})
}

// sharedTsFloorRTC are the WebRTC settings of the shared TsFloor server.
var sharedTsFloorRTC = server.RTC{NoNack: true, NoTWCC: true}

// TestRtc_NackTwccOff tests nack and twcc off: SRS asks a publisher for no
// lost packet, sends a player none again, and sends no TWCC feedback. It
// shares the TsFloor server, whose HLS settings don't touch WebRTC sessions;
// not RemuxOff, whose DASH groups keep its one core busier.
func TestRtc_NackTwccOff(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsFloor", sharedTsFloor())
	rtcLossNackOff(t, srs)

	// NackOffPlayer drops packets SRS sends a player: the answer offers no
	// NACK, and when the player asks for the lost packets anyway, SRS
	// doesn't send them. The player asks for a keyframe after the NACK, and
	// SRS handles a session's RTCP in order and would send the packets at
	// once, so the request reaching the publisher shows the NACK was handled.
	check.Case(t, "NackOffPlayer", check.Normal, func(c *check.C) {
		pub, player, more := rtcRecoveryStart(c, srs, nil, nil)
		checkNoNack(c, player.Answer)
		nackDrop(c, srs, &player.Session)
		more()
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		rec, err := player.Recorder.Until(ctx, func(rec *rtc.Record) bool {
			v := rec.Received(h264Mime)
			return v != nil && len(v.Missed) >= rtcDrop
		})
		c.Must(err, "the player misses the dropped packets")
		v := rec.Received(h264Mime)
		checkConsecutive(c, v.Missed, "the missed")
		c.CheckEqual(len(rtc.Nacked(rec.RTCPOut, v.SSRC)), 0, "packets the player NACKed by itself")
		pairs := rtcp.NackPairsFromSequenceNumbers(v.Missed)
		plis := pub.PLIs()
		c.Must(player.PC.WriteRTCP([]rtcp.Packet{&rtcp.TransportLayerNack{MediaSSRC: v.SSRC, Nacks: pairs}}), "NACK")
		c.Must(player.RequestKeyframe(), "the keyframe request after the NACK")
		c.Must(pub.WaitPLI(ctx, plis), "the publisher gets the keyframe request")
		// A packet sent again would be on its way before the request.
		time.Sleep(20 * time.Millisecond)
		rec = player.Recorder.Record()
		v = rec.Received(h264Mime)
		c.CheckEqual(len(v.Late), 0, "packets sent again")
		c.Check(len(v.Missed) == rtcDrop, "%v packets missed, want %v", len(v.Missed), rtcDrop)
	})

	// NackOffPublisher drops packets of a publisher: SRS sends it no NACK,
	// and the player never gets them.
	check.Case(t, "NackOffPublisher", check.Normal, func(c *check.C) {
		pub, player, more := rtcRecoveryStart(c, srs, nil, nil)
		checkNoNack(c, pub.Answer)
		nackDrop(c, srs, &pub.Session)
		more()
		// SRS has the packets after the dropped ones at once.
		time.Sleep(rtcNackQuiet)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Recorder.Until(ctx, func(rec *rtc.Record) bool {
			v := rec.Received(h264Mime)
			return v != nil && len(v.Missed) >= rtcDrop
		})
		c.Must(err, "the player misses the dropped packets")
		rec := pub.Recorder.Record()
		c.CheckEqual(len(rtc.Nacked(rec.RTCPIn, rec.Sent(h264Mime).SSRC)), 0, "packets SRS NACKed")
		c.CheckEqual(len(rec.Sent(h264Mime).Repeated), 0, "packets the publisher sent again")
		c.CheckEqual(len(player.Recorder.Record().Received(h264Mime).Late), 0, "packets the player got late")
	})

	// TwccOff publishes with the transport-wide sequence number: the answer
	// doesn't take the extension, so the publisher sends none, and SRS
	// sends no TWCC feedback.
	check.Case(t, "TwccOff", check.Normal, func(c *check.C) {
		pub := rtcPublisher(c, whipURL(srs, "live", streamName(c)), &rtc.Options{TWCC: true})
		c.Check(strings.Contains(pub.Offer, twccExtension), "the offer has no TWCC extension")
		c.Check(!strings.Contains(pub.Answer, twccExtension), "the answer has the TWCC extension")
		c.Check(!strings.Contains(pub.Answer, "transport-cc"), "the answer has transport-cc feedback")
		sendRTCSamples(c, rtcSamples(c), pub)
		time.Sleep(rtcTWCCQuiet)
		rec := pub.Recorder.Record()
		c.Check(rec.Sent(h264Mime) != nil && rec.Sent(h264Mime).Packets > 0, "no video sent")
		c.CheckEqual(len(rec.TWCC), 0, "transport-wide sequence numbers sent")
		c.CheckEqual(len(rtc.TWCCFeedback(rec.RTCPIn)), 0, "TWCC feedbacks")
	})
}

// twccExtension is the URI of the transport-wide sequence number header
// extension.
const twccExtension = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"

// rtcRecoveryStart connects a publisher and a player with these options,
// sends the first 200ms of the video of the WHIP test file at once, and
// waits until the player plays it. The case then calls the returned more,
// after it arms the simulator, which sends the next 400ms at once, so the
// dropped packets come at once, and the rest in real time until the case
// ends.
func rtcRecoveryStart(c *check.C, srs *server.Server, pubOpts, playOpts *rtc.Options) (*rtc.Publisher, *rtc.Player, func()) {
	c.Helper()
	pub, player := rtcConnectBoth(c, srs, streamName(c), pubOpts, playOpts)
	samples := rtcVideoSamples(c)
	split := func(d time.Duration) int {
		i := 0
		for i < len(samples) && samples[i].DTS < d {
			i++
		}
		return i
	}
	lead, burst := split(rtcLead), split(rtcLead+rtcBurst)
	// Each sample of the lead goes at once, so the sender is done when the
	// player has them.
	stop := sendRTCSamples(c, samples[:lead], pub, player)
	waitPlayed(c, player, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5})
	stop()
	more := func() {
		c.Helper()
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		c.Must(pub.Send(ctx, samples[lead:burst], nil), "send the burst")
		sendRest(c, pub, samples[burst:])
	}
	return pub, player, more
}

// rtcConnectBoth connects a publisher and a player of a stream at the same
// time, each closed when the case ends.
func rtcConnectBoth(c *check.C, srs *server.Server, stream string, pubOpts, playOpts *rtc.Options) (*rtc.Publisher, *rtc.Player) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	var pub *rtc.Publisher
	var player *rtc.Player
	var pubErr, playErr error
	var wg sync.WaitGroup
	wg.Go(func() { pub, pubErr = rtc.Publish(ctx, whipURL(srs, "live", stream), rtcOptions(pubOpts)) })
	wg.Go(func() { player, playErr = rtc.Play(ctx, whepURL(srs, "live", stream), rtcOptions(playOpts)) })
	wg.Wait()
	if pub != nil {
		c.Cleanup(func() { pub.Close() })
	}
	if player != nil {
		c.Cleanup(func() { player.Close() })
	}
	c.Must(pubErr, "WHIP publish")
	c.Must(playErr, "WHEP play")
	return pub, player
}

// rtcBurst is the media rtcRecoveryStart sends at once after the
// simulator is armed: more than the packets it drops.
const rtcBurst = 400 * time.Millisecond

// sendRest sends samples in real time from the first, until the case ends.
func sendRest(c *check.C, pub *rtc.Publisher, samples []rtc.Sample) {
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		defer close(done)
		pub.Send(ctx, samples, &rtmp.Pacer{})
	}()
	c.Cleanup(func() {
		cancel()
		<-done
	})
}

// rtcVideoSamples are the video samples of the WHIP test file.
func rtcVideoSamples(c *check.C) []rtc.Sample {
	c.Helper()
	var out []rtc.Sample
	for _, s := range rtcSamples(c) {
		if s.Video {
			out = append(out, s)
		}
	}
	return out
}

// A nackAnswer is the answer of SRS's NACK simulator.
type nackAnswer struct {
	Code   int               `json:"code"`
	Query  map[string]string `json:"query"`
	status int
	err    error
}

// getNackAPI sends a request to SRS's NACK simulator.
func getNackAPI(srs *server.Server, query string) (a nackAnswer) {
	res, err := httpClient.Get(srs.APIURL("/rtc/v1/nack/?" + query))
	if err != nil {
		a.err = err
		return a
	}
	defer res.Body.Close()
	a.status = res.StatusCode
	if err := json.NewDecoder(res.Body).Decode(&a); err != nil {
		a.err = fmt.Errorf("decode the answer: %w", err)
	}
	return a
}

// nackAPI sends a request to SRS's NACK simulator, and returns the code and
// the query of its answer.
func nackAPI(c *check.C, srs *server.Server, query string) (int, map[string]string) {
	c.Helper()
	a := getNackAPI(srs, query)
	c.Must(a.err, "the NACK simulator")
	c.CheckEqual(a.status, 200, "the NACK simulator's status")
	return a.Code, a.Query
}

// nackDrop has SRS's NACK simulator drop the next rtcDrop RTP packets of a
// session: those SRS would send a player, or those it receives from a
// publisher.
func nackDrop(c *check.C, srs *server.Server, s *rtc.Session) {
	c.Helper()
	username, err := s.Username()
	c.Must(err, "the session's username")
	code, _ := nackAPI(c, srs, fmt.Sprintf("username=%s&drop=%d", username, rtcDrop))
	if code != 0 {
		c.Fatalf("the NACK simulator answers code %v", code)
	}
}

// waitPlayerRecovered waits at most 1s until the player has missed rtcDrop
// video packets and got each of them again: late on the media stream, or
// on the RTX stream when rtx is set.
func waitPlayerRecovered(c *check.C, p *rtc.Player, rtx bool) *rtc.Record {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	rec, err := p.Recorder.Until(ctx, func(rec *rtc.Record) bool {
		v := rec.Received(h264Mime)
		if v == nil || len(v.Missed) < rtcDrop {
			return false
		}
		if !rtx {
			return len(v.Late) >= len(v.Missed)
		}
		x := rec.Received(rtxMime)
		return x != nil && containsAll(x.OSNs, v.Missed)
	})
	if err != nil {
		v := rec.Received(h264Mime)
		c.Fatalf("the player recovers %v dropped packets: %v; video %+v, RTX %+v", rtcDrop, err, v, rec.Received(rtxMime))
	}
	v := rec.Received(h264Mime)
	c.CheckEqual(len(v.Missed), rtcDrop, "missed packets")
	checkConsecutive(c, v.Missed, "the missed")
	if !rtx {
		c.Check(containsAll(v.Late, v.Missed), "late %v, want the missed %v", v.Late, v.Missed)
	}
	return rec
}

// checkPlayerNacked checks the player asked for each packet it missed.
func checkPlayerNacked(c *check.C, rec *rtc.Record) {
	c.Helper()
	v := rec.Received(h264Mime)
	nacked := rtc.Nacked(rec.RTCPOut, v.SSRC)
	c.Check(containsAll(nacked, v.Missed), "NACKed %v, want the missed %v", nacked, v.Missed)
}

// waitPublisherResent waits at most 1s until SRS has asked the publisher
// for rtcDrop video packets by NACK and the publisher has sent each again:
// on the media stream, or on the RTX stream when rtx is set. It returns the
// NACKed sequence numbers.
func waitPublisherResent(c *check.C, pub *rtc.Publisher, rtx bool) []uint16 {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	rec, err := pub.Recorder.Until(ctx, func(rec *rtc.Record) bool {
		v := rec.Sent(h264Mime)
		if v == nil {
			return false
		}
		nacked := rtc.Nacked(rec.RTCPIn, v.SSRC)
		if len(nacked) < rtcDrop {
			return false
		}
		if !rtx {
			return containsAll(v.Repeated, nacked)
		}
		x := rec.Sent(rtxMime)
		return x != nil && containsAll(x.OSNs, nacked)
	})
	v := rec.Sent(h264Mime)
	if err != nil {
		c.Fatalf("SRS NACKs and the publisher sends again %v dropped packets: %v; video %+v, RTX %+v, NACKed %v",
			rtcDrop, err, v, rec.Sent(rtxMime), rtc.Nacked(rec.RTCPIn, v.SSRC))
	}
	nacked := rtc.Nacked(rec.RTCPIn, v.SSRC)
	c.CheckEqual(len(nacked), rtcDrop, "NACKed packets")
	return nacked
}

// checkConsecutive checks sequence numbers follow each other: the packets
// the simulator drops are the next ones.
func checkConsecutive(c *check.C, seqs []uint16, what string) {
	c.Helper()
	for i := 1; i < len(seqs); i++ {
		if seqs[i] != seqs[i-1]+1 {
			c.Errorf("%v sequence numbers %v don't follow each other", what, seqs)
			return
		}
	}
}

// checkRtxAnswer checks the video section of an answer has an rtx codec for
// H.264, and an RTX stream with it for a player's answer, or neither.
func checkRtxAnswer(c *check.C, answer string, want bool) {
	c.Helper()
	video := sdpSection(answer, "video")
	pt := ""
	for _, line := range strings.Split(video, "\n") {
		if v, ok := strings.CutPrefix(strings.TrimSpace(line), "a=rtpmap:"); ok && strings.Contains(v, " H264/") {
			pt, _, _ = strings.Cut(v, " ")
		}
	}
	has := strings.Contains(video, " rtx/90000") && pt != "" && strings.Contains(video, "apt="+pt)
	c.CheckEqual(has, want, "the answer has rtx for H.264 payload type "+pt)
	if want && strings.Contains(video, "a=sendonly") {
		c.Check(strings.Contains(video, "a=ssrc-group:FID "), "the player's answer has no RTX stream (FID group)")
	}
}

// checkNoNack checks no section of an answer offers NACK feedback.
func checkNoNack(c *check.C, answer string) {
	c.Helper()
	for _, line := range strings.Split(answer, "\n") {
		if strings.HasPrefix(line, "a=rtcp-fb:") && strings.Contains(line, " nack") {
			c.Errorf("the answer offers NACK: %q", strings.TrimSpace(line))
		}
	}
}

// sdpSection is the media section of a kind in an SDP.
func sdpSection(s, kind string) string {
	for _, m := range strings.Split(s, "\nm=")[1:] {
		if strings.HasPrefix(m, kind+" ") {
			return m
		}
	}
	return ""
}

// containsAll reports whether have holds every one of want.
func containsAll(have, want []uint16) bool {
	for _, w := range want {
		if !slices.Contains(have, w) {
			return false
		}
	}
	return true
}
