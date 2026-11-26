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
	"testing"
	"time"

	gosrt "github.com/datarhei/gosrt"
	zsrt "github.com/zsiec/srtgo"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// A peer is a link the listener accepted, whichever library it is.
type peer interface {
	Read(p []byte) (int, error)
	Write(p []byte) (int, error)
	Close() error
}

// A testListener is one library's listener: it calls serve with each link it
// accepts and its stream ID, and checks the passphrase, if it has one, with
// a key of keylen bytes, zero for 16.
type testListener struct {
	name   string
	listen func(t *testing.T, passphrase string, keylen int, serve func(c peer, streamID string)) string
}

var testListeners = []testListener{
	{"gosrt", listenGosrt},
	{"zsiec", listenZsiec},
}

// listenGosrt is a gosrt listener, which checks the passphrase of each
// request itself.
func listenGosrt(t *testing.T, passphrase string, keylen int, serve func(c peer, streamID string)) string {
	t.Helper()
	cfg := gosrt.DefaultConfig()
	if keylen != 0 {
		cfg.PBKeylen = keylen
	}
	return listen(t, cfg, func(req gosrt.ConnRequest) {
		if passphrase != "" {
			if !req.IsEncrypted() {
				req.Reject(gosrt.REJ_UNSECURE)
				return
			}
			if err := req.SetPassphrase(passphrase); err != nil {
				req.Reject(gosrt.REJ_BADSECRET)
				return
			}
		} else if req.IsEncrypted() {
			req.Reject(gosrt.REJ_UNSECURE)
			return
		}
		c, err := req.Accept()
		if err != nil {
			return
		}
		serve(c, req.StreamId())
	})
}

// listenZsiec is a zsiec/srtgo listener; it gets the stream ID from its
// accept gate, by the caller's address.
func listenZsiec(t *testing.T, passphrase string, keylen int, serve func(c peer, streamID string)) string {
	t.Helper()
	cfg := zsrt.DefaultConfig()
	cfg.Passphrase = passphrase
	cfg.KeyLength = keylen
	cfg.Linger = -1
	ln, err := zsrt.Listen("127.0.0.1:0", cfg)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			go serve(c, c.StreamID())
		}
	}()
	return ln.Addr().String()
}

// relayServer serves a publisher, whose stream ID ends with m=publish, by
// passing what it reads to the players, which get it from their start.
func relayServer() func(c peer, streamID string) {
	relay := make(chan []byte, 4096)
	return func(c peer, streamID string) {
		defer c.Close()
		if strings.HasSuffix(streamID, "m=publish") {
			for {
				b := make([]byte, 2048)
				n, err := c.Read(b)
				if err != nil {
					return
				}
				relay <- b[:n]
			}
		}
		for b := range relay {
			if _, err := c.Write(b); err != nil {
				return
			}
		}
	}
}

// needClient skips a test of libsrt when the srt-libsrt tool is not built, as
// the client tests skip ffmpeg when there is none.
func needClient(t *testing.T, cl Client) {
	t.Helper()
	if cl != Libsrt {
		return
	}
	if _, err := LibsrtPath(); err != nil {
		t.Skip(err)
	}
}

// forEachPair runs f for each client against each library's listener.
func forEachPair(t *testing.T, f func(t *testing.T, cl Client, ln testListener)) {
	for _, cl := range Clients() {
		for _, ln := range testListeners {
			t.Run(cl.Name()+"To"+strings.ToUpper(ln.name[:1])+ln.name[1:], func(t *testing.T) {
				t.Parallel()
				needClient(t, cl)
				f(t, cl, ln)
			})
		}
	}
}

func TestClients(t *testing.T) {
	var names []string
	for _, cl := range Clients() {
		names = append(names, cl.Name())
		got, err := ClientNamed(cl.Name())
		if err != nil || got != cl {
			t.Errorf("ClientNamed(%q) = %v, %v", cl.Name(), got, err)
		}
	}
	if fmt.Sprint(names) != "[gosrt zsiec libsrt]" {
		t.Errorf("clients %v, want gosrt, zsiec and libsrt", names)
	}
	// FFmpeg moves media one way, so it is no client of the matrix, but a
	// case can name it.
	if got, err := ClientNamed("ffmpeg"); err != nil || got != FFmpeg {
		t.Errorf("ClientNamed(ffmpeg) = %v, %v", got, err)
	}
	if _, err := ClientNamed("nothing"); err == nil || !strings.Contains(err.Error(), "gosrt") || !strings.Contains(err.Error(), "ffmpeg") {
		t.Errorf("ClientNamed of an unknown library: %v, want an error that lists the libraries", err)
	}
}

