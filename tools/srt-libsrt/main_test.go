// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

// A proc is one run of the command in a goroutine, with its output.
type proc struct {
	stdout syncBuffer
	events chan event
	code   chan int
	mu     sync.Mutex
	all    []event
}

// syncBuffer is a bytes.Buffer that the command writes while the test reads.
type syncBuffer struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (b *syncBuffer) Write(p []byte) (int, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.b.Write(p)
}

func (b *syncBuffer) Bytes() []byte {
	b.mu.Lock()
	defer b.mu.Unlock()
	return bytes.Clone(b.b.Bytes())
}

// start runs the command with args and stdin until ctx ends.
func start(t *testing.T, ctx context.Context, stdin io.Reader, args ...string) *proc {
	t.Helper()
	p := &proc{events: make(chan event, 16), code: make(chan int, 1)}
	pr, pw := io.Pipe()
	go func() {
		s := bufio.NewScanner(pr)
		for s.Scan() {
			var e event
			if err := json.Unmarshal(s.Bytes(), &e); err != nil {
				t.Logf("%v: %s", args[0], s.Bytes())
				continue
			}
			p.mu.Lock()
			p.all = append(p.all, e)
			p.mu.Unlock()
			p.events <- e
		}
	}()
	go func() {
		p.code <- run(ctx, args, stdin, &p.stdout, pw)
		pw.Close()
	}()
	return p
}

// next waits for the next event, which must be want.
func (p *proc) next(t *testing.T, want string) event {
	t.Helper()
	select {
	case e := <-p.events:
		if e.Event != want {
			t.Fatalf("event %+v, want %v", e, want)
		}
		return e
	case <-time.After(3 * time.Second):
		t.Fatalf("no %v event", want)
	}
	return event{}
}

// wait waits for the command to end and returns its exit code and its last
// event.
func (p *proc) wait(t *testing.T) (int, event) {
	t.Helper()
	select {
	case code := <-p.code:
		// The events are written before run returns; give the reader a moment.
		time.Sleep(10 * time.Millisecond)
		p.mu.Lock()
		defer p.mu.Unlock()
		if len(p.all) == 0 {
			return code, event{}
		}
		return code, p.all[len(p.all)-1]
	case <-time.After(5 * time.Second):
		t.Fatal("the command did not end")
	}
	return 0, event{}
}

// listenOn starts a listener with flags and returns it and its address.
func listenOn(t *testing.T, ctx context.Context, stdin io.Reader, flags ...string) (*proc, string) {
	t.Helper()
	ln := start(t, ctx, stdin, append(append([]string{"listen"}, flags...), "127.0.0.1:0")...)
	e := ln.next(t, "listening")
	return ln, e.Addr
}

// tsMessages makes n messages of 7 TS packets, each packet numbered.
func tsMessages(n int) []byte {
	b := make([]byte, 0, n*1316)
	for i := 0; i < n*7; i++ {
		p := make([]byte, 188)
		p[0] = 0x47
		copy(p[4:], fmt.Sprintf("packet %08d", i))
		b = append(b, p...)
	}
	return b
}

// TestPublishToListener publishes 500 messages from stdin to a listener,
// which writes them to its stdout; the publisher ends at the end of its stdin
// and the listener when the publisher closes.
func TestPublishToListener(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	keep, _ := io.Pipe()
	ln, addr := listenOn(t, ctx, keep, "-latency", "20")

	data := tsMessages(500)
	pub := start(t, ctx, bytes.NewReader(data), "publish", "-latency", "20", "-streamid", "#!::r=live/a,m=publish", addr)
	pub.next(t, "connected")
	if e := ln.next(t, "connected"); e.StreamID != "#!::r=live/a,m=publish" {
		t.Errorf("listener got stream ID %q", e.StreamID)
	}

	code, e := pub.wait(t)
	if code != 0 || e.Event != "closed" || e.Reason != "eof" || e.MsgSent != 500 || e.Stats == nil || e.Stats.PktSent < 500 {
		t.Errorf("publisher exit %v, last event %+v; want closed at eof after 500 messages", code, e)
	}
	code, e = ln.wait(t)
	if code != 0 || e.Event != "closed" || e.Reason != "peer" || e.MsgRecv != 500 || e.Stats == nil || e.Stats.PktRecv < 500 {
		t.Errorf("listener exit %v, last event %+v; want closed by the peer after 500 messages", code, e)
	}
	if got := ln.stdout.Bytes(); !bytes.Equal(got, data) {
		t.Errorf("listener wrote %v bytes, want the %v published", len(got), len(data))
	}
}

