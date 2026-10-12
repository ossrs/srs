// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package dash

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// srsMPD is an MPD like SRS writes: a dynamic presentation, the stream name
// as the BaseURL, and a timeline in milliseconds for audio and video.
const srsMPD = `<?xml version="1.0" encoding="utf-8"?>
<MPD profiles="urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple"
    ns1:schemaLocation="urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd"
    xmlns="urn:mpeg:dash:schema:mpd:2011" xmlns:ns1="http://www.w3.org/2001/XMLSchema-instance"
    type="dynamic"
    minimumUpdatePeriod="PT150.000S"
    timeShiftBufferDepth="PT50.000S"
    availabilityStartTime="2026-10-10T12:00:00Z"
    publishTime="2026-10-10T12:00:30Z"
    minBufferTime="PT20.000S" >
    <BaseURL>livestream/</BaseURL>
    <Period start="PT0S">
        <AdaptationSet mimeType="audio/mp4" segmentAlignment="true" startWithSAP="1">
            <Representation id="audio" bandwidth="48000" codecs="mp4a.40.2">
                <SegmentTemplate initialization="$RepresentationID$-init.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="3" timescale="1000">
                    <SegmentTimeline>
                        <S t="20000" d="10007" />
                        <S t="30007" d="9984" />
                    </SegmentTimeline>
                </SegmentTemplate>
            </Representation>
        </AdaptationSet>
        <AdaptationSet mimeType="video/mp4" segmentAlignment="true" startWithSAP="1">
            <Representation id="video" bandwidth="800000" codecs="avc1.64001e" width="768" height="320">
                <SegmentTemplate initialization="$RepresentationID$-init.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="3" timescale="1000">
                    <SegmentTimeline>
                        <S t="20000" d="10000" />
                        <S t="30000" d="10000" />
                    </SegmentTimeline>
                </SegmentTemplate>
            </Representation>
        </AdaptationSet>
    </Period>
</MPD>
`

func mustURL(t *testing.T, s string) *url.URL {
	t.Helper()
	u, err := url.Parse(s)
	if err != nil {
		t.Fatal(err)
	}
	return u
}

func TestParseSRS(t *testing.T) {
	m, err := ParseMPD([]byte(srsMPD))
	if err != nil {
		t.Fatal(err)
	}
	if m.Type != "dynamic" || m.BaseURL != "livestream/" || len(m.Periods) != 1 || len(m.Periods[0].AdaptationSets) != 2 {
		t.Fatalf("MPD %+v", m)
	}
	v := m.Periods[0].AdaptationSets[1].Representations[0]
	if v.ID != "video" || v.Bandwidth != 800000 || v.Codecs != "avc1.64001e" || v.Width != 768 || v.Height != 320 {
		t.Errorf("video representation %+v", v)
	}

	streams, err := m.Streams(mustURL(t, "http://127.0.0.1:8080/live/livestream.mpd"))
	if err != nil {
		t.Fatal(err)
	}
	if len(streams) != 2 {
		t.Fatalf("%v streams, want 2", len(streams))
	}
	a, vs := streams[0], streams[1]
	if a.Kind != "audio" || a.Init != "http://127.0.0.1:8080/live/livestream/audio-init.mp4" {
		t.Errorf("audio stream %+v", a)
	}
	if vs.Kind != "video" || vs.Init != "http://127.0.0.1:8080/live/livestream/video-init.mp4" {
		t.Errorf("video stream %+v", vs)
	}
	want := []SegmentURL{
		{Number: 3, Time: 20000, Duration: 10000, URL: "http://127.0.0.1:8080/live/livestream/video-3.m4s"},
		{Number: 4, Time: 30000, Duration: 10000, URL: "http://127.0.0.1:8080/live/livestream/video-4.m4s"},
	}
	if len(vs.Segments) != 2 || vs.Segments[0] != want[0] || vs.Segments[1] != want[1] {
		t.Errorf("video segments %+v, want %+v", vs.Segments, want)
	}
	if len(a.Segments) != 2 || a.Segments[1].Time != 30007 || a.Segments[1].Duration != 9984 {
		t.Errorf("audio segments %+v", a.Segments)
	}
}