// TestClientsPublishPlay publishes the TS test file with each client to each
// library's listener, which relays it to a player of the same client that
// analyzes it.
func TestClientsPublishPlay(t *testing.T) {
	file := media.Path(t, media.H264HighAACTS)
	forEachPair(t, func(t *testing.T, cl Client, ln testListener) {
		addr := ln.listen(t, "", 0, relayServer())
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()

		type result struct {
			r   *report.Report
			err error
		}
		played := make(chan result, 1)
		go func() {
			r, err := PlayWith(ctx, cl, addr, Options{StreamID: "#!::r=live/s,m=request"}, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 2, VideoFrames: 30})
			played <- result{r, err}
		}()

		pub, err := cl.Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish"})
		if err != nil {
			t.Fatal(err)
		}
		defer pub.Close()
		if pub.Client() != cl.Name() {
			t.Errorf("the connection's client is %v, want %v", pub.Client(), cl.Name())
		}
		f, err := os.Open(file)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		// Wait until the player is connected, so it gets the stream from its
		// start; the publisher sends 2s at once and the rest in real time.
		time.Sleep(100 * time.Millisecond)
		pubCtx, stop := context.WithCancel(ctx)
		defer stop()
		go pub.Publish(pubCtx, f, 2*time.Second)

		res := <-played
		if res.err != nil {
			t.Fatalf("%v: %v", res.err, res.r)
		}
		v, a := res.r.Video, res.r.Audio
		if v == nil || a == nil || v.Codec != "h264" || a.Codec != "aac" || v.Width != 768 || v.LeadingFrames != 0 {
			t.Errorf("report %v, want h264 768x320 from a keyframe and aac", res.r)
		}
	})
}

// TestClientsPassphrase dials a listener with a passphrase: the right one
// connects, in both directions, a wrong one and none are refused with
// libsrt's reasons, and an encrypted caller to a listener without one is
// refused.
func TestClientsPassphrase(t *testing.T) {
	forEachPair(t, func(t *testing.T, cl Client, ln testListener) {
		const phrase = "the-listener-phrase"
		echo := func(c peer, streamID string) {
			defer c.Close()
			b := make([]byte, 2048)
			for {
				n, err := c.Read(b)
				if err != nil {
					return
				}
				if _, err := c.Write(b[:n]); err != nil {
					return
				}
			}
		}
		addr := ln.listen(t, phrase, 0, echo)
		plain := ln.listen(t, "", 0, echo)

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
			c, err := cl.Dial(ctx, tc.addr, Options{Passphrase: tc.passphrase})
			cancel()
			var rej *RejectError
			if !errors.As(err, &rej) || rej.Reason != tc.reason {
				t.Errorf("%v: %v, %v; want rejected with %v", tc.name, c, err, tc.reason)
			}
			if c != nil {
				c.Close()
			}
		}

		// libsrt's listener takes the caller's key length; neither pure-Go
		// listener does with another library's caller, so each key length
		// gets a listener of its own.
		for _, keylen := range []int{16, 32} {
			addr := ln.listen(t, phrase, keylen, echo)
			if reason, ok := knownPairFailure(cl.Name(), ln.name, keylen); ok {
				ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
				c, err := cl.Dial(ctx, addr, Options{Passphrase: phrase, PBKeyLen: keylen})
				cancel()
				if err == nil {
					c.Close()
					t.Errorf("key length %v connects now; drop it from knownPairFailure: %v", keylen, reason)
				} else {
					t.Logf("key length %v skipped, known: %v", keylen, reason)
				}
				continue
			}
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			c, err := cl.Dial(ctx, addr, Options{Passphrase: phrase, PBKeyLen: keylen})
			if err != nil {
				t.Fatalf("the right passphrase, key length %v: %v", keylen, err)
			}
			if _, err := c.Write([]byte("hello")); err != nil {
				t.Fatal(err)
			}
			b := make([]byte, 2048)
			if n, err := c.Read(b); err != nil || string(b[:n]) != "hello" {
				t.Errorf("key length %v: read %q, %v; want the echo of hello", keylen, b[:n], err)
			}
			c.Close()
		}
	})
}

