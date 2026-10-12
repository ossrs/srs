// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"reflect"
	"strconv"
	"strings"
	"testing"
)

func TestBuildEnvAPIOnly(t *testing.T) {
	env, err := BuildEnv(Config{Listeners: []Listener{HTTPAPI}}, map[Listener]int{HTTPAPI: 14000}, "/w", TLSFiles{})
	if err != nil {
		t.Fatal(err)
	}

	// Every listener but the API is off, so no SRS default port is left in use.
	want := []string{
		"SRS_CIRCUIT_BREAKER_ENABLED=off",
		"SRS_DAEMON=off",
		"SRS_EXPORTER_ENABLED=off",
		"SRS_HTTP_API_ENABLED=on",
		"SRS_HTTP_API_HTTPS_ENABLED=off",
		"SRS_HTTP_API_LISTEN=14000",
		"SRS_HTTP_SERVER_ENABLED=off",
		"SRS_HTTP_SERVER_HTTPS_ENABLED=off",
		"SRS_LOG_TANK=console",
		"SRS_PID=/w/srs.pid",
		"SRS_QUERY_LATEST_VERSION=off",
		"SRS_RTC_SERVER_ENABLED=off",
		"SRS_RTC_SERVER_TCP_ENABLED=off",
		"SRS_RTMPS_ENABLED=off",
		"SRS_RTSP_SERVER_ENABLED=off",
		"SRS_SRT_SERVER_ENABLED=off",
		"SRS_STREAM_CASTER_ENABLED=off",
	}
	if !reflect.DeepEqual(env, want) {
		t.Errorf("env =\n%s\nwant\n%s", strings.Join(env, "\n"), strings.Join(want, "\n"))
	}
}

func TestBuildEnvCommon(t *testing.T) {
	ports := map[Listener]int{}
	for i, l := range CommonListeners {
		ports[l] = 14100 + i
	}
	env, err := BuildEnv(Config{Listeners: CommonListeners}, ports, "/w", TLSFiles{Key: "/t/server.key", Cert: "/t/server.crt"})
	if err != nil {
		t.Fatal(err)
	}
	m := envMap(env)

	listen := map[Listener]string{
		RTMP:        "SRS_RTMP_LISTEN",
		HTTPAPI:     "SRS_HTTP_API_LISTEN",
		HTTPServer:  "SRS_HTTP_SERVER_LISTEN",
		RTCUDP:      "SRS_RTC_SERVER_LISTEN",
		RTCTCP:      "SRS_RTC_SERVER_TCP_LISTEN",
		SRT:         "SRS_SRT_SERVER_LISTEN",
		RTSP:        "SRS_RTSP_SERVER_LISTEN",
		Exporter:    "SRS_EXPORTER_LISTEN",
		GB:          "SRS_STREAM_CASTER_LISTEN",
		HTTPSAPI:    "SRS_HTTP_API_HTTPS_LISTEN",
		HTTPSServer: "SRS_HTTP_SERVER_HTTPS_LISTEN",
		RTMPS:       "SRS_RTMPS_LISTEN",
	}
	for l, name := range listen {
		if got, want := m[name], strconv.Itoa(ports[l]); got != want {
			t.Errorf("%s=%q, want %q for %v", name, got, want, l)
		}
	}

	on := []string{
		"SRS_HTTP_API_ENABLED", "SRS_HTTP_SERVER_ENABLED", "SRS_RTC_SERVER_ENABLED", "SRS_RTC_SERVER_TCP_ENABLED",
		"SRS_SRT_SERVER_ENABLED", "SRS_RTSP_SERVER_ENABLED", "SRS_EXPORTER_ENABLED", "SRS_STREAM_CASTER_ENABLED",
		"SRS_HTTP_API_HTTPS_ENABLED", "SRS_HTTP_SERVER_HTTPS_ENABLED", "SRS_RTMPS_ENABLED",
	}
	for _, name := range on {
		if m[name] != "on" {
			t.Errorf("%s=%q, want on", name, m[name])
		}
	}

	// SRS opens the WebRTC TCP listener only when its protocol is not udp.
	if m["SRS_RTC_SERVER_PROTOCOL"] != "all" {
		t.Errorf("SRS_RTC_SERVER_PROTOCOL=%q, want all", m["SRS_RTC_SERVER_PROTOCOL"])
	}
	if m["SRS_STREAM_CASTER_CASTER"] != "gb28181" {
		t.Errorf("SRS_STREAM_CASTER_CASTER=%q, want gb28181", m["SRS_STREAM_CASTER_CASTER"])
	}
	if want := "rtmp://127.0.0.1:" + strconv.Itoa(ports[RTMP]) + "/live/[stream]"; m["SRS_STREAM_CASTER_OUTPUT"] != want {
		t.Errorf("SRS_STREAM_CASTER_OUTPUT=%q, want %q", m["SRS_STREAM_CASTER_OUTPUT"], want)
	}
	for _, prefix := range []string{"SRS_HTTP_API_HTTPS_", "SRS_HTTP_SERVER_HTTPS_", "SRS_RTMPS_"} {
		if m[prefix+"KEY"] != "/t/server.key" || m[prefix+"CERT"] != "/t/server.crt" {
			t.Errorf("%sKEY=%q %sCERT=%q, want the test certificate", prefix, m[prefix+"KEY"], prefix, m[prefix+"CERT"])
		}
	}

	// Every port in the env is one the adapter allocated.
	allocated := map[string]bool{}
	for _, p := range ports {
		allocated[strconv.Itoa(p)] = true
	}
	for name, v := range m {
		if strings.HasSuffix(name, "_LISTEN") && !allocated[v] {
			t.Errorf("%s=%s is not an allocated port", name, v)
		}
	}
}

