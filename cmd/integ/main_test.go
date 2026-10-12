// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"strings"
	"testing"

	"srs-integ/internal/rtc"
)

func TestRun(t *testing.T) {
	cases := []struct {
		name   string
		args   []string
		code   int
		stdout string
		stderr string
	}{
		{"NoArgs", nil, 2, "", "Usage: srs-integ"},
		{"Help", []string{"help"}, 0, "Usage: srs-integ", ""},
		{"HelpFlag", []string{"-h"}, 0, "Usage: srs-integ", ""},
		{"UnknownCommand", []string{"play"}, 2, "", "unknown command: play"},
		{"ProbeNoURL", []string{"probe"}, 2, "", "Usage: srs-integ probe [flags] <url>"},
		{"ProbeExtraArgs", []string{"probe", "rtmp://a/b", "c"}, 2, "", "Usage: srs-integ probe [flags] <url>"},
		{"ProbeBadFlag", []string{"probe", "-nothing", "rtmp://a/b"}, 2, "", "Usage: srs-integ probe [flags] <url>"},
		{"ProbeRTMPSRefused", []string{"probe", "-timeout", "1s", "rtmps://127.0.0.1:1/live/a"}, 1, "", "connection refused"},
		{"ProbeBadURL", []string{"probe", "ftp://127.0.0.1/live/a.flv"}, 1, "", "probe: unsupported URL"},
		{"ProbeBadHTTPPath", []string{"probe", "http://127.0.0.1:1/live/a.mp4"}, 1, "", "probe: unsupported URL"},
		{"ProbeHLSRefused", []string{"probe", "-timeout", "1s", "http://127.0.0.1:1/live/a.m3u8"}, 1, "", "probe:"},
		{"ProbeDASHRefused", []string{"probe", "-timeout", "1s", "http://127.0.0.1:1/live/a.mpd"}, 1, "", "probe:"},
		{"ProbeNoMP4File", []string{"probe", "/nothing/a.mp4"}, 1, "", "/nothing/a.mp4"},
		{"ProbeTSRefused", []string{"probe", "-timeout", "1s", "http://127.0.0.1:1/live/a.ts"}, 1, "", "probe:"},
		{"ProbeAudioRefused", []string{"probe", "-timeout", "1s", "http://127.0.0.1:1/live/a.mp3"}, 1, "", "probe:"},
		{"ProbeNoTSFile", []string{"probe", "/nothing/a.ts"}, 1, "", "/nothing/a.ts"},
		{"ProbeNoFile", []string{"probe", "/nothing/a.flv"}, 1, "", "/nothing/a.flv"},
		{"ProbeRefused", []string{"probe", "-timeout", "1s", "rtmp://127.0.0.1:1/live/a"}, 1, "", "probe:"},
		{"PublishNoArgs", []string{"publish"}, 2, "", "Usage: srs-integ publish <file> <url>"},
		{"PublishNoURL", []string{"publish", "a.flv"}, 2, "", "Usage: srs-integ publish <file> <url>"},
		{"PublishBadURL", []string{"publish", "a.flv", "http://127.0.0.1/live/a"}, 1, "", "publish: unsupported URL"},
		{"PublishNoFile", []string{"publish", "/nothing/a.flv", "rtmp://127.0.0.1:1/live/a"}, 1, "", "/nothing/a.flv"},
		{"ProbeWHEPRefused", []string{"probe", "-timeout", "1s", "http://127.0.0.1:1/rtc/v1/whep/?app=live&stream=a"}, 1, "", "connection refused"},
		{"ProbeWhipPlayRefused", []string{"probe", "-timeout", "1s", "http://127.0.0.1:1/rtc/v1/whip-play/?app=live&stream=a"}, 1, "", "connection refused"},
		{"PublishWHIPNoFile", []string{"publish", "/nothing/a.mp4", "http://127.0.0.1:1/rtc/v1/whip/?app=live&stream=a"}, 1, "", "/nothing/a.mp4"},
		{"PublishWHIPNotMP4", []string{"publish", "/nothing/a.flv", "http://127.0.0.1:1/rtc/v1/whip/?app=live&stream=a"}, 1, "", "publish: WHIP takes an MP4 file"},
		{"ProbeSRTNoAnswer", []string{"probe", "-timeout", "1s", "srt://127.0.0.1:1?streamid=#!::r=live/a,m=request"}, 1, "", "probe:"},
		{"ProbeSRTBadURL", []string{"probe", "srt://127.0.0.1"}, 1, "", "probe: \"srt://127.0.0.1\" has no host and port"},
		{"PublishSRTNotTS", []string{"publish", "/nothing/a.flv", "srt://127.0.0.1:1"}, 1, "", "publish: SRT takes an MPEG-TS file"},
		{"PublishSRTNoFile", []string{"publish", "/nothing/a.ts", "srt://127.0.0.1:1?streamid=#!::r=live/a,m=publish"}, 1, "", "/nothing/a.ts"},
		{"PublishSRTBadURL", []string{"publish", "/nothing/a.ts", "srt://127.0.0.1"}, 1, "", "publish: \"srt://127.0.0.1\" has no host and port"},
		{"PrepareExtraArgs", []string{"prepare", "x"}, 2, "", "Usage: srs-integ prepare"},
		{"PrepareBadFlag", []string{"prepare", "-nothing"}, 2, "", "Usage: srs-integ prepare"},
		{"PrepareNoSource", []string{"prepare", "-source", "/nothing/source.flv", "-dir", "/tmp/srs-integ-no-dir"}, 1, "", "/nothing/source.flv"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			var stdout, stderr bytes.Buffer
			code := run(tc.args, &stdout, &stderr)
			if code != tc.code {
				t.Errorf("exit code %v, want %v; stderr %q", code, tc.code, stderr.String())
			}
			checkOutput(t, "stdout", stdout.String(), tc.stdout)
			checkOutput(t, "stderr", stderr.String(), tc.stderr)
		})
	}
}

// checkOutput requires got to contain want, or to be empty when want is empty.
func checkOutput(t *testing.T, name, got, want string) {
	t.Helper()
	if want == "" {
		if got != "" {
			t.Errorf("%v %q, want empty", name, got)
		}
		return
	}
	if !strings.Contains(got, want) {
		t.Errorf("%v %q, want it to contain %q", name, got, want)
	}
}

// TestWithVcodec asks SRS for a WHIP video codec other than H.264, unless
// the URL already names one.
func TestWithVcodec(t *testing.T) {
	const whip = "http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=a"
	for _, tc := range []struct {
		url   string
		video []string
		want  string
	}{
		{whip, []string{"H264"}, whip},
		{whip, nil, whip},
		{whip, []string{"H265"}, whip + "&vcodec=h265"},
		{whip, []string{"AV1"}, whip + "&vcodec=av1"},
		{whip + "&vcodec=hevc", []string{"H265"}, whip + "&vcodec=hevc"},
		{whip + "&codec=vp9", []string{"VP9"}, whip + "&codec=vp9"},
		{"http://127.0.0.1:1985/rtc/v1/whip/", []string{"VP9"}, "http://127.0.0.1:1985/rtc/v1/whip/?vcodec=vp9"},
	} {
		got, err := withVcodec(tc.url, &rtc.Options{Video: tc.video})
		if err != nil || got != tc.want {
			t.Errorf("withVcodec(%v, %v) = %v, %v, want %v", tc.url, tc.video, got, err, tc.want)
		}
	}
}
