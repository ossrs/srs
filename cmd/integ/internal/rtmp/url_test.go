// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import "testing"

func TestParseURL(t *testing.T) {
	cases := []struct {
		raw  string
		want URL
	}{
		{"rtmp://127.0.0.1:1935/live/livestream", URL{Scheme: "rtmp", Addr: "127.0.0.1:1935", TcURL: "rtmp://127.0.0.1:1935/live", App: "live", Stream: "livestream"}},
		{"rtmp://localhost/live/a", URL{Scheme: "rtmp", Addr: "localhost:1935", TcURL: "rtmp://localhost/live", App: "live", Stream: "a"}},
		{"rtmps://localhost/live/a", URL{Scheme: "rtmps", Addr: "localhost:443", TcURL: "rtmps://localhost/live", App: "live", Stream: "a"}},
		{"rtmp://127.0.0.1:14000/live/a/b/stream", URL{Scheme: "rtmp", Addr: "127.0.0.1:14000", TcURL: "rtmp://127.0.0.1:14000/live/a/b", App: "live/a/b", Stream: "stream"}},
		{"rtmp://127.0.0.1:14000/live/a?token=x&vhost=v", URL{Scheme: "rtmp", Addr: "127.0.0.1:14000", TcURL: "rtmp://127.0.0.1:14000/live", App: "live", Stream: "a?token=x&vhost=v"}},
		{"rtmp://[::1]:14000/live/a", URL{Scheme: "rtmp", Addr: "[::1]:14000", TcURL: "rtmp://[::1]:14000/live", App: "live", Stream: "a"}},
	}
	for _, tc := range cases {
		t.Run(tc.raw, func(t *testing.T) {
			got, err := ParseURL(tc.raw)
			if err != nil {
				t.Fatalf("parse: %v", err)
			}
			if got != tc.want {
				t.Errorf("got %+v, want %+v", got, tc.want)
			}
		})
	}
}

func TestParseURLErrors(t *testing.T) {
	for _, raw := range []string{
		"http://127.0.0.1/live/a",
		"rtmp://127.0.0.1/live",
		"rtmp://127.0.0.1/",
		"rtmp:///live/a",
		"rtmp://127.0.0.1/live/",
		"::bad",
	} {
		t.Run(raw, func(t *testing.T) {
			if got, err := ParseURL(raw); err == nil {
				t.Errorf("got %+v, want an error", got)
			}
		})
	}
}