// TestPlayFromListener plays what a listener sends from its stdin; play ends
// when the listener closes at the end of its stdin.
func TestPlayFromListener(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	data := tsMessages(200)
	ln, addr := listenOn(t, ctx, bytes.NewReader(data), "-latency", "20")

	play := start(t, ctx, nil, "play", "-latency", "20", addr)
	play.next(t, "connected")
	if code, e := ln.wait(t); code != 0 || e.Reason != "eof" || e.MsgSent != 200 {
		t.Errorf("listener exit %v, last event %+v; want closed at eof after 200 messages", code, e)
	}
	if code, e := play.wait(t); code != 0 || e.Reason != "peer" || e.MsgRecv != 200 {
		t.Errorf("play exit %v, last event %+v; want closed by the peer after 200 messages", code, e)
	}
	if got := play.stdout.Bytes(); !bytes.Equal(got, data) {
		t.Errorf("play wrote %v bytes, want the %v sent", len(got), len(data))
	}
}

// TestPassphrase calls a listener with an AES-256 passphrase with a wrong one
// and none, which it rejects, then the right one.
func TestPassphrase(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	keep, _ := io.Pipe()
	ln, addr := listenOn(t, ctx, keep, "-passphrase", "a-long-secret", "-pbkeylen", "32")

	for _, tc := range []struct {
		name   string
		flags  []string
		reason int
	}{
		{"Wrong", []string{"-passphrase", "another-secret", "-pbkeylen", "32"}, 1010},
		{"Missing", nil, 1011},
	} {
		c := start(t, ctx, nil, append(append([]string{"play"}, tc.flags...), addr)...)
		if code, e := c.wait(t); code != 1 || e.Event != "error" || e.Reject != tc.reason {
			t.Errorf("%v: exit %v, last event %+v; want an error with reject reason %v", tc.name, code, e, tc.reason)
		}
	}

	c := start(t, ctx, strings.NewReader(""), "publish", "-passphrase", "a-long-secret", "-pbkeylen", "32", addr)
	c.next(t, "connected")
	ln.next(t, "connected")
	if code, e := c.wait(t); code != 0 || e.Reason != "eof" {
		t.Errorf("right passphrase: exit %v, last event %+v", code, e)
	}
}

// TestLatencies checks the TSBPD delays the handshake agrees: each side
// receives with the larger of its own receive latency and the peer latency
// the other asks for.
func TestLatencies(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	keep, _ := io.Pipe()
	ln, addr := listenOn(t, ctx, keep, "-latency", "100")

	c := start(t, ctx, nil, "play", "-rcvlatency", "300", "-peerlatency", "250", addr)
	if e := c.next(t, "connected"); e.MsRcvTsbPdDelay != 300 || e.MsSndTsbPdDelay != 250 {
		t.Errorf("caller TSBPD recv %v send %v, want 300 and 250", e.MsRcvTsbPdDelay, e.MsSndTsbPdDelay)
	}
	if e := ln.next(t, "connected"); e.MsRcvTsbPdDelay != 250 || e.MsSndTsbPdDelay != 300 {
		t.Errorf("listener TSBPD recv %v send %v, want 250 and 300", e.MsRcvTsbPdDelay, e.MsSndTsbPdDelay)
	}
}

