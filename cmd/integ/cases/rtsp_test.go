// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"fmt"
	"os"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/bluenviron/gortsplib/v5/pkg/base"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/rtsp"
	"srs-integ/internal/server"
)

// TestRtsp_Common checks RTSP play on the Common server, where every RTMP
// stream plays over RTSP (rtmp_to_rtsp): DESCRIBE, SETUP over TCP, PLAY and
// TEARDOWN and their error replies, the codecs, many players, the server's
// own port, audio-only and video-only streams, and multi-level URLs. SRS
// plays RTSP only over TCP (interleaved), keeps no GOP for RTSP players, and
// answers DESCRIBE only once a publisher has sent the sequence headers that
// make the stream's tracks. So a case publishes the headers and holds the
// stream, plays, then publishes the media from its first keyframe.
func TestRtsp_Common(t *testing.T) {
	t.Parallel()

	// OPTIONS lists the methods SRS serves, with the request's CSeq.
	check.Case(t, "Options", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rtspDial(c, srs)
		res, err := conn.Do(base.Options, rtspURL(srs, "live/"+streamName(c)), nil, 300*time.Millisecond)
		c.Must(err, "OPTIONS")
		c.CheckEqual(res.StatusCode, base.StatusOK, "status")
		c.CheckEqual(res.Header["CSeq"], base.HeaderValue{"1"}, "CSeq")
		c.CheckEqual(res.Header["Public"], base.HeaderValue{"DESCRIBE, OPTIONS, PLAY, SETUP, TEARDOWN"}, "Public")
	})

	// DESCRIBE answers the stream's SDP, an audio and a video track, each
	// with its control URL under the request's, port 0 (no UDP) and its
	// codec; the AAC config is in the fmtp. The on_play hook comes with it.
	check.Case(t, "DescribeAnswer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		url := rtspURL(srs, "live/"+stream)
		res := rtspDescribe(c, rtspDial(c, srs), url)
		c.CheckEqual(res.Header["Content-Type"], base.HeaderValue{"application/sdp"}, "Content-Type")
		c.Check(len(res.Header["Session"]) == 1 && res.Header["Session"][0] != "", "Session %q, want a session id", res.Header["Session"])
		sdp := string(res.Body)
		for _, line := range []string{
			"m=audio 0 RTP/AVP 111",
			"a=rtpmap:111 MPEG4-GENERIC/44100/2",
			"a=control:" + url + "/trackID=0",
			"m=video 0 RTP/AVP 102",
			"a=rtpmap:102 H264/90000",
			"a=control:" + url + "/trackID=1",
		} {
			c.Check(strings.Contains(sdp, line+"\r\n"), "SDP has no %q:\n%s", line, sdp)
		}
		c.Check(regexp.MustCompile(`a=fmtp:111 .*mode=AAC-hbr;sizelength=13;indexlength=3;config=1210`).MatchString(sdp), "SDP has no AAC fmtp:\n%s", sdp)

		h := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		c.CheckEqual(h.Field("param"), "", "param")
		c.CheckEqual(h.Field("stream_url"), "/live/"+stream, "stream_url")
		c.CheckEqual(h.Field("ip"), "127.0.0.1", "ip")
		c.CheckEqual(h.Field("tcUrl"), "rtsp://"+srs.Addr(server.RTSP)+"/live", "tcUrl")
	})

	// A player gets H.264 and AAC over TCP from the first keyframe.
	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		r, p := rtspPublishPlay(c, server.Common(c), media.H264BaselineAAC)
		c.Check(p.TCP(), "the player plays over UDP, want TCP")
		checkRTSPPlayed(c, r, "h264")
	})

	// HEVC from enhanced RTMP plays as H265 RTP.
	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		r, p := rtspPublishPlay(c, server.Common(c), media.HEVCAAC)
		c.Check(strings.Contains(string(p.Describe.Body), " H265/90000\r\n"), "SDP has no H265 track:\n%s", p.Describe.Body)
		checkRTSPPlayed(c, r, "hevc")
	})

	// An audio-only stream has only an audio track.
	check.Case(t, "AudioOnly", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		r, p := rtspPublishPlayUntil(c, srs, media.AACOnly, report.Until{AudioHeader: true, AudioFrames: 10})
		c.CheckEqual(rtspMedias(p), "audio", "medias")
		c.Check(r.Video == nil, "video %v, want none", r.Video)
		checkAudio(c, r, "aac", 1)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	// A video-only stream has only a video track.
	check.Case(t, "VideoOnly", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		r, p := rtspPublishPlayUntil(c, srs, media.H264Only, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		c.CheckEqual(rtspMedias(p), "video", "medias")
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
		checkVideo(c, r, "h264")
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	// RTSP carries AAC, not MP3: an H.264/MP3 stream has only its video
	// track, which plays.
	check.Case(t, "Mp3AudioDropped", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		r, p := rtspPublishPlayUntil(c, srs, media.H264HighMP3, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		c.CheckEqual(rtspMedias(p), "video", "medias")
		checkVideo(c, r, "h264")
	})

	// A multi-level path is app/stream like RTMP: the last part is the stream.
	check.Case(t, "MultiLevelApp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/a/b/"+stream, media.H264BaselineAAC)
		p := rtspPlay(c, srs, "live/a/b/"+stream, nil)
		start()
		h := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(h.Field("app"), "live/a/b", "app")
		c.CheckEqual(h.Field("stream_url"), "/live/a/b/"+stream, "stream_url")
		checkRTSPPlayed(c, rtspUntil(c, p, rtspPlayedUntil), "h264")
	})

	// A query is the stream's param, in on_play; the tracks under the URL
	// with it still set up and play.
	check.Case(t, "ParamInUrl", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		p := rtspPlay(c, srs, "live/"+stream+"?token=abc", nil)
		start()
		h := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(h.Field("stream"), stream, "stream")
		c.CheckEqual(h.Field("param"), "?token=abc", "param")
		checkRTSPPlayed(c, rtspUntil(c, p, rtspPlayedUntil), "h264")
	})

	// The control URLs carry the host and port of the request, not SRS's
	// default 554, so a player that reached SRS by a name and a port of its
	// own sets up the tracks there.
	check.Case(t, "CustomPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		host := "localhost:" + rtspPort(srs)
		p := rtspPlayURL(c, "rtsp://"+host+"/live/"+stream, nil)
		c.Check(strings.Contains(string(p.Describe.Body), "a=control:rtsp://"+host+"/live/"+stream+"/trackID=1\r\n"),
			"SDP has no control URL on %v:\n%s", host, p.Describe.Body)
		start()
		checkRTSPPlayed(c, rtspUntil(c, p, rtspPlayedUntil), "h264")
	})

	rtspLifecycle(t)
	rtspBadInput(t)
}

