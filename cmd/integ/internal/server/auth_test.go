// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"errors"
	"net/http"
	"strings"
	"testing"
)

// TestAPIAuthEnv checks the env of each authentication: every value that is
// set, and nothing for a value that is not, so SRS sees a missing one.
func TestAPIAuthEnv(t *testing.T) {
	tests := []struct {
		name string
		auth *APIAuth
		want map[string]string
	}{
		{"Off", nil, nil},
		{"Basic", &APIAuth{Type: "basic", Username: "admin", Password: "secret"}, map[string]string{
			"SRS_HTTP_API_AUTH_ENABLED":  "on",
			"SRS_HTTP_API_AUTH_TYPE":     "basic",
			"SRS_HTTP_API_AUTH_USERNAME": "admin",
			"SRS_HTTP_API_AUTH_PASSWORD": "secret",
		}},
		{"BearerRTC", &APIAuth{Type: "bearer", Token: "t1", RTC: true}, map[string]string{
			"SRS_HTTP_API_AUTH_ENABLED":            "on",
			"SRS_HTTP_API_AUTH_TYPE":               "bearer",
			"SRS_HTTP_API_AUTH_TOKEN":              "t1",
			"SRS_HTTP_API_AUTH_RTC_BEARER_ENABLED": "on",
		}},
		{"NoType", &APIAuth{}, map[string]string{"SRS_HTTP_API_AUTH_ENABLED": "on"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := authEnv(tt.auth)
			if len(got) != len(tt.want) {
				t.Errorf("env %v, want %v", got, tt.want)
			}
			for k, v := range tt.want {
				if got[k] != v {
					t.Errorf("%s=%q, want %q", k, got[k], v)
				}
			}
		})
	}
}

// TestAPIAuthBuildEnv checks that BuildEnv adds the authentication, and that a
// config's own env may not set it.
func TestAPIAuthBuildEnv(t *testing.T) {
	ports := map[Listener]int{HTTPAPI: 14000}
	cfg := Config{Listeners: []Listener{HTTPAPI}, APIAuth: &APIAuth{Type: "bearer", Token: "t1"}}
	env, err := BuildEnv(cfg, ports, "/w", TLSFiles{})
	if err != nil {
		t.Fatal(err)
	}
	m := envMap(env)
	if m["SRS_HTTP_API_AUTH_TYPE"] != "bearer" || m["SRS_HTTP_API_AUTH_TOKEN"] != "t1" {
		t.Errorf("env %v, want the bearer authentication", env)
	}

	cfg = Config{Listeners: []Listener{HTTPAPI}, Env: map[string]string{"SRS_HTTP_API_AUTH_ENABLED": "on"}}
	if _, err := BuildEnv(cfg, ports, "/w", TLSFiles{}); err == nil || !strings.Contains(err.Error(), "SRS_HTTP_API_AUTH_ENABLED") {
		t.Errorf("error %v, want one naming the authentication env the config sets", err)
	}
}

// TestAuthorization checks the Authorization header of each authentication.
func TestAuthorization(t *testing.T) {
	tests := []struct {
		name string
		auth *APIAuth
		want string
	}{
		{"Off", nil, ""},
		{"Basic", &APIAuth{Type: "basic", Username: "admin", Password: "secret"}, "Basic YWRtaW46c2VjcmV0"},
		{"Bearer", &APIAuth{Type: "bearer", Token: "t1"}, "Bearer t1"},
		{"Unknown", &APIAuth{Type: "digest"}, ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.auth.Authorization(); got != tt.want {
				t.Errorf("Authorization() = %q, want %q", got, tt.want)
			}
			s := &Server{auth: tt.auth}
			req, _ := http.NewRequest(http.MethodGet, "http://127.0.0.1/api/v1/versions", nil)
			s.Authorize(req)
			if got := req.Header.Get("Authorization"); got != tt.want {
				t.Errorf("request header %q, want %q", got, tt.want)
			}
		})
	}
}

