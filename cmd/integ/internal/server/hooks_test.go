// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"reflect"
	"strings"
	"testing"
)

// TestHooksEnv checks every hook is on and points at the mock, each at its own
// path, and on_hls_notify, a GET, carries its action and stream in the query.
func TestHooksEnv(t *testing.T) {
	env := HooksEnv("http://127.0.0.1:14005")
	want := map[string]string{
		"SRS_VHOST_HTTP_HOOKS_ENABLED":      "on",
		"SRS_VHOST_HTTP_HOOKS_ON_CONNECT":   "http://127.0.0.1:14005/hooks/on_connect",
		"SRS_VHOST_HTTP_HOOKS_ON_CLOSE":     "http://127.0.0.1:14005/hooks/on_close",
		"SRS_VHOST_HTTP_HOOKS_ON_PUBLISH":   "http://127.0.0.1:14005/hooks/on_publish",
		"SRS_VHOST_HTTP_HOOKS_ON_UNPUBLISH": "http://127.0.0.1:14005/hooks/on_unpublish",
		"SRS_VHOST_HTTP_HOOKS_ON_PLAY":      "http://127.0.0.1:14005/hooks/on_play",
		"SRS_VHOST_HTTP_HOOKS_ON_STOP":      "http://127.0.0.1:14005/hooks/on_stop",
		"SRS_VHOST_HTTP_HOOKS_ON_DVR":       "http://127.0.0.1:14005/hooks/on_dvr",
		"SRS_VHOST_HTTP_HOOKS_ON_HLS":       "http://127.0.0.1:14005/hooks/on_hls",
		"SRS_VHOST_HTTP_HOOKS_ON_HLS_NOTIFY": "http://127.0.0.1:14005/hooks/on_hls_notify" +
			"?action=on_hls_notify&app=[app]&stream=[stream]&ts_url=[ts_url]",
	}
	if !reflect.DeepEqual(env, want) {
		t.Errorf("env %v\nwant %v", env, want)
	}
}

// TestWithHooks checks the hooks env joins a config's own env without
// changing it, and that a config may not set a hook itself.
func TestWithHooks(t *testing.T) {
	cfg := Config{Listeners: []Listener{RTMP, HTTPAPI}, Hooks: true, Env: map[string]string{"SRS_VHOST_PLAY_GOP_CACHE": "off"}}
	got, err := withHooks(cfg, "http://127.0.0.1:14005")
	if err != nil {
		t.Fatal(err)
	}
	if len(cfg.Env) != 1 {
		t.Errorf("the config's env changed: %v", cfg.Env)
	}
	if got.Env["SRS_VHOST_PLAY_GOP_CACHE"] != "off" || got.Env["SRS_VHOST_HTTP_HOOKS_ON_PUBLISH"] != "http://127.0.0.1:14005/hooks/on_publish" {
		t.Errorf("env %v", got.Env)
	}
	if _, err := BuildEnv(got, map[Listener]int{RTMP: 14000, HTTPAPI: 14001}, "/w", TLSFiles{}); err != nil {
		t.Errorf("build env: %v", err)
	}

	cfg.Env = map[string]string{"SRS_VHOST_HTTP_HOOKS_ON_PLAY": "http://x"}
	if _, err := withHooks(cfg, "http://127.0.0.1:14005"); err == nil || !strings.Contains(err.Error(), "SRS_VHOST_HTTP_HOOKS_ON_PLAY") {
		t.Errorf("error %v, want one naming the hook the config sets", err)
	}
}