// TestParseFFmpeg parses an MPD like ffmpeg writes: the content type on the
// adaptation set, the MIME type on the representation, a timeline with
// repeats and without times after the first, and a padded number.
func TestParseFFmpeg(t *testing.T) {
	text := `<?xml version="1.0" encoding="utf-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT6.2S">
	<Period id="0" start="PT0.0S">
		<AdaptationSet id="0" contentType="video">
			<Representation id="0" mimeType="video/mp4" codecs="avc1.64001e" bandwidth="143089" width="768" height="320">
				<SegmentTemplate timescale="16000" initialization="init-stream$RepresentationID$.m4s" media="chunk-stream$RepresentationID$-$Number%05d$.m4s" startNumber="1">
					<SegmentTimeline>
						<S t="368" d="32000" r="2" />
						<S d="3200" />
					</SegmentTimeline>
				</SegmentTemplate>
			</Representation>
		</AdaptationSet>
	</Period>
</MPD>`
	m, err := ParseMPD([]byte(text))
	if err != nil {
		t.Fatal(err)
	}
	streams, err := m.Streams(mustURL(t, "http://h/d/index.mpd"))
	if err != nil {
		t.Fatal(err)
	}
	if len(streams) != 1 || streams[0].Kind != "video" || streams[0].Init != "http://h/d/init-stream0.m4s" {
		t.Fatalf("streams %+v", streams)
	}
	var got []string
	for _, s := range streams[0].Segments {
		got = append(got, strings.TrimPrefix(s.URL, "http://h/d/"))
	}
	if want := "chunk-stream0-00001.m4s chunk-stream0-00002.m4s chunk-stream0-00003.m4s chunk-stream0-00004.m4s"; strings.Join(got, " ") != want {
		t.Errorf("segments %v, want %v", got, want)
	}
	if s := streams[0].Segments[3]; s.Number != 4 || s.Time != 368+3*32000 || s.Duration != 3200 {
		t.Errorf("last segment %+v", s)
	}
	if streams[0].Timescale != 16000 {
		t.Errorf("timescale %v, want 16000", streams[0].Timescale)
	}
}

func TestExpand(t *testing.T) {
	cases := []struct {
		template, want string
	}{
		{"$RepresentationID$-$Number$.m4s", "v1-7.m4s"},
		{"seg-$Number%05d$.m4s", "seg-00007.m4s"},
		{"t-$Time$-$Bandwidth$.m4s", "t-90000-800000.m4s"},
		{"a$$b.m4s", "a$b.m4s"},
	}
	for _, tc := range cases {
		if got := Expand(tc.template, "v1", 800000, 7, 90000); got != tc.want {
			t.Errorf("Expand(%q) = %q, want %q", tc.template, got, tc.want)
		}
	}
}

func TestParseErrors(t *testing.T) {
	cases := []struct {
		name, text, want string
	}{
		{"NotXML", "<MPD", "MPD"},
		{"NoTemplate", `<MPD><Period><AdaptationSet mimeType="video/mp4"><Representation id="v"/></AdaptationSet></Period></MPD>`, "SegmentTemplate"},
		{"NoTimeline", `<MPD><Period><AdaptationSet mimeType="video/mp4"><Representation id="v"><SegmentTemplate media="$Number$.m4s" duration="10"/></Representation></AdaptationSet></Period></MPD>`, "SegmentTimeline"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			m, err := ParseMPD([]byte(tc.text))
			if err == nil {
				_, err = m.Streams(mustURL(t, "http://h/a.mpd"))
			}
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Errorf("error %v, want it to contain %q", err, tc.want)
			}
		})
	}
}

// serve serves dir, with handlers by path first, and counts the segment
// requests.
func serve(t *testing.T, dir string, handlers map[string]http.HandlerFunc) (*httptest.Server, *atomic.Int32) {
	t.Helper()
	var segments atomic.Int32
	files := http.FileServer(http.Dir(dir))
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.Contains(r.URL.Path, "chunk") {
			segments.Add(1)
		}
		if h, ok := handlers[r.URL.Path]; ok {
			h(w, r)
			return
		}
		files.ServeHTTP(w, r)
	}))
	t.Cleanup(s.Close)
	return s, &segments
}

// TestPlayHTTP plays the test file as a static DASH presentation of a video
// and an audio representation, to the end.
func TestPlayHTTP(t *testing.T) {
	dir := media.RemuxDir(t, media.H264HighAAC, "dash", "index.mpd", "-seg_duration", "2")
	s, _ := serve(t, dir, nil)
	r, err := PlayHTTP(context.Background(), nil, s.URL+"/index.mpd", report.Until{})
	if err != nil {
		t.Fatal(err)
	}
	if r.Video == nil || r.Audio == nil {
		t.Fatalf("report %v", r)
	}
	v, a := r.Video, r.Audio
	if v.Codec != "h264" || v.Profile != "High" || v.Width != 768 || v.Height != 320 || v.SequenceHeaders != 1 {
		t.Errorf("video %v", r)
	}
	if v.Frames != 750 || v.Keyframes != 30 {
		t.Errorf("%v frames and %v keyframes, want 750 and 30", v.Frames, v.Keyframes)
	}
	if a.Codec != "aac" || a.SampleRate != 44100 || a.Channels != 2 || a.SequenceHeaders != 1 {
		t.Errorf("audio %v", r)
	}
	if err := r.CheckTimestamps(100 * time.Millisecond); err != nil {
		t.Error(err)
	}
	if r.AVSync > 100*time.Millisecond {
		t.Errorf("A/V sync %v, want the segments of both representations merged", r.AVSync)
	}
}