func TestBuildEnvUDPTS(t *testing.T) {
	env, err := BuildEnv(Config{Listeners: []Listener{RTMP, HTTPAPI, UDPTS}},
		map[Listener]int{RTMP: 1, HTTPAPI: 2, UDPTS: 3}, "/w", TLSFiles{})
	if err != nil {
		t.Fatal(err)
	}
	m := envMap(env)
	if m["SRS_STREAM_CASTER_CASTER"] != "mpegts_over_udp" || m["SRS_STREAM_CASTER_LISTEN"] != "3" ||
		m["SRS_STREAM_CASTER_OUTPUT"] != "rtmp://127.0.0.1:1/live/[stream]" {
		t.Errorf("caster env %v", env)
	}
}

func TestBuildEnvExtra(t *testing.T) {
	env, err := BuildEnv(Config{Listeners: []Listener{HTTPAPI}, Env: map[string]string{"SRS_VHOST_PLAY_GOP_CACHE": "off"}},
		map[Listener]int{HTTPAPI: 1}, "/w", TLSFiles{})
	if err != nil {
		t.Fatal(err)
	}
	if envMap(env)["SRS_VHOST_PLAY_GOP_CACHE"] != "off" {
		t.Errorf("the extra env is missing: %v", env)
	}
}

func TestBuildEnvErrors(t *testing.T) {
	tls := TLSFiles{Key: "k", Cert: "c"}
	cases := []struct {
		name  string
		cfg   Config
		ports map[Listener]int
		tls   TLSFiles
		want  string
	}{
		{"no API", Config{Listeners: []Listener{RTMP}}, map[Listener]int{RTMP: 1}, tls, "HTTP API"},
		{"no port", Config{Listeners: []Listener{HTTPAPI, RTMP}}, map[Listener]int{HTTPAPI: 1}, tls, "no port"},
		{"RTC TCP alone", Config{Listeners: []Listener{HTTPAPI, RTCTCP}}, map[Listener]int{HTTPAPI: 1, RTCTCP: 2}, tls, "needs"},
		{"HTTPS server alone", Config{Listeners: []Listener{HTTPAPI, HTTPSServer}}, map[Listener]int{HTTPAPI: 1, HTTPSServer: 2}, tls, "needs"},
		{"two casters", Config{Listeners: []Listener{HTTPAPI, RTMP, GB, UDPTS}}, map[Listener]int{HTTPAPI: 1, RTMP: 2, GB: 3, UDPTS: 4}, tls, "one stream caster"},
		{"caster without RTMP", Config{Listeners: []Listener{HTTPAPI, GB}}, map[Listener]int{HTTPAPI: 1, GB: 2}, tls, "needs"},
		{"TLS without files", Config{Listeners: []Listener{HTTPAPI, RTMPS}}, map[Listener]int{HTTPAPI: 1, RTMPS: 2}, TLSFiles{}, "certificate"},
		{"extra overrides a port", Config{Listeners: []Listener{HTTPAPI}, Env: map[string]string{"SRS_HTTP_API_LISTEN": "1985"}}, map[Listener]int{HTTPAPI: 1}, tls, "set by the adapter"},
		{"extra turns on a listener", Config{Listeners: []Listener{HTTPAPI}, Env: map[string]string{"SRS_RTMP_LISTEN": "1935"}}, map[Listener]int{HTTPAPI: 1}, tls, "set by the adapter"},
		{"extra not SRS_", Config{Listeners: []Listener{HTTPAPI}, Env: map[string]string{"PATH": "/"}}, map[Listener]int{HTTPAPI: 1}, tls, "SRS_"},
	}
	for _, c := range cases {
		_, err := BuildEnv(c.cfg, c.ports, "/w", c.tls)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s: err=%v, want it to contain %q", c.name, err, c.want)
		}
	}
}

func TestListenerNames(t *testing.T) {
	seen := map[string]bool{}
	for l := RTMP; l <= RTMPS; l++ {
		name := l.String()
		if name == "" || seen[name] {
			t.Errorf("listener %d has the name %q, empty or not unique", int(l), name)
		}
		seen[name] = true
	}
	for _, l := range []Listener{RTCUDP, SRT, UDPTS} {
		if !l.UDP() {
			t.Errorf("%v is UDP", l)
		}
	}
	for _, l := range []Listener{RTMP, HTTPAPI, RTCTCP, GB, RTMPS} {
		if l.UDP() {
			t.Errorf("%v is TCP", l)
		}
	}
}

func envMap(env []string) map[string]string {
	m := map[string]string{}
	for _, kv := range env {
		k, v, _ := strings.Cut(kv, "=")
		m[k] = v
	}
	return m
}
