// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"srs-integ/internal/media"
)

// TestProbeFile probes a test file: to its end with -duration 0, and only its
// first second by default.
func TestProbeFile(t *testing.T) {
	file := media.Path(t, media.H264HighAAC)

	var stdout, stderr bytes.Buffer
	if code := run([]string{"probe", "-duration", "0", file}, &stdout, &stderr); code != 0 {
		t.Fatalf("exit code %v, stderr %q", code, stderr.String())
	}
	for _, want := range []string{"h264 High 768x320", "30 keyframes", "750 frames", "aac 44100Hz 2ch"} {
		if !strings.Contains(stdout.String(), want) {
			t.Errorf("stdout %q, want it to contain %q", stdout.String(), want)
		}
	}

	stdout.Reset()
	if code := run([]string{"probe", file}, &stdout, &stderr); code != 0 {
		t.Fatalf("exit code %v, stderr %q", code, stderr.String())
	}
	if !strings.Contains(stdout.String(), "1 keyframe,") {
		t.Errorf("stdout %q, want one second with 1 keyframe", stdout.String())
	}
}

// TestProbeTSAndAudioFiles probes the test media remuxed into TS, ADTS and
// MP3 files, each by its extension, to the end.
func TestProbeTSAndAudioFiles(t *testing.T) {
	cases := []struct {
		name string
		path string
		want []string
	}{
		{"Ts", media.Remux(t, media.H264HighAAC, "mpegts"), []string{"h264 High 768x320", "30 keyframes", "750 frames", "aac 44100Hz 2ch"}},
		{"Aac", media.Remux(t, media.AACOnly, "adts"), []string{"no video", "aac 44100Hz 2ch: 1 sequence header"}},
		{"Mp3", media.Remux(t, media.H264HighMP3, "mp3", "-vn", "-write_xing", "0"), []string{"no video", "mp3 44100Hz 2ch"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			var stdout, stderr bytes.Buffer
			if code := run([]string{"probe", "-duration", "0", tc.path}, &stdout, &stderr); code != 0 {
				t.Fatalf("exit code %v, stderr %q", code, stderr.String())
			}
			for _, want := range tc.want {
				if !strings.Contains(stdout.String(), want) {
					t.Errorf("stdout %q, want it to contain %q", stdout.String(), want)
				}
			}
		})
	}
}

// TestProbeSegments probes the test media as HLS with TS and fmp4 segments,
// as DASH, served over HTTP, and as an MP4 file, to the end.
func TestProbeSegments(t *testing.T) {
	whole := []string{"h264 High 768x320", "30 keyframes", "750 frames", "aac 44100Hz 2ch"}
	serve := func(dir string) string {
		s := httptest.NewServer(http.FileServer(http.Dir(dir)))
		t.Cleanup(s.Close)
		return s.URL
	}
	cases := []struct {
		name string
		url  string
	}{
		{"HlsTs", serve(media.RemuxDir(t, media.H264HighAAC, "hls", "a.m3u8", "-hls_playlist_type", "vod")) + "/a.m3u8"},
		{"HlsFmp4", serve(media.RemuxDir(t, media.H264HighAAC, "hls", "a.m3u8", "-hls_playlist_type", "vod", "-hls_segment_type", "fmp4")) + "/a.m3u8"},
		{"Dash", serve(media.RemuxDir(t, media.H264HighAAC, "dash", "a.mpd")) + "/a.mpd"},
		{"Mp4", media.Remux(t, media.H264HighAAC, "mp4")},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			var stdout, stderr bytes.Buffer
			if code := run([]string{"probe", "-duration", "0", tc.url}, &stdout, &stderr); code != 0 {
				t.Fatalf("exit code %v, stderr %q", code, stderr.String())
			}
			for _, want := range whole {
				if !strings.Contains(stdout.String(), want) {
					t.Errorf("stdout %q, want it to contain %q", stdout.String(), want)
				}
			}
		})
	}
}