// rtspLifecycle adds the lifecycle cases of the protocol checklist to
// TestRtsp_Common.
func rtspLifecycle(t *testing.T) {
	// Several players at once each get the stream.
	check.Case(t, "ManyPlayers", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		players := make([]*rtsp.Player, 4)
		var wg sync.WaitGroup
		for i := range players {
			wg.Add(1)
			go func() {
				defer wg.Done()
				ctx, cancel := context.WithTimeout(context.Background(), time.Second)
				defer cancel()
				players[i], _ = rtsp.Play(ctx, rtspURL(srs, "live/"+stream), nil)
			}()
		}
		wg.Wait()
		for _, p := range players {
			if p != nil {
				c.Cleanup(p.Close)
			}
		}
		for i, p := range players {
			c.Must(errorIf(p == nil, "player %d did not play", i), "play")
		}
		start()
		for _, p := range players {
			checkRTSPPlayed(c, rtspUntil(c, p, rtspPlayedUntil), "h264")
		}
	})

	// SRS describes a stream only once it is published: a player that asks
	// before gets 404, and plays once the publisher has come.
	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		conn := rtspDial(c, srs)
		res, err := conn.Do(base.Describe, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		c.Must(err, "DESCRIBE")
		c.CheckEqual(res.StatusCode, base.StatusNotFound, "DESCRIBE before the publish")
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		p := rtspPlay(c, srs, "live/"+stream, nil)
		start()
		checkRTSPPlayed(c, rtspUntil(c, p, rtspPlayedUntil), "h264")
	})

	// When the publisher leaves, a player gets no more media; SRS keeps its
	// connection, which still answers. What SRS sent before on_unpublish may
	// still be on its way, then nothing comes.
	check.Case(t, "UnpublishStopsMedia", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		conn := rtspRawPlay(c, srs, "live/"+stream)
		pub := start()
		n, err := conn.WaitFrames(1, 300*time.Millisecond)
		c.Check(err == nil && n == 1, "frames %v, %v; want media", n, err)
		pub.Close()
		waitHook(c, srs, "on_unpublish", stream)
		for deadline := time.Now().Add(300 * time.Millisecond); ; {
			n, err := conn.ReadFrames(20 * time.Millisecond)
			c.Must(err, "read after the unpublish")
			if n == 0 {
				break
			}
			c.Must(errorIf(time.Now().After(deadline), "media goes on 300ms after the unpublish"), "read")
		}
		n, err = conn.ReadFrames(100 * time.Millisecond)
		c.Check(err == nil && n == 0, "frames after the unpublish %v, %v; want none", n, err)
		res, err := conn.Do(base.Options, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		c.Check(err == nil && res.StatusCode == base.StatusOK, "OPTIONS after the unpublish: %v, %v", res, err)
	})

	// A player that stays plays the stream when it is published again, from
	// its first keyframe, as the WebRTC and HTTP players do. Each publish
	// sends its first 120ms at once and holds the stream.
	check.Case(t, "RepublishPlays", check.Normal, func(c *check.C) {
		check.KnownBug(c, "an RTSP player that stays across a republish gets nothing of the new publish: SRS keys it to the old publisher's SSRCs")
		srs := server.Common(c)
		stream := streamName(c)
		url := rtmpURL(srs, "live/"+stream)
		tags := readTags(c, media.H264BaselineAAC, 0, 120*time.Millisecond)
		headers := 0
		for tags[headers].Type == media.TagScript || isHeader(tags[headers]) {
			headers++
		}
		pub := publishTags(c, url, tags[:headers])
		p := rtspPlay(c, srs, "live/"+stream, nil)
		c.Must(writeTags(pub, tags[headers:]), "publish the media")
		rtspUntil(c, p, report.Until{VideoHeader: true, Keyframes: 1})
		pub.Close()

		// The new publish is accepted once SRS has let the old one go; an
		// RTMP player that gets its keyframe shows SRS has it, and the RTSP
		// player got it at the same time.
		republishTags(c, url, tags, 300*time.Millisecond)
		playUntil(c, url, report.Until{VideoHeader: true, Keyframes: 1})
		ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
		defer cancel()
		_, err := p.Until(ctx, report.Until{Keyframes: 2})
		c.CheckNoError(err, "the player gets the new publish")
	})

	// TEARDOWN stops the media and the player leaves the API; the connection
	// stays and answers.
	check.Case(t, "TeardownStops", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		conn := rtspRawPlay(c, srs, "live/"+stream)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		start()
		n, err := conn.WaitFrames(1, 300*time.Millisecond)
		c.Check(err == nil && n == 1, "frames %v, %v; want media", n, err)
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+id), 0, "the player in the API")

		res, err := conn.Do(base.Teardown, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		c.Must(err, "TEARDOWN")
		c.CheckEqual(res.StatusCode, base.StatusOK, "TEARDOWN")
		n, err = conn.ReadFrames(100 * time.Millisecond)
		c.Check(err == nil && n == 0, "frames after TEARDOWN %v, %v; want none", n, err)
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
		res, err = conn.Do(base.Options, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		c.Check(err == nil && res.StatusCode == base.StatusOK, "OPTIONS after TEARDOWN: %v, %v", res, err)
	})

	// A player that closes its connection, without TEARDOWN, leaves the API
	// and the stream's clients.
	check.Case(t, "PlayerCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		conn := rtspRawPlay(c, srs, "live/"+stream)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		start()
		n, err := conn.WaitFrames(1, 300*time.Millisecond)
		c.Check(err == nil && n == 1, "frames %v, %v; want media", n, err)
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+id), 0, "the player in the API")
		conn.Close()
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
	})

	// A player that on_play refuses gets 401 to DESCRIBE.
	check.Case(t, "OnPlayRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		srs.Hooks.Script(mock.Match{Action: "on_play", Stream: stream}, mock.Reject(403))
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		p, err := rtsp.Play(ctx, rtspURL(srs, "live/"+stream), nil)
		if p != nil {
			p.Close()
		}
		var se *rtsp.StatusError
		if c.Check(errors.As(err, &se), "play: %v, want a refusal", err) {
			c.CheckEqual(se.Method, "DESCRIBE", "method")
			c.CheckEqual(se.Code, 401, "status")
		}
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_play", Stream: stream})), 1, "on_play requests")
	})

	// A player that asks for UDP first, as most do, gets 461 and plays over
	// TCP.
	check.Case(t, "UdpFallsBackToTcp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		p := rtspPlay(c, srs, "live/"+stream, &rtsp.Options{TryUDP: true})
		c.Check(p.TCP(), "the player plays over UDP, want TCP")
		start()
		checkRTSPPlayed(c, rtspUntil(c, p, rtspPlayedUntil), "h264")
	})
}

