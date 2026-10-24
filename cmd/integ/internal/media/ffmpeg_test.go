// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"bytes"
	"errors"
	"strings"
	"testing"
	"time"
)

func TestParseEncoders(t *testing.T) {
	out := `Encoders:
 V..... = Video
 A..... = Audio
 ------
 V....D libx264              libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (codec h264)
 V....D libvpx-vp9           libvpx VP9 (codec vp9)
 A..X.D opus                 Opus
 A....D aac                  AAC (Advanced Audio Coding)
`
	got := parseEncoders(out)
	for _, e := range []string{"libx264", "libvpx-vp9", "opus", "aac"} {
		if !got[e] {
			t.Errorf("no %v in %v", e, got)
		}
	}
	for _, e := range []string{"V.....", "Video", "------", "libx265"} {
		if got[e] {
			t.Errorf("%v is not an encoder", e)
		}
	}

	// An ffmpeg for Windows ends its lines with CR LF.
	got = parseEncoders(strings.ReplaceAll(out, "\n", "\r\n"))
	for _, e := range []string{"libx264", "libvpx-vp9", "opus", "aac"} {
		if !got[e] {
			t.Errorf("CR LF: no %v in %v", e, got)
		}
	}
	if got["------"] {
		t.Error("CR LF: ------ is not an encoder")
	}
}

func TestUsableAndComplete(t *testing.T) {
	if m := fakeFFmpeg("v", "libx264", "libx265", "opus").Usable(); len(m) != 0 {
		t.Errorf("native opus is usable, lacks %v", m)
	}
	m := fakeFFmpeg("v", "libx264").Usable()
	if strings.Join(m, ",") != "libx265,libopus or opus" {
		t.Errorf("lacks %v", m)
	}
	if fakeFFmpeg("v", "libx264", "libx265", "libopus", "libvpx-vp9").Complete() {
		t.Errorf("complete without libsvtav1")
	}
	if !fakeFFmpeg("v", everyEncoder...).Complete() {
		t.Errorf("not complete with every encoder")
	}
}

// fakeWorld is a machine with ffmpeg binaries at some paths.
type fakeWorld struct {
	env    map[string]string
	path   string // what PATH resolves ffmpeg to, or ""
	files  map[string]*FFmpeg
	probed []string
}

func (w *fakeWorld) finder() Finder {
	return Finder{
		Getenv: func(k string) string { return w.env[k] },
		LookPath: func(string) (string, error) {
			if w.path == "" {
				return "", errors.New("not found")
			}
			return w.path, nil
		},
		Home:   func() (string, error) { return "/home/u", nil },
		Exists: func(p string) bool { return w.files[p] != nil },
		Probe: func(p string) (*FFmpeg, error) {
			w.probed = append(w.probed, p)
			if ff := w.files[p]; ff != nil {
				ff.Path = p
				return ff, nil
			}
			return nil, errors.New("no such file")
		},
	}
}

