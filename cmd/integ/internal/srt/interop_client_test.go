// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"context"
	"errors"
	"os"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// TestLiveTransmitURI checks the caller's URI: the stream ID and the
// passphrase escaped, as the tool URL-decodes each value, and the times in
// milliseconds.
func TestLiveTransmitURI(t *testing.T) {
	got := liveTransmitURI("host.docker.internal:10080", Options{
		StreamID: "#!::r=live/s,m=publish", Passphrase: "a secret&more", PBKeyLen: 32, Latency: 80 * time.Millisecond,
	}, 500*time.Millisecond)
	want := "srt://host.docker.internal:10080?mode=caller&transtype=live&conntimeo=500" +
		"&streamid=%23%21%3A%3Ar%3Dlive%2Fs%2Cm%3Dpublish&passphrase=a+secret%26more&pbkeylen=32&latency=80"
	if got != want {
		t.Errorf("URI\n got %v\nwant %v", got, want)
	}
}

// TestLiveTransmitOptionsRefused checks that the client refuses, by name, an
// option it doesn't pass to the tool, before it looks for Docker.
func TestLiveTransmitOptionsRefused(t *testing.T) {
	on := true
	for _, tc := range []struct {
		opts Options
		want string
	}{
		{Options{PeerIdleTimeout: time.Second}, "peer_idle_timeout"},
		{Options{RecvLatency: time.Second}, "rcvlatency"},
		{Options{MSS: 1000, TLPktDrop: &on}, "mss, tlpktdrop"},
		{Options{PBKeyLen: 20, Passphrase: "a-long-secret"}, "pbkeylen"},
	} {
		c, err := LiveTransmit.Dial(context.Background(), "127.0.0.1:1", tc.opts)
		if err == nil || !strings.Contains(err.Error(), tc.want) {
			t.Errorf("%+v: %v, %v; want an error naming %v", tc.opts, c, err, tc.want)
		}
	}
}

// TestLiveTransmitNamed checks that ClientNamed finds the tool.
func TestLiveTransmitNamed(t *testing.T) {
	cl, err := ClientNamed("srtLiveTransmit")
	if err != nil || cl != LiveTransmit {
		t.Errorf("ClientNamed: %v, %v; want LiveTransmit", cl, err)
	}
}

// TestLiveTransmitPublishPlay publishes the TS test file with the tool to each
// library's listener, which relays it to a player of the tool that analyzes
// it, plain and with a passphrase of 32 bytes, and checks the stream IDs each
// listener got.
func TestLiveTransmitPublishPlay(t *testing.T) {
	needInterop(t)
	file := media.Path(t, media.H264HighAACTS)
	const phrase = "the-listener-phrase"
	for _, ln := range testListeners {
		for _, passphrase := range []string{"", phrase} {
			name := ln.name + "/Plain"
			if passphrase != "" {
				name = ln.name + "/Aes32"
			}
			t.Run(name, func(t *testing.T) {
				t.Parallel()
				relay := relayServer()
				var ids atomic.Value
				ids.Store("")
				addr := ln.listen(t, passphrase, 32, func(c peer, streamID string) {
					ids.Store(ids.Load().(string) + streamID + " ")
					relay(c, streamID)
				})
				ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
				defer cancel()
				opts := func(id string) Options {
					o := Options{StreamID: id, Passphrase: passphrase}
					if passphrase != "" {
						o.PBKeyLen = 32
					}
					return o
				}

				play, err := LiveTransmit.Dial(ctx, addr, opts("#!::r=live/s,m=request"))
				if err != nil {
					t.Fatal(err)
				}
				defer play.Close()
				pub, err := LiveTransmit.Dial(ctx, addr, opts("#!::r=live/s,m=publish"))
				if err != nil {
					t.Fatal(err)
				}
				defer pub.Close()
				if pub.Client() != "srtLiveTransmit" {
					t.Errorf("the connection's client is %v, want srtLiveTransmit", pub.Client())
				}
				f, err := os.Open(file)
				if err != nil {
					t.Fatal(err)
				}
				defer f.Close()
				pubCtx, stop := context.WithCancel(ctx)
				defer stop()
				go pub.Publish(pubCtx, f, 2*time.Second)

				r, err := play.Analyze(ctx, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 2, VideoFrames: 30})
				if err != nil {
					t.Fatalf("%v: %v", err, r)
				}
				v, a := r.Video, r.Audio
				if v == nil || a == nil || v.Codec != "h264" || a.Codec != "aac" || v.Width != 768 || a.SampleRate != 44100 {
					t.Errorf("report %v, want h264 768x320 and aac 44100Hz", r)
				}
				if got, want := ids.Load().(string), "#!::r=live/s,m=request #!::r=live/s,m=publish "; got != want {
					t.Errorf("stream IDs %q, want %q", got, want)
				}
			})
		}
	}
}

// TestLiveTransmitRefused checks that a publisher and a player with a wrong
// passphrase are refused at Dial, with libsrt's reason, and that a player can't write and a publisher
// can't read.
func TestLiveTransmitRefused(t *testing.T) {
	needInterop(t)
	t.Parallel()
	addr := listenGosrt(t, "the-listener-phrase", 0, func(c peer, streamID string) {
		b := make([]byte, 2048)
		for {
			if _, err := c.Read(b); err != nil {
				c.Close()
				return
			}
		}
	})
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	for _, id := range []string{"#!::r=live/s,m=publish", "#!::r=live/s,m=request"} {
		c, err := LiveTransmit.Dial(ctx, addr, Options{StreamID: id, Passphrase: "a-wrong-passphrase"})
		var rej *RejectError
		if !errors.As(err, &rej) || rej.Reason != RejectBadSecret {
			t.Errorf("%v: dial %v, %v; want a refusal for a bad secret", id, c, err)
		}
		if c != nil {
			c.Close()
		}
	}

	play, err := LiveTransmit.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=request", Passphrase: "the-listener-phrase"})
	if err != nil {
		t.Fatal(err)
	}
	defer play.Close()
	if _, err := play.Write([]byte("hello")); err == nil || !strings.Contains(err.Error(), "m=publish") {
		t.Errorf("a player's write: %v, want an error saying to publish with m=publish", err)
	}
	pub, err := LiveTransmit.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish", Passphrase: "the-listener-phrase"})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	if _, err := pub.Read(make([]byte, 2048)); err == nil || !strings.Contains(err.Error(), "publishes") {
		t.Errorf("a publisher's read: %v, want an error saying it publishes", err)
	}
}