// rtspBadInput adds the bad-input cases to TestRtsp_Common: each failing
// request gets its error reply, or SRS closes the connection, and the server
// serves the next client.
func rtspBadInput(t *testing.T) {
	// SETUP over UDP is refused with 461: SRS plays only over TCP.
	check.Case(t, "SetupUdpRefused", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		conn := rtspDial(c, srs)
		url := rtspURL(srs, "live/"+stream)
		rtspDescribe(c, conn, url)
		res, err := conn.Do(base.Setup, url+"/trackID=0", base.Header{"Transport": base.HeaderValue{"RTP/AVP;unicast;client_port=5000-5001"}}, 300*time.Millisecond)
		c.Must(err, "SETUP")
		c.CheckEqual(res.StatusCode, base.StatusUnsupportedTransport, "SETUP over UDP")
	})

	// SETUP without a Transport header is 400.
	check.Case(t, "SetupWithoutTransport", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		conn := rtspDial(c, srs)
		url := rtspURL(srs, "live/"+stream)
		rtspDescribe(c, conn, url)
		res, err := conn.Do(base.Setup, url+"/trackID=0", nil, 300*time.Millisecond)
		c.Must(err, "SETUP")
		c.CheckEqual(res.StatusCode, base.StatusBadRequest, "SETUP without Transport")
		checkRTSPServes(c, srs)
	})

	// SETUP of a track the stream doesn't have, or before any DESCRIBE, is
	// refused with an error status.
	check.Case(t, "SetupUnknownTrack", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		url := rtspURL(srs, "live/"+stream)
		conn := rtspDial(c, srs)
		rtspDescribe(c, conn, url)
		res, err := conn.Do(base.Setup, url+"/trackID=9", rtspTCPTransport(0), 300*time.Millisecond)
		c.Must(err, "SETUP")
		c.Check(res.StatusCode >= 400, "SETUP of track 9: status %v, want an error", res.StatusCode)
	})
	check.Case(t, "SetupBeforeDescribe", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		res, err := rtspDial(c, srs).Do(base.Setup, rtspURL(srs, "live/"+stream)+"/trackID=0", rtspTCPTransport(0), 300*time.Millisecond)
		c.Must(err, "SETUP")
		c.Check(res.StatusCode >= 400, "SETUP before DESCRIBE: status %v, want an error", res.StatusCode)
	})

	// PLAY before any track is set up is not valid in that state (RFC 2326,
	// 455), and must not make a player.
	check.Case(t, "PlayWithoutSetup", check.Normal, func(c *check.C) {
		check.KnownBug(c, "PLAY without SETUP is answered 200, not 455, and the API lists a player")
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		url := rtspURL(srs, "live/"+stream)
		conn := rtspDial(c, srs)
		rtspDescribe(c, conn, url)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		res, err := conn.Do(base.Play, url, nil, 300*time.Millisecond)
		c.Must(err, "PLAY")
		c.Check(res.StatusCode >= 400, "PLAY without SETUP: status %v, want an error", res.StatusCode)
		c.Check(apiCode(c, srs, "/api/v1/clients/"+id) != 0, "the connection is a player in the API")
	})

	// Every request gets an answer, 501 or 405 for a method the server
	// doesn't serve, not silence: a client waits for it.
	check.Case(t, "UnsupportedMethodAnswered", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a request with a method SRS doesn't serve, such as GET_PARAMETER or PAUSE, gets no answer")
		srs := server.Common(c)
		url := rtspURL(srs, "live/"+streamName(c))
		methods := []base.Method{base.GetParameter, base.Pause, base.Record, base.SetParameter}
		errs := make([]error, len(methods))
		var wg sync.WaitGroup
		for i, m := range methods {
			conn := rtspDial(c, srs)
			wg.Add(1)
			go func() {
				defer wg.Done()
				_, errs[i] = conn.Do(m, url, nil, 200*time.Millisecond)
			}()
		}
		wg.Wait()
		for i, m := range methods {
			c.CheckNoError(errs[i], "an answer to "+string(m))
		}
	})

	// A request SRS can't parse ends the connection: SRS closes it, and
	// serves the next client.
	check.Case(t, "MalformedRequestClosed", check.Normal, func(c *check.C) {
		check.KnownBug(c, "SRS ends an RTSP session on a request it can't parse, but never closes the connection: the client waits forever")
		srs := server.Common(c)
		conn := rtspDial(c, srs)
		c.Must(conn.Write([]byte("hello\r\n\r\n")), "write")
		checkRTSPServes(c, srs)
		c.CheckNoError(conn.WaitClosed(200*time.Millisecond), "the server closes the connection")
	})

	// A client that closes in the middle of a request leaves the server
	// serving.
	check.Case(t, "CloseMidRequest", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rtspDial(c, srs)
		c.Must(conn.Write([]byte("DESCRIBE "+rtspURL(srs, "live/"+streamName(c))+" RTSP/1.0\r\nCSeq: 1\r\nAcc")), "write")
		conn.Close()
		checkRTSPServes(c, srs)
	})

	// An interleaved frame from the client, such as an RTCP receiver report,
	// is skipped: the next request is answered.
	check.Case(t, "InterleavedFrameSkipped", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rtspDial(c, srs)
		// A receiver report of SSRC 1 with no blocks, on channel 1.
		rr := []byte{'$', 1, 0, 8, 0x80, 201, 0, 1, 0, 0, 0, 1}
		c.Must(conn.Write(append(rr, rr...)), "write")
		res, err := conn.Do(base.Options, rtspURL(srs, "live/"+streamName(c)), nil, 300*time.Millisecond)
		c.Must(err, "OPTIONS")
		c.CheckEqual(res.StatusCode, base.StatusOK, "OPTIONS after the frames")
	})
}

