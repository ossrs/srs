package main

import (
	"bytes"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// SRS answers a new HLS player with a master playlist that names its session
// by hls_ctx, and the media playlist with that session's segment URLs.
func TestParsePlaylist(t *testing.T) {
	master, err := parsePlaylist([]byte("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1\n/live/perf-run.m3u8?hls_ctx=abc\n"))
	if err != nil || master.variant != "/live/perf-run.m3u8?hls_ctx=abc" || len(master.segments) != 0 {
		t.Fatalf("master = %+v, %v", master, err)
	}
	media, err := parsePlaylist([]byte("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:7\n#EXT-X-TARGETDURATION:10\n\n#EXTINF:10.000, no desc\nperf-run-7.ts?hls_ctx=abc\n#EXT-X-DISCONTINUITY\n#EXTINF:9.960, no desc\nperf-run-8.ts?hls_ctx=abc\n"))
	if err != nil || media.variant != "" || media.target != 10*time.Second {
		t.Fatalf("media = %+v, %v", media, err)
	}
	want := []hlsSegment{{seq: 7, duration: 10 * time.Second, uri: "perf-run-7.ts?hls_ctx=abc"}, {seq: 8, duration: 9960 * time.Millisecond, uri: "perf-run-8.ts?hls_ctx=abc"}}
	if len(media.segments) != len(want) {
		t.Fatalf("segments = %+v, want %+v", media.segments, want)
	}
	for i := range want {
		if media.segments[i] != want[i] {
			t.Errorf("segment %d = %+v, want %+v", i, media.segments[i], want[i])
		}
	}
	if _, err := parsePlaylist([]byte("<html>not found</html>")); err == nil {
		t.Fatal("a body that is not a playlist was accepted")
	}
}

// hlsTestServer serves a stream as SRS does with hls_ctx on: the master
// playlist, then the media playlist of the session, whose segments are GOPs
// of the TS test source. Before missing requests, the playlist is a 404, as
// before SRS reaps the first segment; the media playlist lists the first
// segment, and from its second request on, the second too.
type hlsTestServer struct {
	*httptest.Server
	mu       sync.Mutex
	missing  int
	media    int
	fetched  []string
	conns    atomic.Int32
	extinf   string
	segDelay time.Duration
	segments map[string][]byte
}

func newHLSTestServer(t *testing.T, missing int) *hlsTestServer {
	ts, starts := muxAll(t, tsTestTags())
	s := &hlsTestServer{missing: missing, extinf: "2.000", segments: map[string][]byte{
		"/live/perf-run-1.ts": ts[starts[3]:starts[7]],
		"/live/perf-run-2.ts": ts[starts[7]:],
	}}
	s.Server = httptest.NewUnstartedServer(http.HandlerFunc(s.serve))
	s.Server.Config.ConnState = func(_ net.Conn, state http.ConnState) {
		if state == http.StateNew {
			s.conns.Add(1)
		}
	}
	s.Start()
	t.Cleanup(s.Close)
	return s
}

func (s *hlsTestServer) serve(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if r.URL.Path == "/live/perf-run.m3u8" && s.missing > 0 {
		s.missing--
		http.NotFound(w, r)
		return
	}
	ctx := r.URL.Query().Get("hls_ctx")
	if r.URL.Path == "/live/perf-run.m3u8" && ctx == "" {
		w.Write([]byte("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1\n/live/perf-run.m3u8?hls_ctx=abc\n"))
		return
	}
	if r.URL.Path == "/live/perf-run.m3u8" && ctx == "abc" {
		s.media++
		playlist := "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:1\n#EXT-X-TARGETDURATION:1\n#EXTINF:" + s.extinf + ", no desc\nperf-run-1.ts?hls_ctx=abc\n"
		if s.media > 1 {
			playlist += "#EXTINF:" + s.extinf + ", no desc\nperf-run-2.ts?hls_ctx=abc\n"
		}
		w.Write([]byte(playlist))
		return
	}
	segment, ok := s.segments[r.URL.Path]
	if !ok || ctx != "abc" {
		http.NotFound(w, r)
		return
	}
	s.fetched = append(s.fetched, r.URL.Path)
	time.Sleep(s.segDelay)
	w.Write(segment)
}

func (s *hlsTestServer) fetchedSegments() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]string(nil), s.fetched...)
}

