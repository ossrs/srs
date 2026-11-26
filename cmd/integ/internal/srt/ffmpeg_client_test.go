// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// These tests check the FFmpeg client: an ffmpeg with libsrt as the caller,
// against the pure-Go listeners.

// needFFmpeg skips a test of the FFmpeg client when there is no ffmpeg with
// libsrt, as the other ffmpeg tests do.
func needFFmpeg(t *testing.T) {
	t.Helper()
	if _, err := FFmpegPath(); err != nil {
		t.Skip(err)
	}
}

// TestFFmpegURL checks that every option ffmpeg has becomes its URL option,
// in its unit, the stream ID and passphrase options of the command line as
// they are, and an unset one none.
func TestFFmpegURL(t *testing.T) {
	on, off := true, false
	got, args := ffmpegURL("127.0.0.1:10080", Options{
		StreamID: "#!::r=live/s,m=publish", Passphrase: "a long&secret", PBKeyLen: 32,
		Latency: 200 * time.Millisecond, RecvLatency: 300 * time.Millisecond,
		PayloadSize: 1316, MSS: 1400, MaxBW: 1000000, SendBuf: 2000000, RecvBuf: 3000000,
		TSBPDMode: &on, TLPktDrop: &off,
	}, 1500*time.Millisecond)
	want := "srt://127.0.0.1:10080?mode=caller&transtype=live&connect_timeout=1500&pbkeylen=32" +
		"&latency=200000&rcvlatency=300000&peerlatency=200000&mss=1400&payload_size=1316" +
		"&maxbw=1000000&sndbuf=2000000&rcvbuf=3000000&tsbpd=1&tlpktdrop=0"
	if got != want {
		t.Errorf("url\n%v\nwant\n%v", got, want)
	}
	if got, want := fmt.Sprintf("%q", args), `["-srt_streamid" "#!::r=live/s,m=publish" "-passphrase" "a long&secret"]`; got != want {
		t.Errorf("args %v, want %v", got, want)
	}
	got, args = ffmpegURL("[::1]:9000", Options{}, time.Second)
	if want := "srt://[::1]:9000?mode=caller&transtype=live&connect_timeout=1000"; got != want || len(args) != 0 {
		t.Errorf("url of no options %q %q, want %q and no args", got, args, want)
	}
}

// TestFFmpegPublishes checks which stream IDs make the FFmpeg client publish:
// m=publish in any case, in any place; anything else plays, as SRS's default
// mode does.
func TestFFmpegPublishes(t *testing.T) {
	for id, want := range map[string]bool{
		"#!::r=live/s,m=publish":  true,
		"#!::m=PUBLISH,r=live/s":  true,
		"live/s?m=publish":        true,
		"#!::r=live/s,m=request":  false,
		"#!::r=live/s":            false,
		"":                        false,
		"#!::r=live/m=publisher1": false,
	} {
		if got := ffmpegPublishes(id); got != want {
			t.Errorf("ffmpegPublishes(%q) = %v, want %v", id, got, want)
		}
	}
}

// TestFFmpegRejectOf checks that libsrt's log of a refused handshake, in
// ffmpeg's output, gives the reason on the wire.
func TestFFmpegRejectOf(t *testing.T) {
	const prefix = "21:56:49.875715/!W:SRT.cn: @364338756: processConnectResponse: rejecting per reception of a rejection HS response: "
	for log, want := range map[string]int{
		prefix + "ERROR:BADSECRET\n[srt @ 0x1] Connection to srt://... failed: Input/output error": RejectBadSecret,
		prefix + "ERROR:UNSECURE":        RejectUnsecure,
		prefix + "ERROR:UNKNOWN":         RejectUnknown,
		prefix + "ERROR:PREDEFINED:7":    1007,
		prefix + "ERROR:USERDEFINED:403": 2403,
	} {
		var rej *RejectError
		if err := ffmpegRejectOf(errors.New("exit status 1"), log); !errors.As(err, &rej) || rej.Reason != want {
			t.Errorf("reject of %q: %v, want reason %v", log, err, want)
		}
	}
	other := "[srt @ 0x1] Connection to srt://127.0.0.1:9 failed: Connection setup failure: connection timed out"
	if err := ffmpegRejectOf(errors.New("exit status 1"), other); err == nil || errors.As(err, new(*RejectError)) || !strings.Contains(err.Error(), "timed out") {
		t.Errorf("a timeout: %v, want an error with ffmpeg's output and no reason", err)
	}
}

