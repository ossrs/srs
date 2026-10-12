// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// rtcErrors adds the WHIP and WHEP error cases to TestRtc_Common: a bad
// offer is 400, a busy stream 409, a publish or play a hook refuses 401, and
// a stream nobody publishes over WebRTC.
func rtcErrors(t *testing.T) {
	// WhipBadSdp, WhipEmptySdp and WhepBadSdp post a body that is not an
	// SDP offer.
	check.Case(t, "WhipBadSdp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		rtcRefused(c, whipURL(srs, "live", streamName(c)), "not an sdp", http.StatusBadRequest)
	})

	check.Case(t, "WhipEmptySdp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		rtcRefused(c, whipURL(srs, "live", streamName(c)), "", http.StatusBadRequest)
	})

	check.Case(t, "WhepBadSdp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		rtcRefused(c, whepURL(srs, "live", streamName(c)), "not an sdp", http.StatusBadRequest)
	})

	// WhipNoBundle offers without BUNDLE, which SRS requires.
	check.Case(t, "WhipNoBundle", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		offer := rtcOffer(c, true, []string{"H264"}, []string{"opus"})
		var lines []string
		for _, l := range strings.Split(offer, "\r\n") {
			if !strings.HasPrefix(l, "a=group:BUNDLE") {
				lines = append(lines, l)
			}
		}
		rtcRefused(c, whipURL(srs, "live", streamName(c)), strings.Join(lines, "\r\n"), http.StatusBadRequest)
	})

	// WhipRecvonlyOffer posts a player's offer to WHIP, and
	// WhepSendonlyOffer a publisher's offer to WHEP.
	check.Case(t, "WhipRecvonlyOffer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		offer := rtcOffer(c, false, []string{"H264"}, []string{"opus"})
		rtcRefused(c, whipURL(srs, "live", streamName(c)), offer, http.StatusBadRequest)
	})

	check.Case(t, "WhepSendonlyOffer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		offer := rtcOffer(c, true, []string{"H264"}, []string{"opus"})
		rtcRefused(c, whepURL(srs, "live", streamName(c)), offer, http.StatusBadRequest)
	})

	// WhipBusyRtmp publishes by WHIP a stream RTMP publishes: 409, and no
	// on_publish for it.
	check.Case(t, "WhipBusyRtmp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264BaselineAAC), rtcLead)
		waitHook(c, srs, "on_publish", stream)
		offer := rtcOffer(c, true, []string{"H264"}, []string{"opus"})
		rtcRefused(c, whipURL(srs, "live", stream), offer, http.StatusConflict)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 1, "on_publish requests")
	})

	// WhipHookRejected and WhepHookRejected publish and play a stream whose
	// on_publish or on_play the hook server refuses: 401.
	check.Case(t, "WhipHookRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		srs.Hooks.Script(mock.Match{Action: "on_publish", Stream: stream}, mock.Reject(403))
		offer := rtcOffer(c, true, []string{"H264"}, []string{"opus"})
		rtcRefused(c, whipURL(srs, "live", stream), offer, http.StatusUnauthorized)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 1, "on_publish requests")
	})

	check.Case(t, "WhepHookRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		srs.Hooks.Script(mock.Match{Action: "on_play", Stream: stream}, mock.Reject(403))
		offer := rtcOffer(c, false, []string{"H264"}, []string{"opus"})
		rtcRefused(c, whepURL(srs, "live", stream), offer, http.StatusUnauthorized)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_play", Stream: stream})), 1, "on_play requests")
	})

	// WhipRejectedThenPublish publishes again once the hook server accepts:
	// the refused publish holds nothing, so it is 201 and plays.
	check.Case(t, "WhipRejectedThenPublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		srs.Hooks.Script(mock.Match{Action: "on_publish", Stream: stream}, mock.Reject(403))
		offer := rtcOffer(c, true, []string{"H264"}, []string{"opus"})
		rtcRefused(c, whipURL(srs, "live", stream), offer, http.StatusUnauthorized)
		srs.Hooks.Script(mock.Match{Action: "on_publish", Stream: stream}, mock.OK())
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		pub, err := rtc.Publish(ctx, whipURL(srs, "live", stream), rtcOptions(nil))
		if pub != nil {
			c.Cleanup(func() { pub.Close() })
		}
		c.Must(err, "publish after the hook accepts")
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// WhepMissingStream plays a stream nobody publishes: SRS answers 201 and
	// connects, as it does for a player that comes before the publisher, and
	// sends nothing.
	check.Case(t, "WhepMissingStream", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		player := rtcPlayer(c, whepURL(srs, "live", streamName(c)), nil)
		c.CheckEqual(player.Response.StatusCode, http.StatusCreated, "status")
		time.Sleep(100 * time.Millisecond)
		c.CheckEqual(player.Packets(), 0, "RTP packets of a stream nobody publishes")
	})

	// WhepRtmpStream plays by WHEP a stream only RTMP publishes: with
	// rtmp_to_rtc off, SRS refuses it rather than answer a session that never
	// gets media.
	check.Case(t, "WhepRtmpStream", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264BaselineAAC), rtcLead)
		waitHook(c, srs, "on_publish", stream)
		offer := rtcOffer(c, false, []string{"H264"}, []string{"opus"})
		s, err := rtc.Post(context.Background(), whepURL(srs, "live", stream), offer, rtcOptions(nil))
		rtcDeleteLater(c, s, err)
		var se *rtc.StatusError
		c.Check(errors.As(err, &se) && se.Code >= 400, "WHEP of an RTMP stream: %v, want it refused", err)
	})
}

