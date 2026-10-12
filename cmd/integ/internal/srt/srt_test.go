// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bytes"
	"context"
	"errors"
	"io"
	"net"
	"os"
	"reflect"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	gosrt "github.com/datarhei/gosrt"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

func TestParseURL(t *testing.T) {
	cases := []struct {
		url  string
		addr string
		opts Options
	}{
		{"srt://127.0.0.1:10080", "127.0.0.1:10080", Options{}},
		{"srt://127.0.0.1:10080?streamid=#!::r=live/s,m=publish", "127.0.0.1:10080",
			Options{StreamID: "#!::r=live/s,m=publish"}},
		{"srt://localhost:10080?streamid=%23%21%3A%3Ar%3Dlive%2Fs%2Cm%3Drequest", "localhost:10080",
			Options{StreamID: "#!::r=live/s,m=request"}},
		{"srt://[::1]:10080?streamid=live/s&passphrase=0123456789ab&pbkeylen=32&latency=200", "[::1]:10080",
			Options{StreamID: "live/s", Passphrase: "0123456789ab", PBKeyLen: 32, Latency: 200 * time.Millisecond}},
		{"srt://127.0.0.1:10080?latency=0&mode=caller", "127.0.0.1:10080", Options{}},
	}
	for _, tc := range cases {
		addr, opts, err := ParseURL(tc.url)
		if err != nil || addr != tc.addr || !reflect.DeepEqual(opts, tc.opts) {
			t.Errorf("ParseURL(%q) = %q, %+v, %v; want %q, %+v", tc.url, addr, opts, err, tc.addr, tc.opts)
		}
	}

	for _, bad := range []string{
		"rtmp://127.0.0.1:1935/live/s",
		"srt://127.0.0.1",
		"srt://:10080",
		"srt://127.0.0.1:10080?latency=x",
		"srt://127.0.0.1:10080?pbkeylen=20",
		"srt://127.0.0.1:10080?streamid=%zz",
	} {
		if _, _, err := ParseURL(bad); err == nil {
			t.Errorf("ParseURL(%q) succeeded", bad)
		}
	}
}

func TestRejectErrorNames(t *testing.T) {
	for reason, want := range map[int]string{
		RejectBadSecret: "SRT handshake rejected: 1010 (bad secret)",
		RejectUnsecure:  "SRT handshake rejected: 1011 (unsecure)",
		RejectPeer:      "SRT handshake rejected: 1002 (peer)",
		2000:            "SRT handshake rejected: 2000",
	} {
		if got := (&RejectError{Reason: reason}).Error(); got != want {
			t.Errorf("reason %v: %q, want %q", reason, got, want)
		}
	}
}

// listen starts an in-process gosrt listener on the loopback interface; each
// connection it accepts goes to serve. It stops when the test ends.
func listen(t *testing.T, cfg gosrt.Config, serve func(req gosrt.ConnRequest)) string {
	t.Helper()
	ln, err := gosrt.Listen("srt", "127.0.0.1:0", cfg)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(ln.Close)
	go func() {
		for {
			req, err := ln.Accept2()
			if err != nil {
				return
			}
			go serve(req)
		}
	}()
	return ln.Addr().String()
}

func TestDialSendsStreamID(t *testing.T) {
	got := make(chan string, 1)
	data := make(chan []byte, 1)
	addr := listen(t, gosrt.DefaultConfig(), func(req gosrt.ConnRequest) {
		got <- req.StreamId()
		c, err := req.Accept()
		if err != nil {
			return
		}
		defer c.Close()
		b := make([]byte, 2048)
		n, _ := c.Read(b)
		data <- b[:n]
	})

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	c, err := Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish"})
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if _, err := c.Write([]byte("hello")); err != nil {
		t.Fatal(err)
	}
	if s := <-got; s != "#!::r=live/s,m=publish" {
		t.Errorf("listener got streamid %q", s)
	}
	select {
	case b := <-data:
		if string(b) != "hello" {
			t.Errorf("listener read %q", b)
		}
	case <-ctx.Done():
		t.Fatal("listener read nothing")
	}
}