// TestFFmpegPathMissing checks that a missing ffmpeg, or one without libsrt,
// fails with how to build one.
func TestFFmpegPathMissing(t *testing.T) {
	_, err := ffmpegSRTPath(func() (*media.FFmpeg, error) { return nil, errors.New("no ffmpeg found") })
	if err == nil || !strings.Contains(err.Error(), "setup-ffmpeg-with-whip.sh") {
		t.Errorf("no ffmpeg: %v, want how to build one", err)
	}
	_, err = ffmpegSRTPath(func() (*media.FFmpeg, error) { return &media.FFmpeg{Path: "/no/such/ffmpeg"}, nil })
	if err == nil || !strings.Contains(err.Error(), "lacks libsrt") || !strings.Contains(err.Error(), "setup-ffmpeg-with-whip.sh") {
		t.Errorf("ffmpeg without libsrt: %v, want that it lacks libsrt and how to build one", err)
	}
}

// TestFFmpegOptionsRefused checks that the FFmpeg client refuses an option
// ffmpeg lacks, before it looks for ffmpeg.
func TestFFmpegOptionsRefused(t *testing.T) {
	for _, tc := range []struct {
		opts Options
		want string
	}{
		{Options{PeerIdleTimeout: time.Second}, "peer_idle_timeout"},
		{Options{PBKeyLen: 20, Passphrase: "a-long-secret"}, "pbkeylen"},
	} {
		c, err := FFmpeg.Dial(context.Background(), "127.0.0.1:1", tc.opts)
		if err == nil || !strings.Contains(err.Error(), tc.want) {
			t.Errorf("%+v: %v, %v; want an error naming %v", tc.opts, c, err, tc.want)
		}
	}
}

// TestFFmpegPublishPlay publishes the TS test file with ffmpeg to each
// library's listener, which relays it to an ffmpeg player that analyzes it,
// and checks the stream ID each listener got.
func TestFFmpegPublishPlay(t *testing.T) {
	needFFmpeg(t)
	file := media.Path(t, media.H264HighAACTS)
	for _, ln := range testListeners {
		t.Run(ln.name, func(t *testing.T) {
			t.Parallel()
			relay := relayServer()
			var ids atomic.Value
			ids.Store("")
			addr := ln.listen(t, "", 0, func(c peer, streamID string) {
				ids.Store(ids.Load().(string) + streamID + " ")
				relay(c, streamID)
			})
			ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
			defer cancel()

			type result struct {
				r   *report.Report
				err error
			}
			played := make(chan result, 1)
			go func() {
				r, err := PlayWith(ctx, FFmpeg, addr, Options{StreamID: "#!::r=live/s,m=request"}, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 2, VideoFrames: 30})
				played <- result{r, err}
			}()
			// Wait until the player is connected, so it gets the stream from its
			// start.
			time.Sleep(300 * time.Millisecond)

			pub, err := FFmpeg.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish"})
			if err != nil {
				t.Fatal(err)
			}
			defer pub.Close()
			if pub.Client() != "ffmpeg" {
				t.Errorf("the connection's client is %v, want ffmpeg", pub.Client())
			}
			f, err := os.Open(file)
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()
			pubCtx, stop := context.WithCancel(ctx)
			defer stop()
			go pub.Publish(pubCtx, f, 2*time.Second)

			res := <-played
			if res.err != nil {
				t.Fatalf("%v: %v", res.err, res.r)
			}
			v, a := res.r.Video, res.r.Audio
			if v == nil || a == nil || v.Codec != "h264" || a.Codec != "aac" || v.Width != 768 || a.SampleRate != 44100 {
				t.Errorf("report %v, want h264 768x320 and aac 44100Hz", res.r)
			}
			if got, want := ids.Load().(string), "#!::r=live/s,m=request #!::r=live/s,m=publish "; got != want {
				t.Errorf("stream IDs %q, want %q", got, want)
			}
		})
	}
}

