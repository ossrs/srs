// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// The session check is off unless asked for, so the scripts that only play keep the best-effort delete.
func TestRequireSessionFromEnvDefaultsToOff(t *testing.T) {
	for _, value := range []string{"", "off"} {
		require, err := requireSessionFromEnv(func(string) string { return value })
		if err != nil {
			t.Fatalf("PION_WHEP_REQUIRE_SESSION=%q: %v", value, err)
		}
		if require {
			t.Errorf("PION_WHEP_REQUIRE_SESSION=%q: require = true, want false", value)
		}
	}
}

func TestRequireSessionFromEnvOn(t *testing.T) {
	env := map[string]string{envRequireSession: "on"}
	require, err := requireSessionFromEnv(func(key string) string { return env[key] })
	if err != nil {
		t.Fatalf("PION_WHEP_REQUIRE_SESSION=on: %v", err)
	}
	if !require {
		t.Errorf("PION_WHEP_REQUIRE_SESSION=on: require = false, want true")
	}
}

// Any other value is rejected by name, like a bad option, never ignored.
func TestRequireSessionFromEnvRejectsOtherValues(t *testing.T) {
	for _, value := range []string{"1", "true", "ON"} {
		_, err := requireSessionFromEnv(func(string) string { return value })
		want := "Invalid value '" + value + "' for PION_WHEP_REQUIRE_SESSION; use on or off"
		if err == nil || !strings.Contains(err.Error(), want) {
			t.Errorf("PION_WHEP_REQUIRE_SESSION=%q: error = %v, want it to contain %q", value, err, want)
		}
	}
}

// A WHEP server names the session by the Location of its answer, and the client ends the session with a DELETE
// to it (RFC 9725). When the session is required, a missing Location or a refused DELETE fails the run, so a
// server or proxy that loses the Location cannot pass by playing alone.
func TestEndSession(t *testing.T) {
	var methods []string
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		methods = append(methods, r.Method)
		if r.URL.Query().Get("session") == "bad" {
			w.WriteHeader(http.StatusInternalServerError)
			return
		}
		w.WriteHeader(http.StatusOK)
	}))
	defer server.Close()
	good := server.URL + "/rtc/v1/whep/?action=delete&session=good"
	bad := server.URL + "/rtc/v1/whep/?action=delete&session=bad"

	cases := []struct {
		name       string
		sessionURL string
		require    bool
		wantErr    string
	}{
		{"RequiredDeleted", good, true, ""},
		{"RequiredNoLocation", "", true, "no Location"},
		{"RequiredDeleteRefused", bad, true, "500"},
		{"BestEffortNoLocation", "", false, ""},
		{"BestEffortDeleteRefused", bad, false, ""},
	}
	lg := newLogger("quiet")
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			methods = nil
			err := endSession(tc.sessionURL, tc.require, lg)
			if tc.wantErr == "" && err != nil {
				t.Fatalf("endSession: %v, want no error", err)
			}
			if tc.wantErr != "" && (err == nil || !strings.Contains(err.Error(), tc.wantErr)) {
				t.Fatalf("endSession: error = %v, want it to contain %q", err, tc.wantErr)
			}
			if tc.sessionURL != "" && (len(methods) != 1 || methods[0] != http.MethodDelete) {
				t.Fatalf("server got %v, want one DELETE", methods)
			}
		})
	}
}
