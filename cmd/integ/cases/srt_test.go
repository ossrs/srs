// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// TestSrt_Common checks SRT signaling on the Common server: the streamid in
// its #!:: form, with the resource r, the mode m, the host h and params, and
// in the short form, which SRS takes as a player by default; the defaults for
// a short or missing streamid; and the streamids SRS refuses, which it
// accepts the handshake of and then closes. SRS keeps no GOP for SRT players,
// so a player connects before the stream is published, to get it from its
// first keyframe.
func TestSrt_Common(t *testing.T) {
	t.Parallel()

	// The #!:: form with r and m=publish names the app and stream of the
	// on_publish hook, on the default vhost, with no param; a player of the
	// same r with m=request plays it.
	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		c.CheckEqual(h.Field("param"), "", "param")
		c.CheckEqual(h.Field("stream_url"), "/live/"+stream, "stream_url")
		c.CheckEqual(h.Field("ip"), "127.0.0.1", "ip")
		c.Check(strings.HasPrefix(h.Field("tcUrl"), "srt://") && strings.HasSuffix(h.Field("tcUrl"), "/live"),
			"tcUrl %q, want srt://<host>/live", h.Field("tcUrl"))
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	// The resource may name a multi-level app, like an RTMP path: the last
	// part is the stream.
	check.Case(t, "MultiLevelApp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		resource := "live/a/b/" + stream
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID(resource, "request")}, stream)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID(resource, "publish")}, media.H264HighAACTS)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live/a/b", "app")
		c.CheckEqual(h.Field("stream_url"), "/live/a/b/"+stream, "stream_url")
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	// Keys other than r, m and h are the stream's params.
	check.Case(t, "ParamsInStreamId", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := dialSRT(c, srs, srt.Options{StreamID: "#!::r=live/" + stream + ",m=publish,token=abc,ext=1"})
		defer pub.Close()

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("stream"), stream, "stream")
		checkParams(c, h.Field("param"), "token=abc", "ext=1")
	})

	// A query in the resource, the older way to pass params, is taken as
	// params too, not as part of the stream.
	check.Case(t, "QueryInResource", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := dialSRT(c, srs, srt.Options{StreamID: "#!::r=live/" + stream + "?token=abc,m=publish"})
		defer pub.Close()

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		checkParams(c, h.Field("param"), "token=abc")
	})

	// h names the vhost; an unknown one is the default vhost, so a player
	// that names none plays the stream.
	check.Case(t, "HostIsVhost", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		startSRTPublisher(c, srs, srt.Options{StreamID: "#!::h=example.com,r=live/" + stream + ",m=publish"}, media.H264HighAACTS)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		c.CheckEqual(h.Field("app"), "live", "app")
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	// The older form puts the stream in h, as app/stream, or as
	// vhost/app/stream.
	for name, id := range map[string]string{
		"LegacyHostResource":      "#!::h=live/%s,m=publish",
		"LegacyHostVhostResource": "#!::h=example.com/live/%s,m=publish",
	} {
		check.Case(t, name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
			startSRTPublisher(c, srs, srt.Options{StreamID: strings.Replace(id, "%s", stream, 1)}, media.H264HighAACTS)

			h := waitHook(c, srs, "on_publish", stream)
			c.CheckEqual(h.Field("app"), "live", "app")
			c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
			checkSRTPlayed(c, srtAnalyze(c, play))
		})
	}

	// The mode is not case-sensitive.
	check.Case(t, "ModeUppercase", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+stream, "PUBLISH")})
		defer pub.Close()
		waitHook(c, srs, "on_publish", stream)
	})

	// Without m, the #!:: form is a player.
	check.Case(t, "NoModeIsPlayer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := dialSRT(c, srs, srt.Options{StreamID: "#!::r=live/" + stream})
		defer play.Close()
		h := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 0, "on_publish requests")
	})

	// The short form, without #!::, is app/stream, and a player by default
	// (default_mode request): it plays a stream the #!:: form publishes.
	check.Case(t, "ShortFormPlays", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: "live/" + stream}, stream)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	// A short form with only the stream is in the default app, live
	// (default_app).
	check.Case(t, "ShortFormDefaultApp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := dialSRT(c, srs, srt.Options{StreamID: stream})
		defer play.Close()
		h := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		c.CheckEqual(h.Field("stream_url"), "/live/"+stream, "stream_url")
	})

	// A caller that sends no streamid gets the default streamid,
	// #!::r=live/livestream,m=request (default_streamid): it plays
	// live/livestream.
	check.Case(t, "NoStreamIdUsesDefault", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		since := time.Now()
		play := dialSRT(c, srs, srt.Options{})
		defer play.Close()
		h := waitHookSince(c, srs, mock.Match{Action: "on_play", Stream: "livestream"}, since)
		c.CheckEqual(h.Field("app"), "live", "app")
	})

	// SRS takes the handshake of a streamid it can't use, then closes the
	// connection, before any hook, and serves the next caller.
	for name, id := range map[string]string{
		"BadMode":    "#!::r=live/%s,m=play",
		"NoResource": "#!::m=publish,s=%s",
		"HostOnly":   "#!::h=example.com,m=publish,s=%s",
	} {
		check.Case(t, name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			conn := dialSRTEarlyClose(c, srs, srt.Options{StreamID: strings.Replace(id, "%s", stream, 1)})
			defer conn.Close()
			checkSRTClosedEarly(c, conn)
			c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks for the stream")
			checkSRTServes(c, srs, srt.Options{})
		})
	}

	// Common has no passphrase, so it refuses a caller that has one.
	check.Case(t, "PassphraseNotExpected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		checkSRTRejected(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish"), Passphrase: srtPassphrase}, srt.RejectUnsecure)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks for the stream")
	})

	// ffmpeg with libsrt publishes; the client plays what SRS relays.
	check.Case(t, "FfmpegPublishPlays", check.Normal, func(c *check.C) {
		ff := ffmpegSRT(c)
		srs := server.Common(c)
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		ffmpegPublish(c, ff, media.Path(c, media.H264HighAACTS), "srt://"+srs.Addr(server.SRT)+"?streamid="+srtID("live/"+stream, "publish"))
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	srtMedia(t)
	srtLifecycle(t)
	srtLatency(t)
}

// srtPassphrase is the passphrase of the SRT variant.
const srtPassphrase = "srs-integ-passphrase"

// sharedTsFloorSRT is the SRT setting of the TsFloor server, for TestSrt_Options:
// short streamids publish, in the app srtapp when they name only a stream, a
// caller without a streamid plays srtapp/default, and every caller needs the
// passphrase. The HLS and WebRTC groups on it use no SRT.
var sharedTsFloorSRT = server.SRTOptions{
	DefaultApp:      "srtapp",
	DefaultMode:     "publish",
	DefaultStreamID: "#!::r=srtapp/default,m=request",
	Passphrase:      srtPassphrase,
	PBKeyLen:        16,
}

// TestSrt_Options checks the SRT server settings Common keeps at their
// defaults: default_mode, default_app, default_streamid, and the passphrase
// with its key length.
func TestSrt_Options(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsFloor", sharedTsFloor())

	// with is opts with the passphrase.
	with := func(opts srt.Options) srt.Options {
		opts.Passphrase = srtPassphrase
		return opts
	}

	// With default_mode publish, the short form publishes; a player plays it
	// over the encrypted connections.
	check.Case(t, "ShortFormPublishes", check.Normal, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayer(c, srs, with(srt.Options{StreamID: srtID("live/"+stream, "request")}), stream)
		startSRTPublisher(c, srs, with(srt.Options{StreamID: "live/" + stream}), media.H264HighAACTS)
		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	// A short form with only the stream is in default_app.
	check.Case(t, "ShortFormDefaultApp", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub := dialSRT(c, srs, with(srt.Options{StreamID: stream}))
		defer pub.Close()
		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "srtapp", "app")
	})

	// default_mode applies only to the short form: the #!:: form without m
	// is still a player.
	check.Case(t, "ResourceFormKeepsRequest", check.Normal, func(c *check.C) {
		stream := streamName(c)
		play := dialSRT(c, srs, with(srt.Options{StreamID: "#!::r=live/" + stream}))
		defer play.Close()
		waitHook(c, srs, "on_play", stream)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 0, "on_publish requests")
	})

	// A caller without a streamid gets default_streamid.
	check.Case(t, "DefaultStreamId", check.Normal, func(c *check.C) {
		since := time.Now()
		play := dialSRT(c, srs, with(srt.Options{}))
		defer play.Close()
		h := waitHookSince(c, srs, mock.Match{Action: "on_play", Stream: "default"}, since)
		c.CheckEqual(h.Field("app"), "srtapp", "app")
	})

	// The key length is the caller's: a 32-byte key is accepted by a
	// listener set to 16.
	check.Case(t, "PassphraseKeyLength32", check.Normal, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayer(c, srs, with(srt.Options{StreamID: srtID("live/"+stream, "request"), PBKeyLen: 32}), stream)
		startSRTPublisher(c, srs, with(srt.Options{StreamID: srtID("live/"+stream, "publish"), PBKeyLen: 32}), media.H264HighAACTS)
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	// A wrong passphrase and a missing one are refused in the handshake,
	// with their reasons, before any hook; the server serves the next
	// caller.
	check.Case(t, "PassphraseWrong", check.Normal, func(c *check.C) {
		stream := streamName(c)
		checkSRTRejected(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish"), Passphrase: "a-wrong-passphrase"}, srt.RejectBadSecret)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks for the stream")
		checkSRTServes(c, srs, with(srt.Options{}))
	})
	check.Case(t, "PassphraseMissing", check.Normal, func(c *check.C) {
		stream := streamName(c)
		checkSRTRejected(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, srt.RejectUnsecure)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks for the stream")
		checkSRTServes(c, srs, with(srt.Options{}))
	})
}

