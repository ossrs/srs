// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestMissingFeatures(t *testing.T) {
	all := `{"code":0,"data":{"features":{"ssl":true,"srt":true,"rtsp":true,"gb28181":true,"rtc":true,"simulator":true,"sanitizer":true}}}`
	if got, err := missingFeatures([]byte(all)); err != nil || len(got) != 0 {
		t.Errorf("missingFeatures(all) = %v, %v", got, err)
	}

	// A false field and a field the server does not report are both missing.
	some := `{"code":0,"data":{"features":{"srt":false,"rtsp":true,"gb28181":true,"rtc":true,"sanitizer":true}}}`
	if got, err := missingFeatures([]byte(some)); err != nil || !reflect.DeepEqual(got, []string{"srt", "simulator"}) {
		t.Errorf("missingFeatures(some) = %v, %v; want [srt simulator]", got, err)
	}

	if _, err := missingFeatures([]byte(`not json`)); err == nil {
		t.Error("missingFeatures(not json) has no error")
	}
	if _, err := missingFeatures([]byte(`{"code":1}`)); err == nil {
		t.Error("missingFeatures(code 1) has no error")
	}
}

func TestCleanStaleRuns(t *testing.T) {
	root := t.TempDir()
	for _, name := range []string{"100", "200", "keep", "300"} {
		if err := os.MkdirAll(filepath.Join(root, name, "srs-1"), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(root, "400"), nil, 0o644); err != nil {
		t.Fatal(err)
	}

	cleanStaleRuns(root, func(pid int) bool { return pid == 200 })

	for name, want := range map[string]bool{"100": false, "200": true, "keep": true, "300": false, "400": true} {
		_, err := os.Stat(filepath.Join(root, name))
		if exists := err == nil; exists != want {
			t.Errorf("%s exists=%v, want %v", name, exists, want)
		}
	}
}

// TestCommonConfig checks the Common server plays with the lowest latency, so
// a player gets each frame at once rather than in 350ms batches, serves
// HTTP-FLV, HLS and DASH with 10s segments, calls every hook, and that its env
// builds.
func TestCommonConfig(t *testing.T) {
	cfg := CommonConfig()
	if !cfg.Hooks {
		t.Error("hooks are off, want every hook on the mock")
	}
	if !reflect.DeepEqual(cfg.Listeners, CommonListeners) {
		t.Errorf("listeners %v, want %v", cfg.Listeners, CommonListeners)
	}
	want := map[string]string{
		"SRS_VHOST_MIN_LATENCY":        "on",
		"SRS_VHOST_PLAY_MW_LATENCY":    "0",
		"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
		"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream].flv",
		"SRS_VHOST_HLS_ENABLED":        "on",
		"SRS_VHOST_DASH_ENABLED":       "on",
		"SRS_VHOST_DASH_DASH_FRAGMENT": "10",
	}
	if !reflect.DeepEqual(cfg.Env, want) {
		t.Errorf("env %v, want %v", cfg.Env, want)
	}
	ports := map[Listener]int{}
	for i, l := range cfg.Listeners {
		ports[l] = 14000 + i
	}
	if _, err := BuildEnv(cfg, ports, "/tmp/x", TLSFiles{Key: "k", Cert: "c"}); err != nil {
		t.Errorf("build env: %v", err)
	}
}

// TestRemuxConfig checks a remux variant serves its extension, plays with
// Common's latency, has only the listeners it needs, and that its env builds.
func TestRemuxConfig(t *testing.T) {
	cfg := RemuxConfig("ts")
	if want := []Listener{RTMP, HTTPAPI, HTTPServer}; !reflect.DeepEqual(cfg.Listeners, want) {
		t.Errorf("listeners %v, want %v", cfg.Listeners, want)
	}
	want := map[string]string{
		"SRS_VHOST_MIN_LATENCY":        "on",
		"SRS_VHOST_PLAY_MW_LATENCY":    "0",
		"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
		"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream].ts",
	}
	if !reflect.DeepEqual(cfg.Env, want) {
		t.Errorf("env %v, want %v", cfg.Env, want)
	}
	ports := map[Listener]int{RTMP: 14000, HTTPAPI: 14001, HTTPServer: 14002}
	if _, err := BuildEnv(cfg, ports, "/tmp/x", TLSFiles{Key: "k", Cert: "c"}); err != nil {
		t.Errorf("build env: %v", err)
	}
}