// rtcParams adds the cases of the WHIP and WHEP query parameters to
// TestRtc_Common: codec, vcodec and acodec choose the codecs, eip and
// candidate add a candidate, and encrypt and dtls turn off SRTP and DTLS.
func rtcParams(t *testing.T) {
	// WhipVcodec publishes an offer of H.264 and VP9 with vcodec=vp9: the
	// answer takes VP9 only.
	check.Case(t, "WhipVcodec", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c))+"&vcodec=vp9", rtcOffer(c, true, []string{"H264", "VP9"}, []string{"opus"}))
		c.CheckEqual(answerCodecs(s.Answer, "video"), []string{"VP9"}, "video codecs")
	})

	// WhipCodecAlias: codec is vcodec when vcodec is not set.
	check.Case(t, "WhipCodecAlias", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		offer := rtcOffer(c, true, []string{"H264", "VP9"}, []string{"opus"})
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c))+"&codec=vp9", offer)
		c.CheckEqual(answerCodecs(s.Answer, "video"), []string{"VP9"}, "video codecs with codec=vp9")
		s = rtcAnswered(c, whipURL(srs, "live", streamName(c))+"&vcodec=h264&codec=vp9", offer)
		c.CheckEqual(answerCodecs(s.Answer, "video"), []string{"H264"}, "video codecs with vcodec=h264&codec=vp9")
	})

	// WhipVideoDefault offers VP9 before H.264 without vcodec: SRS takes
	// H.264.
	check.Case(t, "WhipVideoDefault", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"VP9", "H264"}, []string{"opus"}))
		c.CheckEqual(answerCodecs(s.Answer, "video"), []string{"H264"}, "video codecs")
	})

	// WhipVcodecNotOffered asks for AV1 that the offer doesn't have: 400.
	check.Case(t, "WhipVcodecNotOffered", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		rtcRefused(c, whipURL(srs, "live", streamName(c))+"&vcodec=av1", rtcOffer(c, true, []string{"H264"}, []string{"opus"}), http.StatusBadRequest)
	})

	// WhipAcodec offers Opus, PCMU and PCMA: acodec takes one of them, in any
	// case, and Opus is the default.
	check.Case(t, "WhipAcodec", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		offer := rtcOffer(c, true, []string{"H264"}, []string{"opus", "PCMU", "PCMA"})
		for _, w := range []struct{ query, codec string }{{"&acodec=pcmu", "PCMU"}, {"&acodec=PCMA", "PCMA"}, {"", "opus"}} {
			s := rtcAnswered(c, whipURL(srs, "live", streamName(c))+w.query, offer)
			c.CheckEqual(answerCodecs(s.Answer, "audio"), []string{w.codec}, "audio codecs with "+w.query)
		}
	})

	// WhipAcodecNotOffered asks for PCMA that the offer doesn't have: 400.
	check.Case(t, "WhipAcodecNotOffered", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		rtcRefused(c, whipURL(srs, "live", streamName(c))+"&acodec=pcma", rtcOffer(c, true, []string{"H264"}, []string{"opus"}), http.StatusBadRequest)
	})

	// WhepVcodecPlays plays the H.264 stream with vcodec=h264.
	check.Case(t, "WhepVcodecPlays", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream)+"&vcodec=h264", nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// WhepVcodecMismatch asks for HEVC of a stream that publishes H.264:
	// SRS never transcodes for WebRTC, so it is 400.
	check.Case(t, "WhepVcodecMismatch", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
		offer := rtcOffer(c, false, []string{"H264", "H265"}, []string{"opus"})
		rtcRefused(c, whepURL(srs, "live", stream)+"&vcodec=hevc", offer, http.StatusBadRequest)
	})

	// WhepAcodecIgnored plays with acodec=pcmu: a player gets the stream's
	// audio codec, Opus.
	check.Case(t, "WhepAcodecIgnored", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
		s := rtcAnswered(c, whepURL(srs, "live", stream)+"&acodec=pcmu", rtcOffer(c, false, []string{"H264"}, []string{"PCMU", "opus"}))
		c.CheckEqual(answerCodecs(s.Answer, "audio"), []string{"opus"}, "audio codecs")
	})

	// EipCandidate publishes with eip: the answer has a host candidate at
	// that address on SRS's UDP port, beside 127.0.0.1, and the session
	// connects and plays through 127.0.0.1.
	check.Case(t, "EipCandidate", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream)+"&eip=10.20.30.40", nil)
		c.CheckEqual(answerCandidates(pub.Answer, srs), []string{"10.20.30.40", "127.0.0.1"}, "candidates")
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// CandidateParam plays with candidate, the other name of eip.
	check.Case(t, "CandidateParam", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whepURL(srs, "live", streamName(c))+"&candidate=10.20.30.41", rtcOffer(c, false, []string{"H264"}, []string{"opus"}))
		c.CheckEqual(answerCandidates(s.Answer, srs), []string{"10.20.30.41", "127.0.0.1"}, "candidates")
	})

	// EipOverCandidate sets both: eip is taken, candidate is not.
	check.Case(t, "EipOverCandidate", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whepURL(srs, "live", streamName(c))+"&eip=10.20.30.42&candidate=10.20.30.43", rtcOffer(c, false, []string{"H264"}, []string{"opus"}))
		c.CheckEqual(answerCandidates(s.Answer, srs), []string{"10.20.30.42", "127.0.0.1"}, "candidates")
	})

	// DtlsOffAloneEncrypts plays with dtls=false but encrypt at its default:
	// SRS keeps DTLS and SRTP, so a pion player connects and plays.
	check.Case(t, "DtlsOffAloneEncrypts", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream)+"&dtls=false", nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// SrtpByDefault plays with a raw client that does DTLS but sends and
	// reads RTP and RTCP in clear: SRS drops its clear keyframe request, and
	// the packets it sends are SRTP, so they come but don't play.
	check.Case(t, "SrtpByDefault", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcRawPlayer(c, whepURL(srs, "live", stream), true)
		ctx, cancel := context.WithCancel(context.Background())
		c.Cleanup(cancel)
		go pub.Send(ctx, rtcSamples(c), &rtmp.Pacer{})
		deadline := time.Now().Add(400 * time.Millisecond)
		for player.Packets() < 10 && time.Now().Before(deadline) {
			time.Sleep(10 * time.Millisecond)
		}
		c.Check(player.Packets() >= 10, "%v RTP packets in 400ms, want 10", player.Packets())
		r := player.Report()
		c.Check(!rtcPlayedUntil.Done(r), "SRTP played as clear RTP: %v", r)
	})

	// EncryptOffPlay plays with encrypt=false: after DTLS, SRS sends RTP in
	// clear, which a raw player plays.
	check.Case(t, "EncryptOffPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcRawPlayer(c, whepURL(srs, "live", stream)+"&encrypt=false", true)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// EncryptOffPublish publishes with encrypt=false: SRS takes clear RTP
	// after DTLS, and a pion player plays it with SRTP.
	check.Case(t, "EncryptOffPublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcRawPublisher(c, whipURL(srs, "live", stream)+"&encrypt=false", true)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// PlaintextPlay plays with dtls=false and encrypt=false: no DTLS, and
	// RTP in clear from the first STUN answer.
	check.Case(t, "PlaintextPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcRawPlayer(c, whepURL(srs, "live", stream)+"&dtls=false&encrypt=false", false)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// PlaintextPublish publishes with dtls=false and encrypt=false, and a
	// pion player plays it with DTLS and SRTP.
	check.Case(t, "PlaintextPublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcRawPublisher(c, whipURL(srs, "live", stream)+"&dtls=false&encrypt=false", false)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})
}

// rtcOffer makes an offer that is only posted, never connected.
func rtcOffer(c *check.C, publish bool, video, audio []string) string {
	c.Helper()
	offer, err := rtc.Offer(publish, video, audio)
	c.Must(err, "offer")
	return offer
}

// rtcAnswered posts an offer that SRS must answer with 201, and deletes its
// session when the case ends, since nothing connects it.
func rtcAnswered(c *check.C, url, offer string) *rtc.Session {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	s, err := rtc.Post(ctx, url, offer, rtcOptions(nil))
	rtcDeleteLater(c, s, err)
	c.Must(err, "post the offer")
	return s
}

// rtcRefused posts an offer that SRS must refuse with status.
func rtcRefused(c *check.C, url, offer string, status int) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	s, err := rtc.Post(ctx, url, offer, rtcOptions(nil))
	rtcDeleteLater(c, s, err)
	var se *rtc.StatusError
	if c.Check(errors.As(err, &se), "post the offer: %v, want status %v", err, status) {
		c.CheckEqual(se.Code, status, fmt.Sprintf("status (body %q)", se.Body))
	}
}

// rtcDeleteLater deletes a session SRS answered when the case ends.
func rtcDeleteLater(c *check.C, s *rtc.Session, err error) {
	if err != nil || s == nil || s.Location == "" {
		return
	}
	c.Cleanup(func() {
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		s.Delete(ctx)
	})
}

// answerCodecs is the codec of each rtpmap of a kind's sections in an
// answer, without the retransmission and error correction formats.
func answerCodecs(answer, kind string) []string {
	var out []string
	for _, m := range strings.Split(answer, "\nm=")[1:] {
		if !strings.HasPrefix(m, kind) {
			continue
		}
		for _, l := range strings.Split(m, "\n") {
			_, v, ok := strings.Cut(strings.TrimSpace(l), "a=rtpmap:")
			if !ok {
				continue
			}
			_, enc, _ := strings.Cut(v, " ")
			name, _, _ := strings.Cut(enc, "/")
			switch strings.ToLower(name) {
			case "rtx", "red", "ulpfec":
			default:
				out = append(out, name)
			}
		}
	}
	return out
}

// answerCandidates is the address of each UDP candidate on SRS's WebRTC
// UDP port in an answer, once each, in order.
func answerCandidates(answer string, srs *server.Server) []string {
	port := fmt.Sprint(srs.Ports[server.RTCUDP])
	var out []string
	seen := map[string]bool{}
	for _, l := range strings.Split(answer, "\n") {
		f := strings.Fields(strings.TrimSpace(l))
		if len(f) < 6 || !strings.HasPrefix(f[0], "a=candidate:") || !strings.EqualFold(f[2], "udp") || f[5] != port {
			continue
		}
		if !seen[f[4]] {
			seen[f[4]] = true
			out = append(out, f[4])
		}
	}
	return out
}

// rtcRawPublisher connects a raw publisher, closed when the case ends.
func rtcRawPublisher(c *check.C, url string, withDTLS bool) *rtc.RawPublisher {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	pub, err := rtc.RawPublish(ctx, url, rtcOptions(nil), withDTLS)
	c.Must(err, "raw WHIP publish")
	c.Cleanup(func() { pub.Close() })
	return pub
}

// rtcRawPlayer connects a raw player, closed when the case ends.
func rtcRawPlayer(c *check.C, url string, withDTLS bool) *rtc.RawPlayer {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	player, err := rtc.RawPlay(ctx, url, rtcOptions(nil), withDTLS)
	c.Must(err, "raw WHEP play")
	c.Cleanup(func() { player.Close() })
	return player
}