// srtID is the #!:: streamid of a resource and a mode.
func srtID(resource, mode string) string {
	return "#!::r=" + resource + ",m=" + mode
}

// srtLead is the media a publisher sends at once, by its PCR: ffmpeg's TS
// muxer sends the audio of a moment after the video, so this much gets a
// player both sequence headers at once.
const srtLead = 500 * time.Millisecond

// srtPlayedUntil is when an SRT player has seen a stream play.
var srtPlayedUntil = report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}

// dialSRT calls the server's SRT listener; the connection is closed when the
// case ends.
func dialSRT(c *check.C, srs *server.Server, opts srt.Options) *srt.Conn {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	conn, err := srt.Dial(ctx, srs.Addr(server.SRT), opts)
	c.Must(err, "dial SRT with streamid "+opts.StreamID)
	c.Cleanup(func() { conn.Close() })
	return conn
}

// srtPlayer connects a player of a stream and waits for its on_play, after
// which SRS makes its consumer before it reads anything else, so the
// player gets a stream published next from its start.
func srtPlayer(c *check.C, srs *server.Server, opts srt.Options, stream string) *srt.Conn {
	c.Helper()
	conn := dialSRT(c, srs, opts)
	waitHook(c, srs, "on_play", stream)
	return conn
}

// startSRTPublisher publishes a TS file until the case ends: srtLead of it
// at once, then the rest in real time.
func startSRTPublisher(c *check.C, srs *server.Server, opts srt.Options, file string) *srt.Conn {
	c.Helper()
	conn, err := trySRTPublisher(c, srs, opts, file)
	c.Must(err, "publish the lead")
	return conn
}