func TestFindOrder(t *testing.T) {
	good := func() *FFmpeg { return fakeFFmpeg("v", "libx264", "libx265", "libopus") }
	bad := func() *FFmpeg { return fakeFFmpeg("v", "libx264") }

	cases := []struct {
		name  string
		world *fakeWorld
		want  string
		err   string
	}{
		{"EnvFirst", &fakeWorld{env: map[string]string{"FFMPEG": "/env/ffmpeg"}, path: "/usr/bin/ffmpeg",
			files: map[string]*FFmpeg{"/env/ffmpeg": good(), "/usr/bin/ffmpeg": good()}}, "/env/ffmpeg", ""},
		{"PathWhenEnvUnset", &fakeWorld{path: "/usr/bin/ffmpeg",
			files: map[string]*FFmpeg{"/usr/bin/ffmpeg": good(), "/home/u/.local/bin/ffmpeg": good()}}, "/usr/bin/ffmpeg", ""},
		{"LocalBinLast", &fakeWorld{files: map[string]*FFmpeg{"/home/u/.local/bin/ffmpeg": good()}}, "/home/u/.local/bin/ffmpeg", ""},
		{"SkipsUnusable", &fakeWorld{path: "/usr/bin/ffmpeg",
			files: map[string]*FFmpeg{"/usr/bin/ffmpeg": bad(), "/home/u/.local/bin/ffmpeg": good()}}, "/home/u/.local/bin/ffmpeg", ""},
		{"NoneUsable", &fakeWorld{path: "/usr/bin/ffmpeg",
			files: map[string]*FFmpeg{"/usr/bin/ffmpeg": bad()}}, "", "/usr/bin/ffmpeg lacks libx265, libopus or opus"},
		{"NoneFound", &fakeWorld{}, "", "no ffmpeg found"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			ff, err := tc.world.finder().Find()
			if tc.err != "" {
				if err == nil || !strings.Contains(err.Error(), tc.err) {
					t.Fatalf("err %v, want %q", err, tc.err)
				}
				return
			}
			if err != nil || ff.Path != tc.want {
				t.Fatalf("got %+v, %v, want %v", ff, err, tc.want)
			}
		})
	}
}

// fakeSetup records the script runs; after a run, local is the ffmpeg in
// ~/.local/bin.
type fakeSetup struct {
	world  *fakeWorld
	busy   []string // what Busy answers, one per call, then idle
	runs   int
	slept  time.Duration
	runErr error
	log    bytes.Buffer
}

func (f *fakeSetup) setup(goos string) Setup {
	return Setup{
		Script: "setup-ffmpeg-with-whip.sh",
		GOOS:   goos,
		Busy: func() string {
			if len(f.busy) == 0 {
				return ""
			}
			b := f.busy[0]
			f.busy = f.busy[1:]
			return b
		},
		Run: func(string) error {
			f.runs++
			if f.runErr != nil {
				return f.runErr
			}
			f.world.files["/home/u/.local/bin/ffmpeg"] = fakeFFmpeg("new", everyEncoder...)
			return nil
		},
		Wait:     10 * time.Second,
		Patience: 30 * time.Second,
		Sleep:    func(d time.Duration) { f.slept += d },
		Log:      &f.log,
	}
}

