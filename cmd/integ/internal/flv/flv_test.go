// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package flv

import (
	"context"
	"errors"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
)

const ms = time.Millisecond

// openFile returns a tag source over a generated test file.
func openFile(t *testing.T, name string) *media.FLVReader {
	t.Helper()
	f, err := os.Open(media.Path(t, name))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { f.Close() })
	r, err := media.NewFLVReader(f)
	if err != nil {
		t.Fatal(err)
	}
	return r
}

// TestAnalyzeTheMedia reads each FLV test file to its end, and checks the
// report against what its recipe fixes.
func TestAnalyzeTheMedia(t *testing.T) {
	type want struct {
		video, profile string
		audio          string
		rate           int
	}
	cases := map[string]want{
		media.H264HighAAC:     {"h264", "High", "aac", 44100},
		media.H264BaselineAAC: {"h264", "Baseline", "aac", 44100},
		media.HEVCAAC:         {"hevc", "Main", "aac", 44100},
		media.AACOnly:         {"", "", "aac", 44100},
		media.H264Only:        {"h264", "High", "", 0},
		media.H264HighMP3:     {"h264", "High", "mp3", 44100},
	}
	for name, w := range cases {
		t.Run(name, func(t *testing.T) {
			r, err := Analyze(openFile(t, name), report.Until{})
			if err != nil {
				t.Fatal(err)
			}
			if r.Metadata != 1 {
				t.Errorf("metadata %v, want 1", r.Metadata)
			}
			if err := r.CheckTimestamps(100 * ms); err != nil {
				t.Errorf("timestamps: %v", err)
			}

			if w.video == "" {
				if r.Video != nil {
					t.Errorf("has video %+v", r.Video)
				}
			} else if v := r.Video; v == nil {
				t.Errorf("no video")
			} else {
				if v.Codec != w.video || v.Profile != w.profile || v.Width != 768 || v.Height != 320 {
					t.Errorf("video %v %v %vx%v, want %v %v 768x320", v.Codec, v.Profile, v.Width, v.Height, w.video, w.profile)
				}
				if v.SequenceHeaders != 1 || v.Keyframes != media.Duration || v.Frames != media.Duration*media.FPS {
					t.Errorf("video %v headers %v keyframes %v frames, want 1 %v %v", v.SequenceHeaders, v.Keyframes, v.Frames, media.Duration, media.Duration*media.FPS)
				}
			}

			if w.audio == "" {
				if r.Audio != nil {
					t.Errorf("has audio %+v", r.Audio)
				}
			} else if a := r.Audio; a == nil {
				t.Errorf("no audio")
			} else {
				if a.Codec != w.audio || a.SampleRate != w.rate || a.Channels != 2 || a.Frames == 0 {
					t.Errorf("audio %v %vHz %vch %v frames, want %v %vHz 2ch", a.Codec, a.SampleRate, a.Channels, a.Frames, w.audio, w.rate)
				}
				if wantHeaders := map[string]int{"aac": 1, "mp3": 0}[w.audio]; a.SequenceHeaders != wantHeaders {
					t.Errorf("audio %v sequence headers, want %v", a.SequenceHeaders, wantHeaders)
				}
			}
			// The audio outlasts the video at the end of the cut, so the sync is
			// checked on the first seconds, where both tracks play.
			if w.video != "" && w.audio != "" {
				head, err := Analyze(openFile(t, name), report.Until{Duration: 5 * time.Second})
				if err != nil || head.AVSync > 50*ms {
					t.Errorf("A/V sync %v %v, want at most 50ms in an interleaved file", head.AVSync, err)
				}
			}
			if d := r.Duration(); d < media.Duration*time.Second-80*ms || d > media.Duration*time.Second+80*ms {
				t.Errorf("duration %v, want %vs", d, media.Duration)
			}
		})
	}
}

// TestAnalyzeStops stops at the first tag that meets the condition.
func TestAnalyzeStops(t *testing.T) {
	u := report.Until{Metadata: true, VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 25}
	r, err := Analyze(openFile(t, media.H264HighAAC), u)
	if err != nil {
		t.Fatal(err)
	}
	if r.Video.Frames != 25 || r.Video.Keyframes != 1 {
		t.Errorf("stopped after %v frames and %v keyframes, want 25 and 1", r.Video.Frames, r.Video.Keyframes)
	}
}

// TestAnalyzeEndsTooEarly fails when the stream ends before the condition,
// and the error tells both.
func TestAnalyzeEndsTooEarly(t *testing.T) {
	r, err := Analyze(openFile(t, media.AACOnly), report.Until{VideoHeader: true})
	if err == nil {
		t.Fatal("no error for a stream without the video it waits for")
	}
	for _, want := range []string{"ended before video sequence header", "no video", "aac"} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("error %q, want it to contain %q", err, want)
		}
	}
	if r == nil || r.Audio == nil || r.Audio.Frames == 0 {
		t.Errorf("no partial report with the audio: %+v", r)
	}
}

type tags []*media.FLVTag

func (s *tags) ReadTag() (*media.FLVTag, error) {
	if len(*s) == 0 {
		return nil, io.EOF
	}
	tag := (*s)[0]
	*s = (*s)[1:]
	return tag, nil
}