func TestDialRejected(t *testing.T) {
	// A gosrt listener checks the passphrase of each request itself.
	addr := listen(t, gosrt.DefaultConfig(), func(req gosrt.ConnRequest) {
		if !req.IsEncrypted() {
			req.Reject(gosrt.REJ_UNSECURE)
			return
		}
		if err := req.SetPassphrase("the-listener-secret"); err != nil {
			req.Reject(gosrt.REJ_BADSECRET)
			return
		}
		if c, err := req.Accept(); err == nil {
			c.Close()
		}
	})

	cases := []struct {
		passphrase string
		reason     int
	}{
		{"a-wrong-secret", RejectBadSecret},
		{"", RejectUnsecure},
	}
	for _, tc := range cases {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		c, err := Dial(ctx, addr, Options{Passphrase: tc.passphrase})
		cancel()
		var rej *RejectError
		if !errors.As(err, &rej) || rej.Reason != tc.reason {
			t.Errorf("passphrase %q: %v, %v; want rejected with %v", tc.passphrase, c, err, tc.reason)
		}
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	c, err := Dial(ctx, addr, Options{Passphrase: "the-listener-secret"})
	if err != nil {
		t.Fatalf("the right passphrase: %v", err)
	}
	c.Close()
}

func TestDialTimeout(t *testing.T) {
	// A UDP port where nothing answers.
	pc, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer pc.Close()

	start := time.Now()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	_, err = Dial(ctx, pc.LocalAddr().String(), Options{Timeout: 200 * time.Millisecond})
	if err == nil {
		t.Fatal("dial succeeded")
	}
	if d := time.Since(start); d > time.Second {
		t.Errorf("dial failed after %v, want about 200ms", d)
	}
}

func TestWaitClosed(t *testing.T) {
	addr := listen(t, gosrt.DefaultConfig(), func(req gosrt.ConnRequest) {
		c, err := req.Accept()
		if err != nil {
			return
		}
		c.Write([]byte("bye"))
		time.Sleep(50 * time.Millisecond)
		c.Close()
	})

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	c, err := Dial(ctx, addr, Options{})
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if err := c.WaitClosed(ctx); err != nil {
		t.Errorf("WaitClosed: %v", err)
	}

	// A peer that never closes leaves it to ctx.
	addr = listen(t, gosrt.DefaultConfig(), func(req gosrt.ConnRequest) {
		if c, err := req.Accept(); err == nil {
			time.Sleep(2 * time.Second)
			c.Close()
		}
	})
	c2, err := Dial(ctx, addr, Options{})
	if err != nil {
		t.Fatal(err)
	}
	defer c2.Close()
	short, cancel2 := context.WithTimeout(ctx, 100*time.Millisecond)
	defer cancel2()
	start := time.Now()
	if err := c2.WaitClosed(short); !errors.Is(err, context.DeadlineExceeded) {
		t.Errorf("WaitClosed of an open connection: %v", err)
	}
	if d := time.Since(start); d > 500*time.Millisecond {
		t.Errorf("WaitClosed returned %v after its ctx ended", d)
	}
}

// tsPacket makes a TS packet of pid, with a PCR in an adaptation field when
// pcr is not negative, in 90 kHz units.
func tsPacket(pid uint16, cc byte, pcr int64) []byte {
	p := bytes.Repeat([]byte{0xff}, 188)
	p[0], p[1], p[2] = 0x47, byte(pid>>8)&0x1f, byte(pid)
	if pcr < 0 {
		p[3] = 0x10 | cc&0x0f
		return p
	}
	p[3] = 0x30 | cc&0x0f
	p[4], p[5] = 7, 0x10
	b := uint64(pcr) << 15
	p[6], p[7], p[8], p[9], p[10], p[11] = byte(b>>40), byte(b>>32), byte(b>>24), byte(b>>16), byte(b>>8)|0x7e, 0
	return p
}

// tsStream makes n packets with a PCR every 7 packets, 40ms apart.
func tsStream(n int) []byte {
	var b []byte
	for i := 0; i < n; i++ {
		pcr := int64(-1)
		if i%7 == 0 {
			pcr = int64(i/7) * 40 * 90
		}
		b = append(b, tsPacket(0x100, byte(i), pcr)...)
	}
	return b
}

// writes records each Write.
type writes struct{ msgs [][]byte }

func (w *writes) Write(p []byte) (int, error) {
	w.msgs = append(w.msgs, append([]byte(nil), p...))
	return len(p), nil
}

func TestSenderMessages(t *testing.T) {
	data := tsStream(37) // 5 messages of 7 and one of 2: PCRs at 0, 40, 80, 120, 160 and 200ms
	s := NewSender(bytes.NewReader(data))
	w := &writes{}

	if err := s.SendThrough(w, 50*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	if len(w.msgs) != 2 {
		t.Fatalf("sent %v messages through 50ms, want 2 (at 0 and 40ms)", len(w.msgs))
	}

	now := time.Unix(1000, 0)
	var sleeps []time.Duration
	err := s.Stream(context.Background(), w, func() time.Time { return now }, func(ctx context.Context, d time.Duration) error {
		sleeps = append(sleeps, d)
		now = now.Add(d)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	// The message at 80ms goes at once, then each 40ms later; the last, of 2
	// packets, has the PCR of 200ms.
	if want := []time.Duration{40 * time.Millisecond, 40 * time.Millisecond, 40 * time.Millisecond}; !reflect.DeepEqual(sleeps, want) {
		t.Errorf("slept %v, want %v", sleeps, want)
	}
	if len(w.msgs) != 6 {
		t.Fatalf("sent %v messages, want 6", len(w.msgs))
	}
	for i, m := range w.msgs[:5] {
		if len(m) != MessageSize {
			t.Errorf("message %v of %v bytes, want %v", i, len(m), MessageSize)
		}
	}
	if !bytes.Equal(bytes.Join(w.msgs, nil), data) {
		t.Errorf("the messages are not the stream")
	}
	if msgs, n := s.Sent(); msgs != 6 || n != len(data) {
		t.Errorf("Sent() = %v, %v; want 6, %v", msgs, n, len(data))
	}
}

func TestSenderErrors(t *testing.T) {
	bad := tsStream(3)
	bad[188] = 0x46
	if err := NewSender(bytes.NewReader(bad)).SendThrough(io.Discard, time.Hour); err == nil {
		t.Errorf("sent a stream that lost its sync byte")
	}

	short := tsStream(2)[:300]
	if err := NewSender(bytes.NewReader(short)).SendThrough(io.Discard, time.Hour); err == nil {
		t.Errorf("sent a stream that ends in the middle of a packet")
	}

	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if err := NewSender(bytes.NewReader(tsStream(14))).Stream(ctx, io.Discard, nil, nil); !errors.Is(err, context.Canceled) {
		t.Errorf("Stream with a canceled ctx: %v", err)
	}
}

// TestPublishPlay publishes the TS test file to an in-process listener that
// relays it to a player, which analyzes it.
func TestPublishPlay(t *testing.T) {
	file := media.Path(t, media.H264HighAACTS)
	relay := make(chan []byte, 4096)
	addr := listen(t, gosrt.DefaultConfig(), func(req gosrt.ConnRequest) {
		c, err := req.Accept()
		if err != nil {
			return
		}
		defer c.Close()
		if strings.HasSuffix(req.StreamId(), "m=publish") {
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
	})

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	type result struct {
		r   *report.Report
		err error
	}
	played := make(chan result, 1)
	go func() {
		r, err := Play(ctx, addr, Options{StreamID: "#!::r=live/s,m=request"}, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 2, VideoFrames: 30})
		played <- result{r, err}
	}()

	pub, err := Dial(ctx, addr, Options{StreamID: "#!::r=live/s,m=publish"})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	f, err := os.Open(file)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	// Wait until the player is connected, so it gets the stream from its
	// start.
	time.Sleep(100 * time.Millisecond)
	if err := NewSender(f).SendThrough(pub, 2*time.Second); err != nil {
		t.Fatal(err)
	}

	res := <-played
	if res.err != nil {
		t.Fatalf("%v: %v", res.err, res.r)
	}
	v, a := res.r.Video, res.r.Audio
	if v == nil || a == nil || v.Codec != "h264" || a.Codec != "aac" || v.Width != 768 || v.LeadingFrames != 0 {
		t.Errorf("report %v, want h264 768x320 from a keyframe and aac", res.r)
	}
}

// TestTSBPD reads the TSBPD delays a caller agreed with a listener whose
// receive latency is 250ms and peer latency 150ms: each end holds what it
// receives for the larger of its receive latency and the sender's peer
// latency.
func TestTSBPD(t *testing.T) {
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
		latency    time.Duration
		recv, send time.Duration
	}{
		{"NoLatency", 0, 150 * time.Millisecond, 250 * time.Millisecond},
		{"CallerHigher", 400 * time.Millisecond, 400 * time.Millisecond, 400 * time.Millisecond},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			c, err := Dial(ctx, addr, Options{Latency: tc.latency})
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

// relay forwards UDP between callers and the listener at addr until mute is
// called, after which it drops everything, like a peer that is gone
// without a word. It returns its own address.
func relay(t *testing.T, addr string) (string, func()) {
	t.Helper()
	front, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { front.Close() })
	back, err := net.Dial("udp", addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { back.Close() })

	var muted atomic.Bool
	var caller atomic.Value
	go func() {
		b := make([]byte, 2048)
		for {
			n, from, err := front.ReadFrom(b)
			if err != nil {
				return
			}
			caller.Store(from)
			if !muted.Load() {
				back.Write(b[:n])
			}
		}
	}()
	go func() {
		b := make([]byte, 2048)
		for {
			n, err := back.Read(b)
			if err != nil {
				return
			}
			if to, ok := caller.Load().(net.Addr); ok && !muted.Load() {
				front.WriteTo(b[:n], to)
			}
		}
	}()
	return front.LocalAddr().String(), func() { muted.Store(true) }
}

// TestWaitClosedProbing checks the wait for a peer that may close without
// the client hearing it: the client sends a null TS packet now and then, so
// a live peer acknowledges it and the connection stays, and a peer that is
// gone answers nothing, so the client's idle timeout ends it.
func TestWaitClosedProbing(t *testing.T) {
	addr := listen(t, gosrt.DefaultConfig(), func(req gosrt.ConnRequest) {
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
	opts := Options{PeerIdleTimeout: 250 * time.Millisecond}

	t.Run("Gone", func(t *testing.T) {
		front, mute := relay(t, addr)
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		c, err := Dial(ctx, front, opts)
		if err != nil {
			t.Fatal(err)
		}
		defer c.Close()
		mute()
		start := time.Now()
		if err := c.WaitClosedProbing(ctx, 50*time.Millisecond); err != nil {
			t.Errorf("WaitClosedProbing: %v", err)
		}
		if d := time.Since(start); d > time.Second {
			t.Errorf("closed after %v, want about 250ms", d)
		}
	})

	t.Run("Alive", func(t *testing.T) {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		c, err := Dial(ctx, addr, opts)
		if err != nil {
			t.Fatal(err)
		}
		defer c.Close()
		short, cancel2 := context.WithTimeout(ctx, 700*time.Millisecond)
		defer cancel2()
		if err := c.WaitClosedProbing(short, 50*time.Millisecond); !errors.Is(err, context.DeadlineExceeded) {
			t.Errorf("WaitClosedProbing of a live peer: %v, want the deadline", err)
		}
	})
}