// trySRTPublisher is startSRTPublisher, which returns the error of sending
// the lead, such as when the server closes the publisher.
func trySRTPublisher(c *check.C, srs *server.Server, opts srt.Options, file string) (*srt.Conn, error) {
	c.Helper()
	conn := dialSRT(c, srs, opts)
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	s := srt.NewSender(f)
	if err := s.SendThrough(conn, srtLead); err != nil {
		f.Close()
		return conn, err
	}

	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- s.Stream(ctx, conn, nil, nil) }()
	c.Cleanup(func() {
		cancel()
		conn.Close()
		<-done
		f.Close()
	})
	return conn, nil
}

// srtAnalyze plays what a player gets until srtPlayedUntil, for at most 1s.
func srtAnalyze(c *check.C, play *srt.Conn) *report.Report {
	c.Helper()
	return srtAnalyzeUntil(c, play, srtPlayedUntil, time.Second)
}

// srtAnalyzeUntil plays what a player gets until u, for at most d.
func srtAnalyzeUntil(c *check.C, play *srt.Conn, u report.Until, d time.Duration) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	r, err := play.Analyze(ctx, u)
	c.Must(err, "play")
	return r
}

// checkSRTPlayed checks the report of an SRT player of the TS test file, like
// checkPlayed, but with ffmpeg's TS muxer, which puts the audio up to about
// 200ms away from the video of the same time.
func checkSRTPlayed(c *check.C, r *report.Report) {
	c.Helper()
	checkSRTPlayedAs(c, r, "h264", "aac")
}

// checkSRTPlayedAs is checkSRTPlayed for a test file of other codecs. Opus
// is 48 kHz stereo, and TS carries no sequence header for it.
func checkSRTPlayedAs(c *check.C, r *report.Report, vcodec, acodec string) {
	c.Helper()
	if !c.Check(r.Video != nil && r.Audio != nil, "no video or audio: %v", r) {
		return
	}
	checkVideo(c, r, vcodec)
	if acodec == "opus" {
		a := r.Audio
		c.CheckEqual(a.Codec, acodec, "audio codec")
		c.Check(a.SampleRate == 48000 && a.Channels == 2, "audio %vHz %vch, want 48000Hz 2ch", a.SampleRate, a.Channels)
		c.CheckEqual(a.SequenceHeaders, 0, "audio sequence headers")
	} else {
		checkAudio(c, r, acodec, 1)
	}
	c.CheckEqual(r.Video.LeadingFrames, 0, "video frames before the first keyframe")
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	c.Check(r.AVSync <= 400*time.Millisecond, "A/V sync %v, want at most 400ms", r.AVSync)
}

