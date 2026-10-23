// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"os"
	"path/filepath"
	"runtime"
	"slices"
)

// A platform is how the adapter runs SRS on one OS.
type platform struct {
	// binary is the SRS binary in trunk/objs.
	binary string
	// workRoot holds one directory per run, named by the pid of the run.
	workRoot string
	// features are the build features of /api/v1/features the suite needs.
	features []string
	// build is how to build an SRS with those features.
	build string
}

func platformOf(goos string) platform {
	if goos == "windows" {
		// srs.exe comes from the Windows CMake build, which has no sanitizer.
		return platform{
			binary:   filepath.Join("windows", "srs.exe"),
			workRoot: filepath.Join(os.TempDir(), "srs-integ"),
			features: []string{"srt", "rtsp", "gb28181", "rtc", "simulator"},
			build:    "with skills/srs-develop/scripts/srs-windows-runner.sh build",
		}
	}
	return platform{
		binary:   "srs",
		workRoot: "/tmp/srs-integ",
		features: []string{"srt", "rtsp", "gb28181", "rtc", "simulator", "sanitizer"},
		build:    "in trunk with ./configure --srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on && make",
	}
}

// host is the platform this process runs on.
var host = platformOf(runtime.GOOS)

// BuildFeatures returns the build features of /api/v1/features that SRS has
// on this OS, which the suite needs.
func BuildFeatures() []string {
	return slices.Clone(host.features)
}

// listenerFeatures are the listeners SRS has only when it is built with a
// feature; it refuses to start with one it is built without.
var listenerFeatures = map[Listener]string{SRT: "srt", RTSP: "rtsp", GB: "gb28181"}

// builtListeners returns the listeners of all that the platform's build has.
func (p platform) builtListeners(all []Listener) []Listener {
	var out []Listener
	for _, l := range all {
		if f, ok := listenerFeatures[l]; !ok || slices.Contains(p.features, f) {
			out = append(out, l)
		}
	}
	return out
}
