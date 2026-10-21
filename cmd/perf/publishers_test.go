package main

import (
	"io"
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"sort"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/pion/webrtc/v4"
)

// The publisher scenarios count publishers, each on its own stream, with no
// player; many-streams counts streams, each with one RTMP publisher and two
// RTMP players. The publisher ramps start at 10 (D10); WHIP stops at the RTC
// cap, and many-streams where its three connections a stream reach the TCP cap.
func TestPublisherAndStreamScenarios(t *testing.T) {
	for _, tt := range []struct {
		name, publisher, player, unit string
		start, cap                    int
	}{
		{"rtmp-pub", "rtmp", "", "publishers", 10, defaultClientCap},
		{"whip-pub", "whip", "", "publishers", 10, 2000},
		{"many-streams", "rtmp", "rtmp", "streams", 10, defaultClientCap / 3},
	} {
		s, ok := scenarioByName(tt.name)
		if !ok {
			t.Fatalf("no scenario %s", tt.name)
		}
		if s.Name != tt.name || s.Publisher != tt.publisher || s.Player != tt.player || s.Unit != tt.unit || s.rampStart() != tt.start || s.rampCap() != tt.cap {
			t.Errorf("%s = %+v, ramp %d to %d", tt.name, s, s.rampStart(), s.rampCap())
		}
		if s.gap() != 5*time.Second || s.lagLimit() != 2000 {
			t.Errorf("%s: gap %v lag %d, want the stream rules", tt.name, s.gap(), s.lagLimit())
		}
		if _, err := parseCommand([]string{"run", "-scenario", tt.name, "-no-publisher"}); err == nil {
			t.Errorf("%s: -no-publisher was accepted", tt.name)
		}
	}
}

// rtmp-pub and many-streams need only RTMP; whip-pub serves WHIP like
// whip-whep, with rtc_to_rtmp at its default, off.
func TestPublisherScenarioEnvs(t *testing.T) {
	rtc := []string{"SRS_HTTP_API_ENABLED=on", "SRS_HTTP_API_LISTEN=" + rtcAPIPort, "SRS_RTC_SERVER_ENABLED=on", "SRS_RTC_SERVER_LISTEN=" + rtcPort, "SRS_RTC_SERVER_CANDIDATE=127.0.0.1", "SRS_VHOST_RTC_ENABLED=on"}
	for _, tt := range []struct {
		name   string
		has    []string
		hasNot []string
	}{
		{"rtmp-pub", []string{"SRS_RTMP_LISTEN=1935"}, []string{"SRS_HTTP_SERVER_ENABLED", "SRS_RTC_SERVER_ENABLED"}},
		{"many-streams", []string{"SRS_RTMP_LISTEN=1935"}, []string{"SRS_HTTP_SERVER_ENABLED", "SRS_RTC_SERVER_ENABLED"}},
		{"whip-pub", rtc, []string{"SRS_VHOST_RTC_RTMP_TO_RTC", "SRS_VHOST_RTC_RTC_TO_RTMP"}},
	} {
		data, err := os.ReadFile(filepath.Join("env", tt.name+".env"))
		if err != nil {
			t.Fatal(err)
		}
		for _, line := range tt.has {
			if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
				t.Errorf("%s.env has no %s", tt.name, line)
			}
		}
		for _, name := range tt.hasNot {
			if regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(name) + `=`).Match(data) {
				t.Errorf("%s.env sets %s", tt.name, name)
			}
		}
	}
}

// Each worker's clients start at its own offset, so every publisher of a step
// has its own stream.
func TestClientOffsets(t *testing.T) {
	if got := clientOffsets(splitClients(10, 3)); !reflect.DeepEqual(got, []int{0, 4, 7}) {
		t.Fatalf("offsets of 10 clients on 3 workers = %v", got)
	}
	if got := streamName("perf-1", 7); got != "perf-1-7" {
		t.Fatalf("stream name = %q", got)
	}
}

