// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"math"
	"testing"
	"time"
)

// TestGeneratedMedia reads each file of testdata/media, made by make
// integ-prepare, and checks what its recipe fixes. ffmpeg versions make
// different bytes, so nothing checks bytes.
func TestGeneratedMedia(t *testing.T) {
	type want struct {
		video, profile string
		bframes        bool
		audio          string
		rate, channels int
	}
	cases := map[string]want{
		H264HighAAC:      {"h264", "High", true, "aac", 44100, 2},
		H264BaselineAAC:  {"h264", "Baseline", false, "aac", 44100, 2},
		H264BaselineOpus: {"h264", "Baseline", false, "opus", 48000, 2},
		HEVCAAC:          {"hevc", "Main", true, "aac", 44100, 2},
		HEVCOpus:         {"hevc", "Main", false, "opus", 48000, 2},
		AACOnly:          {"", "", false, "aac", 44100, 2},
		H264Only:         {"h264", "High", true, "", 0, 0},
		H264HighMP3:      {"h264", "High", true, "mp3", 44100, 2},
		VP9Opus:          {"vp9", "0", false, "opus", 48000, 2},
		AV1Opus:          {"av1", "Main", false, "opus", 48000, 2},
		PCMU:             {"", "", false, "pcmu", 8000, 1},
		PCMA:             {"", "", false, "pcma", 8000, 1},
	}
	if len(cases) != len(Recipes) {
		t.Fatalf("%v cases for %v recipes", len(cases), len(Recipes))
	}

	const full = Duration * time.Second
	for name, w := range cases {
		t.Run(name, func(t *testing.T) {
			info, err := ProbeFile(Path(t, name))
			if err != nil {
				t.Fatal(err)
			}

			if w.video == "" {
				if info.Video != nil {
					t.Errorf("has video %+v", info.Video)
				}
			} else if v := info.Video; v == nil {
				t.Errorf("no video")
			} else {
				if v.Codec != w.video || v.Profile != w.profile || v.BFrames != w.bframes {
					t.Errorf("video %v %v B-frames %v, want %v %v %v", v.Codec, v.Profile, v.BFrames, w.video, w.profile, w.bframes)
				}
				if v.Frames != Duration*FPS || math.Abs(v.FPS()-FPS) > 0.01 {
					t.Errorf("%v frames at %.3f fps, want %v at %v", v.Frames, v.FPS(), Duration*FPS, FPS)
				}
				checkGOP(t, v.Keyframes)
				checkDuration(t, "video", v.Duration, full)
			}

			if w.audio == "" {
				if info.Audio != nil {
					t.Errorf("has audio %+v", info.Audio)
				}
			} else if a := info.Audio; a == nil {
				t.Errorf("no audio")
			} else {
				if a.Codec != w.audio || a.SampleRate != w.rate || a.Channels != w.channels {
					t.Errorf("audio %v %vHz %vch, want %v %vHz %vch", a.Codec, a.SampleRate, a.Channels, w.audio, w.rate, w.channels)
				}
				if a.Frames == 0 {
					t.Errorf("no audio frames")
				}
				checkDuration(t, "audio", a.Duration, full)
			}
		})
	}
}

// checkGOP requires a keyframe every second from the first frame on.
func checkGOP(t *testing.T, keys []time.Duration) {
	t.Helper()
	if len(keys) != Duration {
		t.Errorf("%v keyframes, want %v", len(keys), Duration)
	}
	for i := 1; i < len(keys); i++ {
		if d := keys[i] - keys[i-1]; d < time.Second-time.Millisecond || d > time.Second+time.Millisecond {
			t.Errorf("keyframe %v is %v after the one before, want 1s", i, d)
			return
		}
	}
}

// checkDuration allows two frames of difference.
func checkDuration(t *testing.T, what string, got, want time.Duration) {
	t.Helper()
	if got < want-80*time.Millisecond || got > want+80*time.Millisecond {
		t.Errorf("%v lasts %v, want %v", what, got, want)
	}
}