// playHLS plays the test server's stream until the reported messages are n,
// or the player ends.
func playHLS(t *testing.T, s *hlsTestServer, n int) ([]rtmpMessage, error) {
	t.Helper()
	c, err := dialHLS(strings.TrimPrefix(s.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	stop := make(chan struct{})
	report := make(chan rtmpMessage, 16)
	done := make(chan error, 1)
	go func() { done <- c.play("perf-run", stop, report) }()
	var got []rtmpMessage
	for len(got) < n {
		select {
		case m := <-report:
			got = append(got, m)
		case err := <-done:
			return got, err
		case <-time.After(5 * time.Second):
			t.Fatalf("got %d of %d messages", len(got), n)
		}
	}
	close(stop)
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("stopped player returned %v", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("player did not stop")
	}
	return got, nil
}

// A player follows the master playlist to its session, skips the segments
// that were there when it joined, and fetches each new one whole on one
// keep-alive connection. It reports the segment's H.264 frames, the last one
// at the segment's end, timed from its first PTS, its AAC PES at the newest
// video time, then the segment itself.
func TestHLSPlayerPlaysNewSegments(t *testing.T) {
	s := newHLSTestServer(t, 0)
	got, err := playHLS(t, s, 4)
	if err != nil {
		t.Fatal(err)
	}
	want := []struct {
		typeID    byte
		timestamp uint32
	}{{9, 0}, {8, 0}, {9, 40}, {segmentMessage, 40}}
	for i, w := range want {
		if got[i].typeID != w.typeID || got[i].timestamp != w.timestamp {
			t.Errorf("message %d = type %d at %d, want type %d at %d", i, got[i].typeID, got[i].timestamp, w.typeID, w.timestamp)
		}
	}
	idr, _ := firstSlice(tsTestTags()[7].payload)
	if slice, ok := firstSlice(got[0].payload); !ok || !bytes.Equal(slice, idr) {
		t.Fatalf("first frame = %x, want the IDR of the second GOP", got[0].payload)
	}
	if fetched := s.fetchedSegments(); len(fetched) != 1 || fetched[0] != "/live/perf-run-2.ts" {
		t.Fatalf("fetched %v, want only the new segment", fetched)
	}
	if n := s.conns.Load(); n != 1 {
		t.Fatalf("player opened %d connections, want 1 kept alive", n)
	}
}

// A player that joins before SRS has its first segment waits for the
// playlist; then every segment in it is new.
func TestHLSPlayerJoinsBeforeTheStream(t *testing.T) {
	s := newHLSTestServer(t, 2)
	// The IDR's PES ends at the next video PES, after the AAC sent with it.
	got, err := playHLS(t, s, 2)
	if err != nil {
		t.Fatal(err)
	}
	if got[0].typeID != 8 || got[1].typeID != 9 {
		t.Fatalf("messages = %+v, want AAC, then the IDR", got)
	}
	if fetched := s.fetchedSegments(); len(fetched) == 0 || fetched[0] != "/live/perf-run-1.ts" {
		t.Fatalf("fetched %v, want the first segment", fetched)
	}
}

// A segment that takes longer to fetch than its duration fails the player:
// it would stall a real one.
func TestHLSPlayerFailsSlowSegment(t *testing.T) {
	s := newHLSTestServer(t, 1)
	s.extinf, s.segDelay = "0.100", 300*time.Millisecond
	if _, err := playHLS(t, s, 100); err == nil || !strings.Contains(err.Error(), "longer than") {
		t.Fatalf("err = %v, want a slow segment error", err)
	}
}

// A segment SRS does not serve fails the player.
func TestHLSPlayerFailsMissingSegment(t *testing.T) {
	s := newHLSTestServer(t, 1)
	s.segments = nil
	if _, err := playHLS(t, s, 100); err == nil || !strings.Contains(err.Error(), "404") {
		t.Fatalf("err = %v, want a 404 error", err)
	}
}

// A video PES has no length and ends at the next one; at a segment's end,
// flush ends the last one.
func TestTSDemuxerFlushEndsTheLastFrame(t *testing.T) {
	ts, starts := muxAll(t, tsTestTags())
	var video []int64
	d := newTSDemuxer(func(pts int64, _ [][]byte) { video = append(video, pts) }, func(int64, []byte) {})
	if err := d.write(ts[starts[7]:]); err != nil {
		t.Fatal(err)
	}
	if len(video) != 1 {
		t.Fatalf("before flush: video at %v, want only the IDR", video)
	}
	d.flush()
	if len(video) != 2 || video[1] != 2040*90 {
		t.Fatalf("after flush: video at %v, want the IDR and the frame at 2040ms", video)
	}
	d.flush()
	if len(video) != 2 {
		t.Fatalf("a second flush reported %v again", video)
	}
}

// HLS is segmented (D30): a player fails when no new segment comes for three
// segment durations of the shipped 10s; its lag is seconds by design, so it
// is reported, not judged. A ramp step is long enough to see three segments.
// The other scenarios keep the 5s gap and the 2s lag rule.
func TestScenarioHealthRules(t *testing.T) {
	for _, tt := range []struct {
		name         string
		gap          time.Duration
		lag          int64
		rampSettle   int
		rampWindow   int
		smokeSeconds int
	}{{"rtmp-rtmp", 5 * time.Second, 2000, 2, 3, 10}, {"srt-srt", 5 * time.Second, 2000, 2, 3, 10}, {"rtmp-hls", 30 * time.Second, 0, 20, 30, 30}, {"rtmp-dash", 30 * time.Second, 0, 20, 30, 30}} {
		s, ok := scenarioByName(tt.name)
		if !ok {
			t.Fatalf("no scenario %s", tt.name)
		}
		settle, window := s.stepTiming("ramp")
		if s.gap() != tt.gap || s.lagLimit() != tt.lag || settle != tt.rampSettle || window != tt.rampWindow || s.smokeSeconds() != tt.smokeSeconds {
			t.Errorf("%s: gap %v lag %d ramp %d+%d smoke %d, want %v %d %d+%d %d", tt.name, s.gap(), s.lagLimit(), settle, window, s.smokeSeconds(), tt.gap, tt.lag, tt.rampSettle, tt.rampWindow, tt.smokeSeconds)
		}
		for _, mode := range []string{"hold", "profile"} {
			gotSettle, gotWindow := s.stepTiming(mode)
			wantSettle, wantWindow := stepTiming(mode)
			if gotSettle != wantSettle || gotWindow != wantWindow {
				t.Errorf("%s %s: %d+%d, want the default %d+%d", tt.name, mode, gotSettle, gotWindow, wantSettle, wantWindow)
			}
		}
	}
	s, _ := scenarioByName("rtmp-hls")
	if s.Publisher != "rtmp" || s.Player != "hls" || s.Unit != "clients" || s.rampStart() != 100 || s.rampCap() != defaultClientCap {
		t.Fatalf("rtmp-hls = %+v", s)
	}
	if cmd, err := parseCommand([]string{"run", "-scenario", "rtmp-hls"}); err != nil || cmd.settle != 20 || cmd.window != 30 {
		t.Fatalf("rtmp-hls ramp = %+v, %v, want 20+30", cmd, err)
	}
}

// An HLS step is healthy with seconds of lag and seconds with no frame, since
// frames come a segment at a time; a failed player or no frame in three
// segment durations fails it.
func TestSegmentedRampDecision(t *testing.T) {
	s, _ := scenarioByName("rtmp-hls")
	seconds := func(n int, frames int64, lag int64) []sample {
		samples := make([]sample, n)
		for i := range samples {
			samples[i] = sample{SRSCPUPercent: 10, LagMillis: lag}
		}
		samples[n-1].Frames = frames
		return samples
	}
	if got, _ := evaluateRampStep(s, 100, 0, seconds(29, 1000, 15000), defaultClientCap); !got.Health.Healthy {
		t.Fatalf("29s without frames and 15s lag: %+v, want healthy", got.Health)
	}
	if got, _ := evaluateRampStep(s, 100, 0, append(seconds(30, 0, 0), sample{Frames: 1}), defaultClientCap); got.Health.Healthy {
		t.Fatal("30s without frames was healthy")
	}
	failed := seconds(5, 1000, 0)
	failed[2].Failures = 1
	if got, _ := evaluateRampStep(s, 100, 0, failed, defaultClientCap); got.Health.Healthy {
		t.Fatal("a failed HLS player was healthy")
	}
}

// A player's gap rule is its scenario's: an HLS player 10s after its last
// segment is fine, a stream player is not. A segment is counted, not a frame.
func TestWorkerPlayersGapAndSegments(t *testing.T) {
	stream := newWorkerPlayers(1, nil)
	segmented := newWorkerPlayers(1, nil)
	segmented.gap = 30 * time.Second
	for _, w := range []*workerPlayers{stream, segmented} {
		w.received(0, rtmpMessage{typeID: 9, payload: []byte{0x17, 1}})
		w.received(0, rtmpMessage{typeID: segmentMessage})
		w.last[0].Store(time.Now().Add(-10 * time.Second).UnixNano())
	}
	if got := stream.sample(0, false); got.Failures != 1 || got.Frames != 1 || got.Segments != 1 {
		t.Fatalf("stream sample = %+v, want 1 failure, 1 frame, 1 segment", got)
	}
	if got := segmented.sample(0, false); got.Failures != 0 || got.Frames != 1 || got.Segments != 1 {
		t.Fatalf("segmented sample = %+v, want no failure, 1 frame, 1 segment", got)
	}
}

// The HLS scenario writes HLS with the shipped defaults and serves it from
// SRS's HTTP server on 8080, the port its players dial.
func TestHLSScenarioEnvServesHLS(t *testing.T) {
	data, err := os.ReadFile(filepath.Join("env", "rtmp-hls.env"))
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range []string{"SRS_RTMP_LISTEN=1935", "SRS_HTTP_SERVER_ENABLED=on", "SRS_HTTP_SERVER_LISTEN=" + hlsPort, "SRS_VHOST_HLS_ENABLED=on"} {
		if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
			t.Errorf("rtmp-hls.env has no %s", line)
		}
	}
	if regexp.MustCompile(`(?m)^SRS_VHOST_HLS_HLS_`).Match(data) {
		t.Error("rtmp-hls.env changes an hls setting; D30 takes the shipped defaults")
	}
}
