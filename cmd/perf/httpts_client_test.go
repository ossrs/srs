package main

import (
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"
)

// The HTTP-TS player GETs /<app>/<stream>.ts and reports the H.264 frames of
// the chunked TS body in milliseconds from the first PTS, each found in the
// source; audio is reported at the newest video time.
func TestHTTPTSPlayerReportsFrames(t *testing.T) {
	tags := tsTestTags()
	ts, _ := muxAll(t, tags)
	gotPath := make(chan string, 1)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath <- r.URL.Path
		w.Header().Set("Content-Type", "video/MP2T")
		// Splits that cut TS packets, as a chunked body may.
		for b := ts; len(b) > 0; {
			n := min(len(b), 1000)
			w.Write(b[:n])
			w.(http.Flusher).Flush()
			b = b[n:]
		}
		<-r.Context().Done()
	}))
	defer server.Close()
	c, err := dialHTTPTS(strings.TrimPrefix(server.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	stop := make(chan struct{})
	report := make(chan rtmpMessage, 64)
	done := make(chan error, 1)
	go func() { done <- c.play("perf-run", stop, report) }()

	timeline := newSourceTimeline(tags)
	var video []uint32
	audio := 0
	deadline := time.After(3 * time.Second)
	for len(video) < 3 {
		select {
		case m := <-report:
			switch m.typeID {
			case 9:
				if len(video) == 0 && m.payload[0] != 0x17 {
					t.Errorf("first video frame %x is not a keyframe", m.payload[:2])
				}
				if at, ok := timeline.inLoop(m); !ok || at != m.timestamp {
					t.Errorf("video at %dms in the loop at %d, %v", m.timestamp, at, ok)
				}
				video = append(video, m.timestamp)
			case 8:
				if len(video) > 0 && m.timestamp != video[len(video)-1] {
					t.Errorf("audio at %dms, want the newest video time %dms", m.timestamp, video[len(video)-1])
				}
				audio++
			default:
				t.Errorf("message type %d", m.typeID)
			}
		case <-deadline:
			t.Fatalf("got video %v and %d audio, want 3 video frames", video, audio)
		}
	}
	if video[0] != 0 || video[1] != 40 || video[2] != 2000 {
		t.Errorf("video at %v, want [0 40 2000]", video)
	}
	if audio == 0 {
		t.Error("no audio reported")
	}
	if path := <-gotPath; path != "/live/perf-run.ts" {
		t.Errorf("path = %q", path)
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
}

// A stream SRS does not serve is an error, not a player that waits.
func TestHTTPTSPlayerRejectsMissingStream(t *testing.T) {
	server := httptest.NewServer(http.NotFoundHandler())
	defer server.Close()
	c, err := dialHTTPTS(strings.TrimPrefix(server.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	err = c.play("missing", make(chan struct{}), make(chan rtmpMessage, 1))
	if err == nil || !strings.Contains(err.Error(), "404") {
		t.Fatalf("err = %v, want a 404 error", err)
	}
}

// A body that is not TS is an error.
func TestHTTPTSPlayerRejectsBadStream(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(strings.Repeat("not a ts stream ", 40)))
	}))
	defer server.Close()
	c, err := dialHTTPTS(strings.TrimPrefix(server.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	if err := c.play("perf-run", make(chan struct{}), make(chan rtmpMessage, 1)); err == nil {
		t.Fatal("a body that is not TS was accepted")
	}
}

// The HTTP-TS scenario serves the stream from SRS's HTTP server on 8080, the
// port its players dial, with the .ts mount of http_remux.
func TestHTTPTSScenarioEnvServesTS(t *testing.T) {
	s, ok := scenarioByName("rtmp-ts")
	if !ok || s.Publisher != "rtmp" || s.Player != "ts" || s.segment != 0 {
		t.Fatalf("rtmp-ts = %+v, %v", s, ok)
	}
	data, err := os.ReadFile(filepath.Join("env", "rtmp-ts.env"))
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range []string{"SRS_RTMP_LISTEN=1935", "SRS_HTTP_SERVER_ENABLED=on", "SRS_HTTP_SERVER_LISTEN=" + httpTSPort, "SRS_VHOST_HTTP_REMUX_ENABLED=on", "SRS_VHOST_HTTP_REMUX_MOUNT=[vhost]/[app]/[stream].ts"} {
		if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
			t.Errorf("rtmp-ts.env has no %s", line)
		}
	}
}
