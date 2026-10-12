// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"reflect"
	"strings"
	"testing"
	"time"
)

// TestHeartbeatEnv checks the env of a heartbeat: on, POSTing to the mock's
// HeartbeatPath with its query, each setting it has, and the bearer
// authentication only with a token.
func TestHeartbeatEnv(t *testing.T) {
	for _, tt := range []struct {
		name string
		h    Heartbeat
		want map[string]string
	}{
		{"Defaults", Heartbeat{}, map[string]string{
			"SRS_HEARTBEAT_ENABLED":   "on",
			"SRS_HEARTBEAT_URL":       "http://127.0.0.1:14005/api/v1/heartbeat",
			"SRS_HEARTBEAT_SUMMARIES": "off",
			"SRS_HEARTBEAT_PORTS":     "off",
		}},
		{"Every", Heartbeat{Interval: time.Second, DeviceID: "d1", IP: "10.0.0.1", Summaries: true, Ports: true, Token: "t1", Query: "k=v"}, map[string]string{
			"SRS_HEARTBEAT_ENABLED":      "on",
			"SRS_HEARTBEAT_URL":          "http://127.0.0.1:14005/api/v1/heartbeat?k=v",
			"SRS_HEARTBEAT_INTERVAL":     "1",
			"SRS_HEARTBEAT_DEVICE_ID":    "d1",
			"SRS_DEVICE_IP":              "10.0.0.1",
			"SRS_HEARTBEAT_SUMMARIES":    "on",
			"SRS_HEARTBEAT_PORTS":        "on",
			"SRS_HEARTBEAT_AUTH_ENABLED": "on",
			"SRS_HEARTBEAT_AUTH_TYPE":    "bearer",
			"SRS_HEARTBEAT_AUTH_TOKEN":   "t1",
		}},
	} {
		t.Run(tt.name, func(t *testing.T) {
			h := tt.h
			got, err := heartbeatEnv(&h, "http://127.0.0.1:14005")
			if err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(got, tt.want) {
				t.Errorf("env %v\nwant %v", got, tt.want)
			}
		})
	}

	if _, err := heartbeatEnv(&Heartbeat{Interval: 1500 * time.Millisecond}, "http://127.0.0.1:14005"); err == nil {
		t.Errorf("an interval of 1.5s accepted, want an error: SRS reads whole seconds")
	}
	if got, err := heartbeatEnv(nil, "http://127.0.0.1:14005"); err != nil || got != nil {
		t.Errorf("no heartbeat: env %v, error %v, want none", got, err)
	}
}

// TestWithHeartbeat checks a config's Heartbeat joins the hooks env, that a
// config may not set the heartbeat itself then, and that it needs the hooks'
// mock.
func TestWithHeartbeat(t *testing.T) {
	cfg := Config{Listeners: []Listener{RTMP, HTTPAPI}, Hooks: true, Heartbeat: &Heartbeat{Interval: time.Second}}
	got, err := withHooks(cfg, "http://127.0.0.1:14005")
	if err != nil {
		t.Fatal(err)
	}
	if got.Env["SRS_HEARTBEAT_URL"] != "http://127.0.0.1:14005/api/v1/heartbeat" || got.Env["SRS_HEARTBEAT_INTERVAL"] != "1" {
		t.Errorf("env %v", got.Env)
	}
	if got.Env["SRS_VHOST_HTTP_HOOKS_ON_PUBLISH"] == "" {
		t.Errorf("the hooks env is gone: %v", got.Env)
	}
	if _, err := BuildEnv(got, map[Listener]int{RTMP: 14000, HTTPAPI: 14001}, "/w", TLSFiles{}); err != nil {
		t.Errorf("build env: %v", err)
	}

	cfg.Env = map[string]string{"SRS_HEARTBEAT_DEVICE_ID": "x"}
	if _, err := withHooks(cfg, "http://127.0.0.1:14005"); err == nil || !strings.Contains(err.Error(), "SRS_HEARTBEAT_DEVICE_ID") {
		t.Errorf("error %v, want one naming the heartbeat env the config sets", err)
	}

	cfg = Config{Listeners: []Listener{RTMP, HTTPAPI}, Heartbeat: &Heartbeat{}}
	if _, err := BuildEnv(cfg, map[Listener]int{RTMP: 14000, HTTPAPI: 14001}, "/w", TLSFiles{}); err == nil || !strings.Contains(err.Error(), "Hooks") {
		t.Errorf("error %v, want one saying Heartbeat needs Hooks", err)
	}

	// Without Heartbeat, a config may set the heartbeat env, such as one SRS
	// refuses to start with.
	cfg = Config{Listeners: []Listener{RTMP, HTTPAPI}, Env: map[string]string{"SRS_HEARTBEAT_AUTH_ENABLED": "on"}}
	if _, err := BuildEnv(cfg, map[Listener]int{RTMP: 14000, HTTPAPI: 14001}, "/w", TLSFiles{}); err != nil {
		t.Errorf("build env: %v", err)
	}
}