// The clock records the pace's start and each tag sent, so a publisher's lag
// is how far its newest tag is behind real time.
func TestMediaClockBehind(t *testing.T) {
	c := &mediaClock{}
	if _, ok := c.behind(time.Now()); ok {
		t.Fatal("a clock that never started has a lag")
	}
	now := time.Now()
	c.begin(now.Add(-3 * time.Second))
	c.sent(rtmpMessage{timestamp: 500, typeID: 9, payload: []byte{1, 2, 3}})
	if got, ok := c.behind(now); !ok || got < 2490 || got > 2510 {
		t.Fatalf("behind = %d, %v, want 2500", got, ok)
	}
	frames, bytes, last := c.take()
	if frames != 1 || bytes != 3 || time.Since(last) > time.Second || c.get() != 500 {
		t.Fatalf("take = %d frames, %d bytes, last %v, newest %d", frames, bytes, last, c.get())
	}
	if frames, bytes, _ := c.take(); frames != 0 || bytes != 0 {
		t.Fatalf("second take = %d frames, %d bytes, want 0", frames, bytes)
	}
}

// pacedPublish is a fake publisher that paces the tags with no server and
// records its streams.
func pacedPublish(tags []rtmpMessage, streams *sync.Map) func(string, *mediaClock, <-chan struct{}) error {
	return func(stream string, clock *mediaClock, stop <-chan struct{}) error {
		streams.Store(stream, true)
		return paceSource(tags, clock, stop, func(rtmpMessage) error { return nil })
	}
}

func storedKeys(m *sync.Map) []string {
	var keys []string
	m.Range(func(k, _ any) bool { keys = append(keys, k.(string)); return true })
	sort.Strings(keys)
	return keys
}

// N publishers each publish their own stream and count the tags they send;
// on pace, they have no lag and no failure.
func TestPublisherClientsPublishOwnStreams(t *testing.T) {
	var streams sync.Map
	w := startPublisherClients(pacedPublish(cbrLikeTags(), &streams), 3, func(i int) string { return streamName("perf-1", 5+i) }, 5*time.Second)
	time.Sleep(1200 * time.Millisecond)
	got := w.sample(nil, true)
	w.close()
	if got.Frames < 3*50 || got.Bytes <= 0 || got.Failures != 0 || got.LagMillis > 100 {
		t.Fatalf("sample = %+v", got)
	}
	if keys := storedKeys(&streams); !reflect.DeepEqual(keys, []string{"perf-1-5", "perf-1-6", "perf-1-7"}) {
		t.Fatalf("streams = %v", keys)
	}
}

// A publisher that falls behind its pace, as when SRS stops reading, lags.
func TestPublisherBehindPaceLags(t *testing.T) {
	w := startPublisherClients(func(stream string, clock *mediaClock, stop <-chan struct{}) error {
		clock.begin(time.Now().Add(-3 * time.Second))
		clock.sent(rtmpMessage{timestamp: 500, typeID: 9, payload: []byte{1}})
		<-stop
		return nil
	}, 1, func(i int) string { return "s" }, 5*time.Second)
	defer w.close()
	time.Sleep(50 * time.Millisecond)
	if got := w.sample(nil, false); got.LagMillis < 2400 || got.LagMillis > 2700 || got.Failures != 0 {
		t.Fatalf("sample = %+v, want 2.5s lag", got)
	}
}

// A publisher whose connect times out tries again; one that ends with any
// other error, or before stop, fails; one that never sends fails at the
// step's end.
func TestPublisherClientsFailures(t *testing.T) {
	var calls atomic.Int32
	retried := startPublisherClients(func(stream string, clock *mediaClock, stop <-chan struct{}) error {
		if calls.Add(1) < 3 {
			return timeoutError{}
		}
		return paceSource(cbrLikeTags(), clock, stop, func(rtmpMessage) error { return nil })
	}, 1, func(i int) string { return "s" }, 5*time.Second)
	time.Sleep(300 * time.Millisecond)
	if got := retried.sample(nil, true); got.Failures != 0 || got.Frames == 0 || calls.Load() != 3 {
		t.Fatalf("retried sample = %+v after %d calls", got, calls.Load())
	}
	retried.close()

	ended := startPublisherClients(func(string, *mediaClock, <-chan struct{}) error { return io.EOF }, 2, func(i int) string { return "s" }, 5*time.Second)
	time.Sleep(50 * time.Millisecond)
	if got := ended.sample(nil, false); got.Failures != 2 {
		t.Fatalf("ended sample = %+v, want 2 failures", got)
	}
	ended.close()

	silent := startPublisherClients(func(_ string, _ *mediaClock, stop <-chan struct{}) error { <-stop; return nil }, 1, func(i int) string { return "s" }, 5*time.Second)
	if got := silent.sample(nil, false); got.Failures != 0 {
		t.Fatalf("silent mid-step sample = %+v", got)
	}
	if got := silent.sample(nil, true); got.Failures != 1 {
		t.Fatalf("silent final sample = %+v, want 1 failure", got)
	}
	silent.close()
}