// checkParams checks that a param, a=1&b=2, has each of want, in any order.
func checkParams(c *check.C, param string, want ...string) {
	c.Helper()
	got := map[string]bool{}
	for _, kv := range strings.Split(strings.TrimPrefix(param, "?"), "&") {
		got[kv] = true
	}
	for _, kv := range want {
		c.Check(got[kv], "param %q has no %v", param, kv)
	}
}

// waitHookSince waits for a hook that matches m and arrived after since, for
// cases whose stream name is fixed by the server, not by the case.
func waitHookSince(c *check.C, srs *server.Server, m mock.Match, since time.Time) mock.Request {
	c.Helper()
	c.Must(checkHooks(srs), "hooks")
	m.Func = func(r mock.Request) bool { return !r.Time.Before(since) }
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	h, err := srs.Hooks.Wait(ctx, m)
	c.Must(err, m.Action)
	return h
}

// checkSRTClosed checks that the server closes a connection within 400ms,
// which leaves the case time to check the server goes on.
func checkSRTClosed(c *check.C, conn *srt.Conn) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 400*time.Millisecond)
	defer cancel()
	c.CheckNoError(conn.WaitClosed(ctx), "the server closes the connection")
}

// srtProbeIdle is the idle timeout of a caller the server closes right after
// the handshake: gosrt drops a shutdown that comes while its Dial returns,
// so such a caller probes the server, and takes its silence for the close.
const srtProbeIdle = 250 * time.Millisecond

// dialSRTEarlyClose dials a caller the server should close right after the
// handshake, for checkSRTClosedEarly.
func dialSRTEarlyClose(c *check.C, srs *server.Server, opts srt.Options) *srt.Conn {
	c.Helper()
	opts.PeerIdleTimeout = srtProbeIdle
	return dialSRT(c, srs, opts)
}

// checkSRTClosedEarly checks that the server closes a connection of
// dialSRTEarlyClose within 400ms: it sends a shutdown, or, if the client
// missed that, it answers no probe.
func checkSRTClosedEarly(c *check.C, conn *srt.Conn) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 400*time.Millisecond)
	defer cancel()
	c.CheckNoError(conn.WaitClosedProbing(ctx, 50*time.Millisecond), "the server closes the connection")
}

// checkSRTRejected checks that the server refuses a handshake for a reason.
func checkSRTRejected(c *check.C, srs *server.Server, opts srt.Options, reason int) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	conn, err := srt.Dial(ctx, srs.Addr(server.SRT), opts)
	if conn != nil {
		conn.Close()
	}
	var rej *srt.RejectError
	if c.Check(errors.As(err, &rej), "dial: %v, want the handshake rejected", err) {
		c.CheckEqual(rej.Reason, reason, "reject reason")
	}
}

// checkSRTServes checks that a new caller publishes after a bad one.
func checkSRTServes(c *check.C, srs *server.Server, opts srt.Options) {
	c.Helper()
	stream := streamName(c)
	opts.StreamID = srtID("live/"+stream, "publish")
	pub := dialSRT(c, srs, opts)
	defer pub.Close()
	waitHook(c, srs, "on_publish", stream)
}

var ffmpegSRTOnce = sync.OnceValues(func() (string, error) {
	ff, err := media.FindFFmpeg()
	if err != nil {
		return "", err
	}
	if !ff.HasProtocol("srt") {
		return "", errors.New("ffmpeg " + ff.Path + " lacks libsrt")
	}
	return ff.Path, nil
})

// ffmpegSRT returns the path of an ffmpeg with libsrt, or skips the case.
func ffmpegSRT(c *check.C) string {
	c.Helper()
	path, err := ffmpegSRTOnce()
	if err != nil {
		c.Skip(err.Error())
	}
	return path
}

// ffmpegPublish runs ffmpeg -re to publish a TS file to an SRT URL until the
// case ends, with its first 500ms at once, like srtLead.
func ffmpegPublish(c *check.C, ff, file, url string) {
	c.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	cmd := exec.CommandContext(ctx, ff, "-hide_banner", "-loglevel", "error", "-nostdin",
		"-re", "-readrate_initial_burst", "0.5", "-i", file, "-c", "copy", "-f", "mpegts", url)
	c.Must(cmd.Start(), "start ffmpeg")
	c.Cleanup(func() {
		cancel()
		cmd.Wait()
	})
}