// TestOptions calls a listener with every option SRS takes, and checks that
// a value libsrt refuses fails with the option's name.
func TestOptions(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	keep, _ := io.Pipe()
	all := []string{"-mss", "1400", "-payloadsize", "1316", "-maxbw", "10000000", "-sndbuf", "2000000",
		"-rcvbuf", "2000000", "-latency", "80", "-conntimeo", "1000", "-peeridletimeo", "2000",
		"-tsbpdmode", "on", "-tlpktdrop", "off"}
	ln, addr := listenOn(t, ctx, keep, all...)

	c := start(t, ctx, strings.NewReader(""), append(append([]string{"publish", "-streamid", "s"}, all...), addr)...)
	c.next(t, "connected")
	ln.next(t, "connected")
	if code, e := c.wait(t); code != 0 || e.Reason != "eof" {
		t.Errorf("every option: exit %v, last event %+v", code, e)
	}

	for _, tc := range []struct {
		flags []string
		want  string
	}{
		{[]string{"-payloadsize", "2000"}, "payloadsize"},
		{[]string{"-pbkeylen", "20", "-passphrase", "a-long-secret"}, "pbkeylen"},
		{[]string{"-passphrase", "short"}, "passphrase"},
	} {
		c := start(t, ctx, nil, append(append([]string{"play"}, tc.flags...), addr)...)
		if code, e := c.wait(t); code != 1 || e.Event != "error" || !strings.Contains(e.Error, tc.want) {
			t.Errorf("%v: exit %v, last event %+v; want an error naming %v", tc.flags, code, e, tc.want)
		}
	}
}

// TestConnectTimeout calls a port where no one listens: the handshake gives
// up after conntimeo, with no reject reason.
func TestConnectTimeout(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	t0 := time.Now()
	c := start(t, ctx, nil, "play", "-conntimeo", "300", "127.0.0.1:9")
	code, e := c.wait(t)
	if code != 1 || e.Event != "error" || e.Reject != 0 {
		t.Errorf("exit %v, last event %+v; want an error with no reject reason", code, e)
	}
	if d := time.Since(t0); d > 2*time.Second {
		t.Errorf("gave up after %v, want about 300ms", d)
	}
}

// TestSignal ends a player that waits for media when its context ends, as
// SIGINT and SIGTERM do.
func TestSignal(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	keep, _ := io.Pipe()
	ln, addr := listenOn(t, ctx, keep)

	pctx, stop := context.WithCancel(ctx)
	c := start(t, pctx, nil, "play", addr)
	c.next(t, "connected")
	stop()
	if code, e := c.wait(t); code != 0 || e.Reason != "signal" {
		t.Errorf("exit %v, last event %+v; want closed by the signal", code, e)
	}
	if code, e := ln.wait(t); code != 0 || e.Reason != "peer" {
		t.Errorf("listener exit %v, last event %+v; want closed by the peer", code, e)
	}
}

// TestUsage checks the command line errors.
func TestUsage(t *testing.T) {
	t.Parallel()
	for _, args := range [][]string{nil, {"nothing"}, {"play"}, {"play", "nohost"}, {"listen", "-nope", "1", ":1"}, {"play", "-tlpktdrop", "maybe", ":1"}} {
		var out bytes.Buffer
		if code := run(context.Background(), args, nil, io.Discard, &out); code != 2 {
			t.Errorf("%q: exit %v, want 2: %s", args, code, out.String())
		}
	}
}

// TestNegotiated checks the MSS and the flow window a caller gets: the smaller
// MSS of the two ends, and the listener's receiver buffer in packets of its
// MSS less the IP and UDP headers, 28 bytes.
func TestNegotiated(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	keep, _ := io.Pipe()
	ln, addr := listenOn(t, ctx, keep, "-mss", "1000", "-payloadsize", "940", "-rcvbuf", strconv.Itoa(500*972))

	c := start(t, ctx, nil, "play", "-mss", "1500", addr)
	if e := c.next(t, "connected"); e.MSS != 1000 || e.FlowWindow != 500 {
		t.Errorf("caller MSS %v flow window %v, want 1000 and 500", e.MSS, e.FlowWindow)
	}
	if e := ln.next(t, "connected"); e.MSS != 1000 {
		t.Errorf("listener MSS %v, want 1000", e.MSS)
	}
}