// knownPairFailure tells whether a caller library fails with a listener
// library at a key length, and why. Both pure-Go libraries connect to libsrt
// 1.5.6's listener at 16 and 32, so it is between the two of them, not with
// SRS.
func knownPairFailure(caller, listener string, keylen int) (string, bool) {
	pure := map[string]bool{"gosrt": true, "zsiec": true}
	if keylen == 32 && caller != listener && pure[caller] && pure[listener] {
		return "gosrt and zsiec time out in the handshake with each other at AES-256", true
	}
	return "", false
}

// TestClientsWaitClosed has each client wait for each listener to close the
// connection.
func TestClientsWaitClosed(t *testing.T) {
	forEachPair(t, func(t *testing.T, cl Client, ln testListener) {
		addr := ln.listen(t, "", 0, func(c peer, streamID string) {
			time.Sleep(50 * time.Millisecond)
			c.Close()
		})
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		c, err := cl.Dial(ctx, addr, Options{})
		if err != nil {
			t.Fatal(err)
		}
		defer c.Close()
		if err := c.WaitClosedProbing(ctx, 100*time.Millisecond); err != nil {
			t.Errorf("WaitClosed: %v", err)
		}
	})
}

// TestClientsTSBPD reads the TSBPD delays each client agreed with a gosrt
// listener whose receive latency is 250ms and peer latency 150ms, when it asks
// for more.
func TestClientsTSBPD(t *testing.T) {
	cfg := gosrt.DefaultConfig()
	cfg.ReceiverLatency = 250 * time.Millisecond
	cfg.PeerLatency = 150 * time.Millisecond
	addr := listen(t, cfg, func(req gosrt.ConnRequest) {
		c, err := req.Accept()
		if err != nil {
			return
		}
		b := make([]byte, 2048)
		for {
			if _, err := c.Read(b); err != nil {
				c.Close()
				return
			}
		}
	})

	cases := []struct {
		name       string
		opts       Options
		recv, send time.Duration
	}{
		{"CallerHigher", Options{Latency: 400 * time.Millisecond}, 400 * time.Millisecond, 400 * time.Millisecond},
		{"RecvAndPeer", Options{RecvLatency: 300 * time.Millisecond, PeerLatency: 280 * time.Millisecond}, 300 * time.Millisecond, 280 * time.Millisecond},
	}
	for _, cl := range Clients() {
		for _, tc := range cases {
			t.Run(cl.Name()+tc.name, func(t *testing.T) {
				t.Parallel()
				needClient(t, cl)
				ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
				defer cancel()
				c, err := cl.Dial(ctx, addr, tc.opts)
				if err != nil {
					t.Fatal(err)
				}
				defer c.Close()
				if recv, send := c.TSBPD(); recv != tc.recv || send != tc.send {
					t.Errorf("TSBPD recv %v send %v, want %v and %v", recv, send, tc.recv, tc.send)
				}
			})
		}
	}
}

// TestClientsOptionsRefused checks that a client refuses an option its
// library lacks or a value it can't take, instead of dropping it.
func TestClientsOptionsRefused(t *testing.T) {
	off := false
	cases := []struct {
		client Client
		opts   Options
		want   string
	}{
		{Zsiec, Options{TSBPDMode: &off}, "tsbpdmode"},
		{Zsiec, Options{TLPktDrop: &off}, "tlpktdrop"},
		{Zsiec, Options{Latency: 10 * time.Millisecond}, "latency"},
		{GoSRT, Options{PBKeyLen: 20, Passphrase: "a-long-secret"}, "pbkeylen"},
		{Zsiec, Options{PBKeyLen: 20, Passphrase: "a-long-secret"}, "pbkeylen"},
		{Libsrt, Options{PBKeyLen: 20, Passphrase: "a-long-secret"}, "pbkeylen"},
	}
	for _, tc := range cases {
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		c, err := tc.client.Dial(ctx, "127.0.0.1:1", tc.opts)
		cancel()
		if err == nil || !strings.Contains(err.Error(), tc.want) {
			t.Errorf("%v with %+v: %v, %v; want an error naming %v", tc.client.Name(), tc.opts, c, err, tc.want)
		}
	}
}