// TestRtsp_Options checks rtmp_to_rtsp off, on the shared RemuxOff server:
// an RTMP stream has no RTSP tracks, so DESCRIBE is 404, while it plays over
// RTMP.
func TestRtsp_Options(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	check.Case(t, "RtmpToRtspOff", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishTags(c, rtmpURL(srs, "live/"+stream), readTags(c, media.H264BaselineAAC, 0, 100*time.Millisecond))
		playUntil(c, rtmpURL(srs, "live/"+stream), report.Until{VideoHeader: true, Keyframes: 1})
		res, err := rtspDial(c, srs).Do(base.Describe, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		c.Must(err, "DESCRIBE")
		c.CheckEqual(res.StatusCode, base.StatusNotFound, "DESCRIBE")
	})
}

// TestRtsp_VhostOff checks the RTSP listener with RTSP off on the vhost, on
// the shared Fmp4Keys server: it answers, but has no stream to describe.
func TestRtsp_VhostOff(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())

	check.Case(t, "DescribeNotFound", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishTags(c, rtmpURL(srs, "live/"+stream), readTags(c, media.H264BaselineAAC, 0, 100*time.Millisecond))
		playUntil(c, rtmpURL(srs, "live/"+stream), report.Until{VideoHeader: true, Keyframes: 1})
		conn := rtspDial(c, srs)
		res, err := conn.Do(base.Options, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		c.Must(err, "OPTIONS")
		c.CheckEqual(res.StatusCode, base.StatusOK, "OPTIONS")
		res, err = conn.Do(base.Describe, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		c.Must(err, "DESCRIBE")
		c.CheckEqual(res.StatusCode, base.StatusNotFound, "DESCRIBE")
	})
}

