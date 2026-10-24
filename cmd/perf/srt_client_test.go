package main

import (
	"bytes"
	"sync"
	"testing"
	"time"

	gosrt "github.com/datarhei/gosrt"
)

// srtTestRelay is an SRT listener that relays what a publisher sends to every
// player, as SRS does for SRT to SRT, and records each caller's streamid.
type srtTestRelay struct {
	ln        gosrt.Listener
	mu        sync.Mutex
	streamIDs []string
	players   []gosrt.Conn
	joined    chan struct{}
}

func startSRTTestRelay(t *testing.T) *srtTestRelay {
	t.Helper()
	ln, err := gosrt.Listen("srt", "127.0.0.1:0", gosrt.DefaultConfig())
	if err != nil {
		t.Fatal(err)
	}
	r := &srtTestRelay{ln: ln, joined: make(chan struct{}, 16)}
	t.Cleanup(ln.Close)
	go func() {
		for {
			req, err := ln.Accept2()
			if err != nil {
				return
			}
			id := req.StreamId()
			conn, err := req.Accept()
			if err != nil {
				continue
			}
			r.mu.Lock()
			r.streamIDs = append(r.streamIDs, id)
			r.mu.Unlock()
			if bytes.HasSuffix([]byte(id), []byte("m=publish")) {
				go r.relay(conn)
				continue
			}
			r.mu.Lock()
			r.players = append(r.players, conn)
			r.mu.Unlock()
			r.joined <- struct{}{}
		}
	}()
	return r
}

func (r *srtTestRelay) relay(pub gosrt.Conn) {
	defer pub.Close()
	b := make([]byte, 1500)
	for {
		n, err := pub.Read(b)
		if err != nil {
			return
		}
		r.mu.Lock()
		for _, p := range r.players {
			p.Write(b[:n])
		}
		r.mu.Unlock()
	}
}

func TestSRTStreamID(t *testing.T) {
	if got := srtStreamID("live", "perf-run", "publish"); got != "#!::r=live/perf-run,m=publish" {
		t.Fatalf("streamid = %q", got)
	}
	if got := srtStreamID("live", "perf-run", "request"); got != "#!::r=live/perf-run,m=request" {
		t.Fatalf("streamid = %q", got)
	}
}

// The SRT publisher sends the source as TS in real time, and the SRT player
// reports each H.264 access unit as an FLV AVC payload in milliseconds from
// its first video frame, and each AAC frame at the newest video time. The
// source timeline finds the player's frames by their first slice, so its lag
// is measured like any other player's.
func TestSRTPublisherAndPlayer(t *testing.T) {
	relay := startSRTTestRelay(t)
	addr := relay.ln.Addr().String()
	tags := tsTestTags()
	stop := make(chan struct{})
	report := make(chan rtmpMessage, 64)
	c, err := dialSRT(addr, "live")
	if err != nil {
		t.Fatal(err)
	}
	played := make(chan error, 1)
	go func() { played <- c.play("perf-run", stop, report) }()
	select {
	case <-relay.joined:
	case <-time.After(5 * time.Second):
		t.Fatal("the player did not connect")
	}
	clock := &mediaClock{}
	published := make(chan error, 1)
	go func() { published <- publishSRT(addr, "live", "perf-run", tags, clock, stop) }()

	timeline := newSourceTimeline(tags)
	var got []rtmpMessage
	deadline := time.After(5 * time.Second)
	for len(got) < 8 {
		select {
		case m := <-report:
			got = append(got, m)
		case <-deadline:
			t.Fatalf("got %d messages, want 8: %+v", len(got), got)
		}
	}
	close(stop)
	for name, done := range map[string]chan error{"play": played, "publish": published} {
		select {
		case err := <-done:
			if err != nil {
				t.Errorf("%s: %v", name, err)
			}
		case <-time.After(3 * time.Second):
			t.Errorf("%s did not stop", name)
		}
	}

	// A video frame comes one frame late, after the audio sent with it (see
	// TestTSMuxDemuxRoundTrip); the 2040ms frame waits for the second loop's
	// first audio and IDR.
	want := []struct {
		typeID    byte
		timestamp uint32
		at        int64
	}{{8, 0, -1}, {9, 0, 0}, {8, 0, -1}, {9, 40, 40}, {9, 2000, 2000}, {8, 2000, -1}, {8, 2000, -1}, {9, 2040, 2040}}
	for i, w := range want {
		m := got[i]
		if m.typeID != w.typeID || m.timestamp != w.timestamp {
			t.Errorf("message %d = type %d at %dms, want type %d at %dms", i, m.typeID, m.timestamp, w.typeID, w.timestamp)
			continue
		}
		if w.at < 0 {
			continue
		}
		if at, ok := timeline.inLoop(m); !ok || int64(at) != w.at {
			t.Errorf("message %d in the loop at %d, %v; want %d", i, at, ok, w.at)
		}
	}
	if got[1].payload[0] != 0x17 {
		t.Errorf("first video frame %x is not a keyframe", got[1].payload[:2])
	}
	relay.mu.Lock()
	ids := append([]string(nil), relay.streamIDs...)
	relay.mu.Unlock()
	if len(ids) != 2 || ids[0] != "#!::r=live/perf-run,m=request" || ids[1] != "#!::r=live/perf-run,m=publish" {
		t.Errorf("streamids = %q", ids)
	}
	if clock.get() < 2040 {
		t.Errorf("publisher clock %d, want the last tag's 2040ms or later", clock.get())
	}
}

// A player that cannot reach SRT fails with an error, rather than waiting.
func TestSRTPlayerFailsWithoutServer(t *testing.T) {
	c, err := dialSRT("127.0.0.1:1", "live")
	if err != nil {
		t.Fatal(err)
	}
	c.timeout = 200 * time.Millisecond
	c.tries = 2
	start := time.Now()
	if err := c.play("perf-run", make(chan struct{}), make(chan rtmpMessage, 1)); err == nil {
		t.Fatal("play without a server succeeded")
	}
	if time.Since(start) > 3*time.Second {
		t.Fatalf("play took %v to fail", time.Since(start))
	}
}
