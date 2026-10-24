// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"bytes"
	"math"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestProbeFLV(t *testing.T) {
	// Three frames at 25 fps, the first a keyframe, with B-frame offsets,
	// and two AAC frames at 44.1 kHz stereo.
	file := flvBytes(5,
		FLVTag{TagVideo, 0, []byte{0x17, 0, 0, 0, 0, 1, 100, 0, 30}},
		FLVTag{TagAudio, 0, []byte{0xaf, 0, 0x12, 0x10}},
		FLVTag{TagVideo, 0, []byte{0x17, 1, 0, 0, 80, 9}},
		FLVTag{TagAudio, 0, []byte{0xaf, 1, 9}},
		FLVTag{TagVideo, 40, []byte{0x27, 1, 0, 0, 0, 9}},
		FLVTag{TagAudio, 23, []byte{0xaf, 1, 9}},
		FLVTag{TagVideo, 80, []byte{0x27, 1, 0, 0, 40, 9}},
	)
	info, err := probeFLV(bytes.NewReader(file))
	if err != nil {
		t.Fatal(err)
	}
	v := info.Video
	if v == nil || v.Codec != "h264" || v.Profile != "High" || v.Frames != 3 || !v.BFrames ||
		len(v.Keyframes) != 1 || v.Duration != 120*time.Millisecond || math.Abs(v.FPS()-25) > 1e-9 {
		t.Errorf("video %+v", v)
	}
	a := info.Audio
	if a == nil || a.Codec != "aac" || a.SampleRate != 44100 || a.Channels != 2 || a.Frames != 2 {
		t.Errorf("audio %+v", a)
	}
}

func TestProbeMP4(t *testing.T) {
	file, _ := testMP4()
	info, err := probeMP4(bytes.NewReader(file), int64(len(file)))
	if err != nil {
		t.Fatal(err)
	}
	v := info.Video
	if v == nil || v.Codec != "h264" || v.Profile != "High" || v.Frames != 5 || !v.BFrames ||
		len(v.Keyframes) != 2 || v.Keyframes[1] != 120*time.Millisecond || v.Duration != 200*time.Millisecond {
		t.Errorf("video %+v", v)
	}
	if a := info.Audio; a == nil || a.Codec != "aac" || a.SampleRate != 44100 || a.Channels != 2 || a.Frames != 3 {
		t.Errorf("audio %+v", a)
	}
}

// TestProbeG711 reads raw G.711 files by their extension: 8 kHz mono, one
// byte a sample, so 1,000 bytes are 125ms in 7 frames of 20ms.
func TestProbeG711(t *testing.T) {
	for _, w := range []struct{ name, codec string }{{"a.ul", "pcmu"}, {"a.al", "pcma"}} {
		path := filepath.Join(t.TempDir(), w.name)
		if err := os.WriteFile(path, make([]byte, 1000), 0o644); err != nil {
			t.Fatal(err)
		}
		info, err := ProbeFile(path)
		if err != nil {
			t.Fatalf("%v: %v", w.name, err)
		}
		if info.Video != nil {
			t.Errorf("%v: video %+v", w.name, info.Video)
		}
		a := info.Audio
		if a == nil || a.Codec != w.codec || a.SampleRate != 8000 || a.Channels != 1 || a.Frames != 7 || a.Duration != 125*time.Millisecond {
			t.Errorf("%v: audio %+v", w.name, a)
		}
	}
}

func TestCodecConfigs(t *testing.T) {
	profiles := []struct {
		codec  string
		config []byte
		want   string
	}{
		{"h264", []byte{1, 66}, "Baseline"},
		{"h264", []byte{1, 77}, "Main"},
		{"h264", []byte{1, 100}, "High"},
		{"hevc", []byte{1, 0x01}, "Main"},
		{"hevc", []byte{1, 0x02}, "2"},
		{"av1", []byte{0x81, 0x08}, "Main"},
		{"vp9", []byte{0, 10}, "0"},
		{"h264", []byte{1}, ""},
	}
	for _, p := range profiles {
		if got := Profile(p.codec, p.config); got != p.want {
			t.Errorf("%v %x: profile %q, want %q", p.codec, p.config, got, p.want)
		}
	}

	if rate, ch := AACConfig([]byte{0x12, 0x10}); rate != 44100 || ch != 2 {
		t.Errorf("AAC LC 44.1k stereo: %v %v", rate, ch)
	}
	if rate, ch := AACConfig([]byte{0x11, 0x88}); rate != 48000 || ch != 1 {
		t.Errorf("AAC LC 48k mono: %v %v", rate, ch)
	}
	if rate, ch := MP3Header([]byte{0xff, 0xfb, 0x90, 0x44}); rate != 44100 || ch != 2 {
		t.Errorf("MPEG-1 44.1k joint stereo: %v %v", rate, ch)
	}
	if rate, ch := MP3Header([]byte{0xff, 0xf3, 0x94, 0xc4}); rate != 24000 || ch != 1 {
		t.Errorf("MPEG-2 24k mono: %v %v", rate, ch)
	}
	if rate, _ := MP3Header([]byte{0, 0, 0, 0}); rate != 0 {
		t.Errorf("not a frame: %v", rate)
	}
}