// rtspLead is the media a publisher sends at once, after the player is in.
const rtspLead = 200 * time.Millisecond

// rtspPlayedUntil is when an RTSP player has seen a stream play.
var rtspPlayedUntil = report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}

// rtspURL is the RTSP URL of a stream path on a server.
func rtspURL(srs *server.Server, path string) string {
	return "rtsp://" + srs.Addr(server.RTSP) + "/" + path
}

// rtspPort is a server's RTSP port.
func rtspPort(srs *server.Server) string {
	addr := srs.Addr(server.RTSP)
	return addr[strings.LastIndex(addr, ":")+1:]
}

// rtspPublisher publishes a file's metadata and sequence headers over RTMP,
// which give SRS the stream's RTSP tracks, and holds the stream. start
// publishes the rest, from the first keyframe: rtspLead of it at once, then
// real time, until the case ends; it returns the publisher.
func rtspPublisher(c *check.C, srs *server.Server, path, file string) (start func() *rtmp.Client) {
	c.Helper()
	pub := publishOnly(c, rtmpURL(srs, path), rtmp.FMLE)
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	c.Cleanup(func() { f.Close() })
	r, err := media.NewFLVReader(f)
	c.Must(err, "read the media")
	var first *media.FLVTag
	for first == nil {
		tag, err := r.ReadTag()
		c.Must(err, "read the media")
		if tag.Type == media.TagScript || isHeader(tag) {
			c.Must(pub.WriteTag(tag), "publish the headers")
		} else {
			first = tag
		}
	}
	return func() *rtmp.Client {
		c.Helper()
		tag := first
		for tag != nil && time.Duration(tag.Timestamp)*time.Millisecond < rtspLead {
			c.Must(pub.WriteTag(tag), "publish the lead")
			tag, err = r.ReadTag()
			c.Must(err, "read the media")
		}
		ctx, cancel := context.WithCancel(context.Background())
		done := make(chan error, 1)
		go func() {
			p := &rtmp.Pacer{}
			if err := p.Wait(ctx, tag.Timestamp); err != nil {
				done <- err
				return
			}
			if err := pub.WriteTag(tag); err != nil {
				done <- err
				return
			}
			_, err := rtmp.PublishFLV(ctx, pub, r, p)
			done <- err
		}()
		c.Cleanup(func() {
			cancel()
			pub.Close()
			<-done
		})
		return pub
	}
}