// TestFFmpegPassphrase dials each listener with a passphrase: a wrong one
// and none are refused with libsrt's reasons, when playing at Dial and when
// publishing at Write, and the right one at 16 and 32 bytes publishes.
func TestFFmpegPassphrase(t *testing.T) {
	needFFmpeg(t)
	file := media.Path(t, media.H264HighAACTS)
	const phrase = "the-listener-phrase"
	// count serves a link by adding what it reads to got.
	count := func(got *atomic.Int64) func(c peer, streamID string) {
		return func(c peer, streamID string) {
			defer c.Close()
			b := make([]byte, 2048)
			for {
				n, err := c.Read(b)
				if err != nil {
					return
				}
				got.Add(int64(n))
			}
		}
	}
	open := func(t *testing.T) *os.File {
		f, err := os.Open(file)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { f.Close() })
		return f
	}

	for _, ln := range testListeners {
		t.Run(ln.name+"PlayRefused", func(t *testing.T) {
			t.Parallel()
			var got atomic.Int64
			addr := ln.listen(t, phrase, 0, count(&got))
			plain := ln.listen(t, "", 0, count(&got))
			cases := []struct {
				name       string
				addr       string
				passphrase string
				reason     int
			}{
				{"Wrong", addr, "a-wrong-secret", RejectBadSecret},
				{"Missing", addr, "", RejectUnsecure},
				{"NotExpected", plain, phrase, RejectUnsecure},
			}
			for _, tc := range cases {
				ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
				c, err := FFmpeg.Dial(ctx, tc.addr, Options{Passphrase: tc.passphrase})
				if err == nil {
					// The handshake outlived Dial's wait; its result ends the read.
					_, err = c.Read(make([]byte, 2048))
					c.Close()
				}
				cancel()
				var rej *RejectError
				if !errors.As(err, &rej) || rej.Reason != tc.reason {
					t.Errorf("play %v: %v; want rejected with %v", tc.name, err, tc.reason)
				}
			}
		})

		// A publisher connects after ffmpeg has read some media, so it learns
		// of the refusal when it writes.
		t.Run(ln.name+"PublishRefused", func(t *testing.T) {
			t.Parallel()
			var got atomic.Int64
			addr := ln.listen(t, phrase, 0, count(&got))
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			pub, err := FFmpeg.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish", Passphrase: "a-wrong-secret"})
			if err != nil {
				t.Fatal(err)
			}
			defer pub.Close()
			err = pub.Publish(ctx, open(t), 2*time.Second)
			var rej *RejectError
			if !errors.As(err, &rej) || rej.Reason != RejectBadSecret {
				t.Errorf("publish with a wrong passphrase: %v; want rejected with %v", err, RejectBadSecret)
			}
		})

		for _, keylen := range []int{16, 32} {
			t.Run(fmt.Sprintf("%vKey%v", ln.name, keylen), func(t *testing.T) {
				t.Parallel()
				var got atomic.Int64
				addr := ln.listen(t, phrase, keylen, count(&got))
				ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
				defer cancel()
				pub, err := FFmpeg.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish", Passphrase: phrase, PBKeyLen: keylen})
				if err != nil {
					t.Fatal(err)
				}
				defer pub.Close()
				published := make(chan error, 1)
				go func() { published <- pub.Publish(ctx, open(t), 1200*time.Millisecond) }()
				for got.Load() < 10000 && ctx.Err() == nil {
					select {
					case err := <-published:
						t.Fatalf("publish ended with %v after %v bytes", err, got.Load())
					case <-time.After(10 * time.Millisecond):
					}
				}
				if n := got.Load(); n < 10000 {
					t.Errorf("the listener got %v bytes, want the media", n)
				}
			})
		}
	}
}

// TestFFmpegOneWay checks that a player can't write and a publisher can't
// read: ffmpeg moves media one way.
func TestFFmpegOneWay(t *testing.T) {
	needFFmpeg(t)
	addr := listenZsiec(t, "", 0, func(c peer, streamID string) {
		b := make([]byte, 2048)
		for {
			if _, err := c.Read(b); err != nil {
				c.Close()
				return
			}
		}
	})
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	play, err := FFmpeg.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=request"})
	if err != nil {
		t.Fatal(err)
	}
	defer play.Close()
	if _, err := play.Write([]byte("hello")); err == nil || !strings.Contains(err.Error(), "m=publish") {
		t.Errorf("a player's write: %v, want an error saying to publish with m=publish", err)
	}

	pub, err := FFmpeg.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish"})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	if _, err := pub.Read(make([]byte, 2048)); err == nil || !strings.Contains(err.Error(), "publishes") {
		t.Errorf("a publisher's read: %v, want an error saying it publishes", err)
	}
}

// TestFFmpegWaitClosed has an ffmpeg player wait for each listener to close
// the connection.
func TestFFmpegWaitClosed(t *testing.T) {
	needFFmpeg(t)
	for _, ln := range testListeners {
		t.Run(ln.name, func(t *testing.T) {
			t.Parallel()
			addr := ln.listen(t, "", 0, func(c peer, streamID string) {
				time.Sleep(50 * time.Millisecond)
				c.Close()
			})
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			c, err := FFmpeg.Dial(ctx, addr, Options{})
			if err != nil {
				t.Fatal(err)
			}
			defer c.Close()
			if err := c.WaitClosed(ctx); err != nil {
				t.Errorf("WaitClosed: %v", err)
			}
		})
	}
}

// TestFFmpegNoListener checks that a call nobody answers fails, at Dial or
// at the first read, with no reject reason.
func TestFFmpegNoListener(t *testing.T) {
	needFFmpeg(t)
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
	defer cancel()
	t0 := time.Now()
	c, err := FFmpeg.Dial(ctx, "127.0.0.1:9", Options{Timeout: 300 * time.Millisecond})
	if err == nil {
		_, err = c.Read(make([]byte, 2048))
		c.Close()
	}
	if err == nil || errors.As(err, new(*RejectError)) || ctx.Err() != nil {
		t.Errorf("dial with no listener: %v, want ffmpeg's failure", err)
	}
	if d := time.Since(t0); d > 3*time.Second {
		t.Errorf("gave up after %v, want about 1s", d)
	}
}