// counter returns an allocator that hands out 14000, 14001 and so on.
func counter() func() (int, error) {
	next := 14000
	return func() (int, error) {
		next++
		return next - 1, nil
	}
}

// TestListenerPorts checks that each listener gets a port of its own, and
// that OneHTTPPort makes the HTTP and HTTPS servers share the API's ports.
func TestListenerPorts(t *testing.T) {
	cfg := Config{Listeners: []Listener{RTMP, HTTPAPI, HTTPServer, HTTPSAPI, HTTPSServer}}
	ports, err := listenerPorts(cfg, counter())
	if err != nil {
		t.Fatal(err)
	}
	seen := map[int]bool{}
	for _, l := range cfg.Listeners {
		if ports[l] == 0 || seen[ports[l]] {
			t.Errorf("%v has port %d, want a new one: %v", l, ports[l], ports)
		}
		seen[ports[l]] = true
	}

	cfg.OneHTTPPort = true
	if ports, err = listenerPorts(cfg, counter()); err != nil {
		t.Fatal(err)
	}
	if ports[HTTPServer] != ports[HTTPAPI] || ports[HTTPSServer] != ports[HTTPSAPI] || ports[HTTPAPI] == ports[HTTPSAPI] || ports[RTMP] == ports[HTTPAPI] {
		t.Errorf("ports %v, want the servers on the APIs' ports and the rest apart", ports)
	}
	if len(seen) != 5 {
		t.Errorf("listeners %d, want 5", len(seen))
	}
	env, err := BuildEnv(cfg, ports, "/w", TLSFiles{Key: "k", Cert: "c"})
	if err != nil {
		t.Fatal(err)
	}
	m := envMap(env)
	if m["SRS_HTTP_API_LISTEN"] != m["SRS_HTTP_SERVER_LISTEN"] || m["SRS_HTTP_API_HTTPS_LISTEN"] != m["SRS_HTTP_SERVER_HTTPS_LISTEN"] {
		t.Errorf("env %v, want the API and server listens the same", env)
	}

	failed := errors.New("exhausted")
	if _, err := listenerPorts(cfg, func() (int, error) { return 0, failed }); !errors.Is(err, failed) {
		t.Errorf("error %v, want the allocator's", err)
	}
}

// TestOneHTTPPortErrors checks that OneHTTPPort needs the HTTP server, and
// the HTTPS server and API together, each pair on one port.
func TestOneHTTPPortErrors(t *testing.T) {
	tests := []struct {
		name  string
		ls    []Listener
		ports map[Listener]int
		want  string
	}{
		{"NoServer", []Listener{HTTPAPI}, map[Listener]int{HTTPAPI: 14000}, "HTTP server"},
		{"HttpsApiAlone", []Listener{HTTPAPI, HTTPServer, HTTPSAPI}, map[Listener]int{HTTPAPI: 14000, HTTPServer: 14000, HTTPSAPI: 14001}, "HTTPS"},
		{"HttpsServerAlone", []Listener{HTTPAPI, HTTPServer, HTTPSServer}, map[Listener]int{HTTPAPI: 14000, HTTPServer: 14000, HTTPSServer: 14001}, "HTTPS"},
		{"OtherPorts", []Listener{HTTPAPI, HTTPServer}, map[Listener]int{HTTPAPI: 14000, HTTPServer: 14001}, "port"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cfg := Config{Listeners: tt.ls, OneHTTPPort: true}
			if _, err := BuildEnv(cfg, tt.ports, "/w", TLSFiles{Key: "k", Cert: "c"}); err == nil || !strings.Contains(err.Error(), tt.want) {
				t.Errorf("error %v, want one about %s", err, tt.want)
			}
		})
	}
}

// TestExitError checks that a server that exits before it is ready is told
// by its exit code.
func TestExitError(t *testing.T) {
	err := error(&ExitError{Code: 255})
	var e *ExitError
	if !errors.As(err, &e) || e.Code != 255 || !strings.Contains(err.Error(), "255") {
		t.Errorf("error %v, want an ExitError with code 255", err)
	}
}