// rtspPlay plays a stream path over RTSP until the case ends.
func rtspPlay(c *check.C, srs *server.Server, path string, opts *rtsp.Options) *rtsp.Player {
	c.Helper()
	return rtspPlayURL(c, rtspURL(srs, path), opts)
}

// rtspPlayURL plays an RTSP URL until the case ends. SRS has the stream's
// tracks once it has read the publisher's sequence headers, so a DESCRIBE
// that comes first, and gets 404, is sent again, for at most 300ms, as a
// player that waits for a stream does.
func rtspPlayURL(c *check.C, url string, opts *rtsp.Options) *rtsp.Player {
	c.Helper()
	deadline := time.Now().Add(300 * time.Millisecond)
	for {
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		p, err := rtsp.Play(ctx, url, opts)
		cancel()
		var se *rtsp.StatusError
		if errors.As(err, &se) && se.Code == 404 && time.Now().Before(deadline) {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		c.Must(err, "play "+url)
		c.Cleanup(p.Close)
		return p
	}
}

// rtspUntil waits until a player's report meets u, for at most 1s.
func rtspUntil(c *check.C, p *rtsp.Player, u report.Until) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	r, err := p.Until(ctx, u)
	c.Must(err, "play")
	return r
}

// rtspPublishPlay publishes a file and plays it over RTSP until
// rtspPlayedUntil.
func rtspPublishPlay(c *check.C, srs *server.Server, file string) (*report.Report, *rtsp.Player) {
	c.Helper()
	return rtspPublishPlayUntil(c, srs, file, rtspPlayedUntil)
}