// fakeStreamPlayer plays a stream by pacing the source itself from when it
// joins, and records the streams it played.
type fakeStreamPlayer struct {
	tags   []rtmpMessage
	played *sync.Map
}

func (p *fakeStreamPlayer) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	n, _ := p.played.LoadOrStore(stream, new(atomic.Int32))
	n.(*atomic.Int32).Add(1)
	return paceSource(p.tags, &mediaClock{}, stop, func(m rtmpMessage) error {
		select {
		case report <- m:
		case <-stop:
		}
		return nil
	})
}

// Each stream has one publisher and two players of that stream, which start
// once the publisher sends; a player's lag is against its own stream's
// publisher.
func TestStreamsPlayEachStream(t *testing.T) {
	tags := cbrLikeTags()
	var published, played sync.Map
	dial := func() (player, error) { return &fakeStreamPlayer{tags: tags, played: &played}, nil }
	w := startStreams(pacedPublish(tags, &published), dial, 3, func(i int) string { return streamName("perf-2", i) }, newSourceTimeline(tags), 5*time.Second)
	time.Sleep(1500 * time.Millisecond)
	got := w.sample(nil, true)
	w.close()
	if got.Frames < 6*30 || got.Failures != 0 || got.LagMillis > 300 {
		t.Fatalf("sample = %+v", got)
	}
	want := []string{"perf-2-0", "perf-2-1", "perf-2-2"}
	if keys := storedKeys(&published); !reflect.DeepEqual(keys, want) {
		t.Fatalf("published = %v", keys)
	}
	if keys := storedKeys(&played); !reflect.DeepEqual(keys, want) {
		t.Fatalf("played = %v", keys)
	}
	played.Range(func(k, v any) bool {
		if n := v.(*atomic.Int32).Load(); n != 2 {
			t.Errorf("%s has %d players, want 2", k, n)
		}
		return true
	})
}

// A stream whose publisher fails has no player, and both of its players fail
// with it at the step's end.
func TestStreamsFailWithTheirPublisher(t *testing.T) {
	tags := cbrLikeTags()
	var played sync.Map
	dial := func() (player, error) { return &fakeStreamPlayer{tags: tags, played: &played}, nil }
	w := startStreams(func(string, *mediaClock, <-chan struct{}) error { return io.EOF }, dial, 1, func(i int) string { return "s" }, newSourceTimeline(tags), 5*time.Second)
	time.Sleep(50 * time.Millisecond)
	got := w.sample(nil, true)
	w.close()
	if got.Failures != 3 {
		t.Fatalf("sample = %+v, want the publisher and both players failed", got)
	}
	if keys := storedKeys(&played); len(keys) != 0 {
		t.Fatalf("players played %v", keys)
	}
}

// A WHIP publisher whose peer connection fails while it sends ends with an
// error instead of sending into a dead session.
func TestRTCSessionReportsFailure(t *testing.T) {
	s, err := newRTCSession()
	if err != nil {
		t.Fatal(err)
	}
	defer s.pc.Close()
	if err := s.ended(); err != nil {
		t.Fatalf("new session ended: %v", err)
	}
	s.failed <- webrtc.PeerConnectionStateFailed
	if err := s.ended(); err == nil {
		t.Fatal("failed session did not end")
	}
}