// TestPlayHTTPUntil stops after the segments that meet the condition.
func TestPlayHTTPUntil(t *testing.T) {
	dir := media.RemuxDir(t, media.H264HighAAC, "dash", "index.mpd", "-seg_duration", "2")
	s, segments := serve(t, dir, nil)
	r, err := PlayHTTP(context.Background(), nil, s.URL+"/index.mpd", report.Until{Keyframes: 3})
	if err != nil {
		t.Fatal(err)
	}
	if r.Video.Keyframes < 3 || r.Video.Frames >= 750 {
		t.Errorf("%v keyframes and %v frames, want at least 3 and not the whole file", r.Video.Keyframes, r.Video.Frames)
	}
	if n := segments.Load(); n != 4 {
		t.Errorf("%v segments fetched, want 2 of each representation", n)
	}

	_, err = PlayHTTP(context.Background(), nil, s.URL+"/index.mpd", report.Until{Keyframes: 100})
	if err == nil || !strings.Contains(err.Error(), "100 keyframes") {
		t.Errorf("error %v, want the presentation to end before 100 keyframes", err)
	}
}

// TestPlayHTTPDynamic plays a dynamic MPD that is first missing: it waits for
// it, reads every segment once, then waits for more until ctx ends.
func TestPlayHTTPDynamic(t *testing.T) {
	dir := media.RemuxDir(t, media.H264HighAAC, "dash", "index.mpd", "-seg_duration", "2")
	b, err := os.ReadFile(filepath.Join(dir, "index.mpd"))
	if err != nil {
		t.Fatal(err)
	}
	dynamic := strings.Replace(string(b), `type="static"`, `type="dynamic"`, 1)
	var n atomic.Int32
	s, segments := serve(t, dir, map[string]http.HandlerFunc{"/live.mpd": func(w http.ResponseWriter, r *http.Request) {
		if n.Add(1) == 1 {
			http.NotFound(w, r)
			return
		}
		w.Write([]byte(dynamic))
	}})

	ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
	defer cancel()
	r, err := PlayHTTP(ctx, nil, s.URL+"/live.mpd", report.Until{Keyframes: 100})
	if !errors.Is(err, context.DeadlineExceeded) || !strings.Contains(err.Error(), "100 keyframes") {
		t.Errorf("error %v, want the deadline before 100 keyframes", err)
	}
	if r == nil || r.Video == nil || r.Video.Keyframes != 30 {
		t.Errorf("report %v, want the 30 keyframes of every segment", r)
	}
	files, _ := filepath.Glob(filepath.Join(dir, "chunk-*"))
	if got := int(segments.Load()); got != len(files) {
		t.Errorf("%v segments fetched, want each of the %v once", got, len(files))
	}
}

// TestPlayHTTPErrors fails on a missing segment.
func TestPlayHTTPErrors(t *testing.T) {
	dir := media.RemuxDir(t, media.H264HighAAC, "dash", "index.mpd", "-seg_duration", "2")
	if err := os.Remove(filepath.Join(dir, "chunk-stream1-00002.m4s")); err != nil {
		t.Fatal(err)
	}
	s, _ := serve(t, dir, nil)
	_, err := PlayHTTP(context.Background(), nil, s.URL+"/index.mpd", report.Until{})
	if err == nil || !strings.Contains(err.Error(), "chunk-stream1-00002.m4s") || !strings.Contains(err.Error(), "404") {
		t.Errorf("error %v, want the 404 of the missing segment", err)
	}
}

func TestParseMPDAttributes(t *testing.T) {
	m, err := ParseMPD([]byte(srsMPD))
	if err != nil {
		t.Fatal(err)
	}
	if m.Profiles != "urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple" {
		t.Errorf("profiles %q", m.Profiles)
	}
	if m.MinimumUpdatePeriod != "PT150.000S" || m.TimeShiftBufferDepth != "PT50.000S" || m.MinBufferTime != "PT20.000S" {
		t.Errorf("durations %q, %q, %q", m.MinimumUpdatePeriod, m.TimeShiftBufferDepth, m.MinBufferTime)
	}
	if m.AvailabilityStartTime != "2026-10-10T12:00:00Z" || m.PublishTime != "2026-10-10T12:00:30Z" {
		t.Errorf("times %q, %q", m.AvailabilityStartTime, m.PublishTime)
	}
	as := m.Periods[0].AdaptationSets[0]
	if !as.SegmentAlignment || as.StartWithSAP != 1 {
		t.Errorf("adaptation set %+v", as)
	}
}

func TestDuration(t *testing.T) {
	for _, tc := range []struct {
		in   string
		want time.Duration
	}{
		{"PT0S", 0},
		{"PT5S", 5 * time.Second},
		{"PT150.000S", 150 * time.Second},
		{"PT0.500S", 500 * time.Millisecond},
		{"PT1M30S", 90 * time.Second},
		{"PT1H2M3.5S", time.Hour + 2*time.Minute + 3500*time.Millisecond},
		{"P1DT1S", 24*time.Hour + time.Second},
	} {
		got, err := Duration(tc.in)
		if err != nil || got != tc.want {
			t.Errorf("Duration(%q) = %v, %v, want %v", tc.in, got, err, tc.want)
		}
	}
	for _, bad := range []string{"", "5S", "PT", "PTxS", "PT5", "P1Y", "PT-1S"} {
		if _, err := Duration(bad); err == nil {
			t.Errorf("Duration(%q): no error", bad)
		}
	}
}
