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
// path, and on_hls_notify, a GET, carries its action, the server ids and every
// variable of the segment in the query.
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
			"?action=on_hls_notify&server_id=[server_id]&service_id=[service_id]&app=[app]&stream=[stream]&ts_url=[ts_url]&param=[param]",
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

// TestHookURLs checks a config's HookURLs: each hook it names calls every
// base in order, a path on the mock or HookDown, the port nothing listens
// on; every other hook calls the mock at /hooks.
func TestHookURLs(t *testing.T) {
	cfg := Config{Listeners: []Listener{RTMP, HTTPAPI}, Hooks: true, HookURLs: map[string][]string{
		"on_publish": {"/hooks", "/more"},
		"on_play":    {HookDown},
	}}
	got, err := withHookURLs(cfg, "http://127.0.0.1:14005", "http://127.0.0.1:14009")
	if err != nil {
		t.Fatal(err)
	}
	for k, v := range map[string]string{
		"SRS_VHOST_HTTP_HOOKS_ON_PUBLISH": "http://127.0.0.1:14005/hooks/on_publish http://127.0.0.1:14005/more/on_publish",
		"SRS_VHOST_HTTP_HOOKS_ON_PLAY":    "http://127.0.0.1:14009/hooks/on_play",
		"SRS_VHOST_HTTP_HOOKS_ON_STOP":    "http://127.0.0.1:14005/hooks/on_stop",
	} {
		if got.Env[k] != v {
			t.Errorf("%s=%q, want %q", k, got.Env[k], v)
		}
	}
	if !cfg.downHooks() {
		t.Errorf("a config with HookDown has no down hook")
	}

	for _, bad := range []map[string][]string{
		{"on_nothing": {"/hooks"}},
		{"on_publish": {}},
		{"on_publish": {"more"}},
	} {
		cfg.HookURLs = bad
		if _, err := withHookURLs(cfg, "http://127.0.0.1:14005", ""); err == nil {
			t.Errorf("HookURLs %v accepted, want an error", bad)
		}
	}
	cfg.HookURLs = map[string][]string{"on_publish": {"/more"}}
	if cfg.downHooks() {
		t.Errorf("a config without HookDown has a down hook")
	}
}