// TestAnalyzerTags covers what the test media doesn't have: an end of
// sequence, an enhanced metadata packet, a bad tag, and a read error.
func TestAnalyzerTags(t *testing.T) {
	var a Analyzer
	for _, tag := range []*media.FLVTag{
		{Type: media.TagVideo, Data: []byte{0x17, 2, 0, 0, 0}},                      // AVC end of sequence.
		{Type: media.TagVideo, Data: []byte{0x94, 'h', 'v', 'c', '1', 8, 0}},        // Enhanced metadata.
		{Type: media.TagVideo, Timestamp: 40, Data: []byte{0x17, 1, 0, 0, 0, 0, 0}}, // A keyframe without a header.
		{Type: media.TagAudio, Timestamp: 40, Data: []byte{0xaf, 1, 0x21}},
	} {
		if err := a.Add(tag); err != nil {
			t.Fatal(err)
		}
	}
	r := &a.Report
	if r.Video == nil || r.Video.Frames != 1 || r.Video.Keyframes != 1 || r.Video.SequenceHeaders != 0 || r.Metadata != 0 {
		t.Errorf("video %+v metadata %v, want 1 keyframe and no header", r.Video, r.Metadata)
	}
	if r.Video.FirstDTS != 40*ms || r.Audio == nil || r.Audio.Frames != 1 {
		t.Errorf("report %v", r)
	}

	if err := a.Add(&media.FLVTag{Type: media.TagVideo, Data: []byte{0x12}}); err == nil {
		t.Errorf("a Sorenson video tag is accepted, want an error")
	}
	if err := a.Add(&media.FLVTag{Type: media.TagVideo, Data: []byte{0x17, 0, 0, 0, 0, 1}}); err == nil {
		t.Errorf("a short avcC is accepted, want an error")
	}

	broken := errReader{errors.New("connection reset")}
	if _, err := Analyze(broken, report.Until{Keyframes: 1}); err == nil || !strings.Contains(err.Error(), "connection reset") {
		t.Errorf("read error %v, want connection reset", err)
	}
}

type errReader struct{ err error }

func (e errReader) ReadTag() (*media.FLVTag, error) { return nil, e.err }

func TestPlayHTTP(t *testing.T) {
	file := media.Path(t, media.H264HighAAC)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		if req.URL.Path != "/live/a.flv" {
			http.NotFound(w, req)
			return
		}
		http.ServeFile(w, req, file)
	}))
	defer ts.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	u := report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 2}
	r, err := PlayHTTP(ctx, nil, ts.URL+"/live/a.flv", u)
	if err != nil {
		t.Fatal(err)
	}
	if r.Video.Keyframes != 2 || r.Video.Width != 768 || r.Audio.SampleRate != 44100 {
		t.Errorf("report %v", r)
	}

	if _, err := PlayHTTP(ctx, nil, ts.URL+"/live/b.flv", u); err == nil || !strings.Contains(err.Error(), "404") {
		t.Errorf("missing stream error %v, want a 404", err)
	}
}

// TestPlayHTTPTimeout stops a stream that never meets the condition when the
// context ends, with the partial report.
func TestPlayHTTPTimeout(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte{'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0})
		w.(http.Flusher).Flush()
		<-req.Context().Done()
	}))
	defer ts.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 100*ms)
	defer cancel()
	start := time.Now()
	r, err := PlayHTTP(ctx, nil, ts.URL+"/live/a.flv", report.Until{Keyframes: 1})
	if err == nil || !strings.Contains(err.Error(), "before 1 keyframe") {
		t.Errorf("error %v, want a timeout before 1 keyframe", err)
	}
	if r == nil {
		t.Errorf("no partial report")
	}
	if d := time.Since(start); d > time.Second {
		t.Errorf("stopped after %v, want about 100ms", d)
	}
}

func TestPlayRTMPRefused(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := l.Addr().String()
	l.Close()

	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if _, err := PlayRTMP(ctx, "rtmp://"+addr+"/live/a", nil, report.Until{Keyframes: 1}); err == nil {
		t.Errorf("no error from a closed port")
	}
}

// TestAnalyzerCountsOnlyMetadata counts onMetaData, alone or after
// @setDataFrame, as metadata, and no other data message, such as the
// |RtmpSampleAccess an RTMP server sends when play starts.
func TestAnalyzerCountsOnlyMetadata(t *testing.T) {
	script := func(values ...any) *media.FLVTag {
		b, err := rtmp.EncodeAMF0(values...)
		if err != nil {
			t.Fatal(err)
		}
		return &media.FLVTag{Type: media.TagScript, Data: b}
	}
	var a Analyzer
	for _, tag := range []*media.FLVTag{
		script("|RtmpSampleAccess", false, false),
		script("onMetaData", map[string]any{"width": float64(768)}),
		script("@setDataFrame", "onMetaData", map[string]any{"width": float64(768)}),
		script("onCuePoint", map[string]any{}),
		{Type: media.TagScript, Data: []byte{0xff}},
	} {
		if err := a.Add(tag); err != nil {
			t.Fatalf("add %x: %v", tag.Data, err)
		}
	}
	if a.Report.Metadata != 2 {
		t.Errorf("metadata %v, want 2: onMetaData and @setDataFrame onMetaData", a.Report.Metadata)
	}
}

// TestIsMetadata tells onMetaData, alone or after @setDataFrame, from the
// other data messages.
func TestIsMetadata(t *testing.T) {
	for _, tc := range []struct {
		values []any
		want   bool
	}{
		{[]any{"onMetaData", map[string]any{}}, true},
		{[]any{"@setDataFrame", "onMetaData", map[string]any{}}, true},
		{[]any{"|RtmpSampleAccess", false, false}, false},
		{[]any{"@setDataFrame", "onCuePoint"}, false},
	} {
		b, err := rtmp.EncodeAMF0(tc.values...)
		if err != nil {
			t.Fatal(err)
		}
		if got := IsMetadata(b); got != tc.want {
			t.Errorf("IsMetadata(%v) = %v, want %v", tc.values, got, tc.want)
		}
	}
	if IsMetadata([]byte{0xff}) {
		t.Errorf("IsMetadata of a broken message, want false")
	}
}