func TestEnsure(t *testing.T) {
	usable := func() *FFmpeg { return fakeFFmpeg("old", "libx264", "libx265", "libopus", "libvpx-vp9") }
	localWith := func(ff *FFmpeg) *fakeWorld {
		return &fakeWorld{files: map[string]*FFmpeg{"/home/u/.local/bin/ffmpeg": ff}}
	}

	t.Run("LinuxUsesWhatItFinds", func(t *testing.T) {
		f := &fakeSetup{world: localWith(usable())}
		ff, err := f.setup("linux").Ensure(f.world.finder())
		if err != nil || ff.Version != "old" || f.runs != 0 {
			t.Fatalf("got %+v, %v, %v runs", ff, err, f.runs)
		}
	})
	t.Run("LinuxNoneFound", func(t *testing.T) {
		f := &fakeSetup{world: &fakeWorld{files: map[string]*FFmpeg{}}}
		if _, err := f.setup("linux").Ensure(f.world.finder()); err == nil || f.runs != 0 {
			t.Fatalf("err %v, %v runs", err, f.runs)
		}
	})
	t.Run("MacCompleteIsKept", func(t *testing.T) {
		f := &fakeSetup{world: localWith(fakeFFmpeg("old", everyEncoder...))}
		ff, err := f.setup("darwin").Ensure(f.world.finder())
		if err != nil || ff.Version != "old" || f.runs != 0 {
			t.Fatalf("got %+v, %v, %v runs", ff, err, f.runs)
		}
	})
	t.Run("MacLacksAV1Rebuilds", func(t *testing.T) {
		f := &fakeSetup{world: localWith(usable())}
		ff, err := f.setup("darwin").Ensure(f.world.finder())
		if err != nil || ff.Version != "new" || f.runs != 1 || f.slept != 0 {
			t.Fatalf("got %+v, %v, %v runs, slept %v", ff, err, f.runs, f.slept)
		}
		if !strings.Contains(f.log.String(), "lacks libsvtav1") {
			t.Errorf("log %q does not say why", f.log.String())
		}
	})
	t.Run("MacNoneFoundBuilds", func(t *testing.T) {
		f := &fakeSetup{world: &fakeWorld{files: map[string]*FFmpeg{}}}
		ff, err := f.setup("darwin").Ensure(f.world.finder())
		if err != nil || ff.Version != "new" || f.runs != 1 {
			t.Fatalf("got %+v, %v, %v runs", ff, err, f.runs)
		}
	})
	t.Run("MacWaitsWhileBusy", func(t *testing.T) {
		f := &fakeSetup{world: localWith(usable()), busy: []string{"the Full-tier lock is held", "ffmpeg is running"}}
		ff, err := f.setup("darwin").Ensure(f.world.finder())
		if err != nil || ff.Version != "new" || f.runs != 1 || f.slept != 20*time.Second {
			t.Fatalf("got %+v, %v, %v runs, slept %v", ff, err, f.runs, f.slept)
		}
		for _, want := range []string{"the Full-tier lock is held", "ffmpeg is running"} {
			if !strings.Contains(f.log.String(), want) {
				t.Errorf("log %q has no %q", f.log.String(), want)
			}
		}
	})
	t.Run("MacGivesUpWhenAlwaysBusy", func(t *testing.T) {
		f := &fakeSetup{world: localWith(usable()), busy: []string{"b", "b", "b", "b", "b", "b"}}
		_, err := f.setup("darwin").Ensure(f.world.finder())
		if err == nil || !strings.Contains(err.Error(), "busy") || f.runs != 0 {
			t.Fatalf("err %v, %v runs", err, f.runs)
		}
	})
	t.Run("MacScriptFails", func(t *testing.T) {
		f := &fakeSetup{world: localWith(usable()), runErr: errors.New("exit status 1")}
		_, err := f.setup("darwin").Ensure(f.world.finder())
		if err == nil || !strings.Contains(err.Error(), "exit status 1") {
			t.Fatalf("err %v", err)
		}
	})
	t.Run("MacNoScriptUsesWhatItFinds", func(t *testing.T) {
		f := &fakeSetup{world: localWith(usable())}
		s := f.setup("darwin")
		s.Script = ""
		ff, err := s.Ensure(f.world.finder())
		if err != nil || ff.Version != "old" || f.runs != 0 {
			t.Fatalf("got %+v, %v, %v runs", ff, err, f.runs)
		}
	})
}

func TestParseProtocols(t *testing.T) {
	out := `Supported file protocols:
Input:
  file
  http
  srt
  srtp
  rtmp
Output:
  file
  http
  srt
  icecast
`
	got := parseProtocols(out)
	for _, p := range []string{"file", "http", "srt"} {
		if !got[p] {
			t.Errorf("no %v in %v", p, got)
		}
	}
	// Only a protocol ffmpeg can both read and write counts.
	for _, p := range []string{"srtp", "rtmp", "icecast", "Input:", "Output:"} {
		if got[p] {
			t.Errorf("%v counted in %v", p, got)
		}
	}
}

func TestHasProtocol(t *testing.T) {
	ff, err := FindFFmpeg()
	if err != nil {
		t.Skipf("no ffmpeg: %v", err)
	}
	if !ff.HasProtocol("file") {
		t.Errorf("%v has no file protocol", ff.Path)
	}
	if ff.HasProtocol("no-such-protocol") {
		t.Errorf("%v has a protocol that doesn't exist", ff.Path)
	}
	if (&FFmpeg{Path: "/no/such/ffmpeg"}).HasProtocol("file") {
		t.Errorf("a missing ffmpeg has the file protocol")
	}
}