// rtspPublishPlayUntil publishes a file and plays it over RTSP until u.
func rtspPublishPlayUntil(c *check.C, srs *server.Server, file string, u report.Until) (*report.Report, *rtsp.Player) {
	c.Helper()
	path := "live/" + streamName(c)
	start := rtspPublisher(c, srs, path, file)
	p := rtspPlay(c, srs, path, nil)
	start()
	return rtspUntil(c, p, u), p
}

// rtspMedias is the media types of a player's session, in order.
func rtspMedias(p *rtsp.Player) string {
	var types []string
	for _, m := range p.Desc.Medias {
		types = append(types, string(m.Type))
	}
	return strings.Join(types, " ")
}

// checkRTSPPlayed checks the report of an RTSP player of a test file: like
// checkPlayed, with AAC and nothing before the first keyframe.
func checkRTSPPlayed(c *check.C, r *report.Report, vcodec string) {
	c.Helper()
	checkPlayed(c, r, vcodec, "aac")
	if r.Video != nil {
		c.CheckEqual(r.Video.LeadingFrames, 0, "video frames before the first keyframe")
	}
}

// rtspDial opens a raw RTSP connection, closed when the case ends.
func rtspDial(c *check.C, srs *server.Server) *rtsp.Conn {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	conn, err := rtsp.Dial(ctx, srs.Addr(server.RTSP))
	c.Must(err, "dial RTSP")
	c.Cleanup(func() { conn.Close() })
	return conn
}

// rtspDescribe sends DESCRIBE on a raw connection until it is 200, for at
// most 300ms, like rtspPlayURL.
func rtspDescribe(c *check.C, conn *rtsp.Conn, url string) *base.Response {
	c.Helper()
	deadline := time.Now().Add(300 * time.Millisecond)
	for {
		res, err := conn.Do(base.Describe, url, nil, 300*time.Millisecond)
		c.Must(err, "DESCRIBE")
		if res.StatusCode == base.StatusNotFound && time.Now().Before(deadline) {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		c.Must(errorIf(res.StatusCode != base.StatusOK, "status %v", res.StatusCode), "DESCRIBE")
		return res
	}
}

var rtspControl = regexp.MustCompile(`(?m)^a=control:(\S+/trackID=\d+)\r?$`)

// rtspTCPTransport is the Transport header of a track on the interleaved
// channels ch and ch+1.
func rtspTCPTransport(ch int) base.Header {
	return base.Header{"Transport": base.HeaderValue{"RTP/AVP/TCP;unicast;interleaved=" + strconv.Itoa(ch) + "-" + strconv.Itoa(ch+1)}}
}

// rtspRawPlay plays a stream path on a raw connection: DESCRIBE, SETUP of
// each track over TCP, then PLAY. The connection is closed when the case ends.
func rtspRawPlay(c *check.C, srs *server.Server, path string) *rtsp.Conn {
	c.Helper()
	conn := rtspDial(c, srs)
	url := rtspURL(srs, path)
	res := rtspDescribe(c, conn, url)
	for i, m := range rtspControl.FindAllStringSubmatch(string(res.Body), -1) {
		res, err := conn.Do(base.Setup, m[1], rtspTCPTransport(2*i), 300*time.Millisecond)
		c.Must(err, "SETUP")
		c.Must(errorIf(res.StatusCode != base.StatusOK, "status %v", res.StatusCode), "SETUP "+m[1])
	}
	res, err := conn.Do(base.Play, url, nil, 300*time.Millisecond)
	c.Must(err, "PLAY")
	c.Must(errorIf(res.StatusCode != base.StatusOK, "status %v", res.StatusCode), "PLAY")
	return conn
}

// checkRTSPServes checks a new connection gets an answer to OPTIONS.
func checkRTSPServes(c *check.C, srs *server.Server) {
	c.Helper()
	res, err := rtspDial(c, srs).Do(base.Options, rtspURL(srs, "live/"+streamName(c)), nil, 300*time.Millisecond)
	c.Must(err, "OPTIONS on a new connection")
	c.CheckEqual(res.StatusCode, base.StatusOK, "OPTIONS on a new connection")
}

// errorIf is an error with the message when cond holds, and nil otherwise.
func errorIf(cond bool, format string, args ...any) error {
	if !cond {
		return nil
	}
	return fmt.Errorf(format, args...)
}
