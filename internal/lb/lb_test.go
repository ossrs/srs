// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"fmt"
	"strings"
	"testing"
	"time"
)

func TestOriginServerID(t *testing.T) {
	for _, tt := range []struct {
		name string
		v    *OriginServer
		want string
	}{
		{"populated", &OriginServer{ServerID: "srv", ServiceID: "svc", PID: "1234"}, "srv-svc-1234"},
		{"empty", &OriginServer{}, "--"},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.v.ID(); got != tt.want {
				t.Fatalf("ID()=%q, want %q", got, tt.want)
			}
		})
	}
}

func TestOriginServerString(t *testing.T) {
	// String() routes through Format with the %v default branch.
	v := &OriginServer{IP: "1.2.3.4", ServerID: "srv", ServiceID: "svc", PID: "p"}
	got := v.String()
	if want := "SRS ip=1.2.3.4, id=srv-svc-p"; got != want {
		t.Fatalf("String()=%q, want %q", got, want)
	}
}

func TestOriginServerFormat_ShortVerbs(t *testing.T) {
	v := &OriginServer{IP: "10.0.0.1", ServerID: "srv", ServiceID: "svc", PID: "9"}
	want := "SRS ip=10.0.0.1, id=srv-svc-9"
	for _, verb := range []string{"%v", "%s"} {
		got := fmt.Sprintf(verb, v)
		if got != want {
			t.Fatalf("Sprintf(%q)=%q, want %q", verb, got, want)
		}
	}
}

func TestOriginServerFormat_PlusVerbsAllFields(t *testing.T) {
	ts := time.Date(2026, 5, 16, 10, 30, 45, 123_000_000, time.UTC)
	v := &OriginServer{
		IP: "10.0.0.1", DeviceID: "dev1",
		ServerID: "srv", ServiceID: "svc", PID: "9",
		RTMP:      []string{":1935", ":1936"},
		HTTP:      []string{":8080"},
		API:       []string{":1985"},
		SRT:       []string{":10080"},
		RTC:       []string{":8000"},
		UpdatedAt: ts,
	}

	for _, verb := range []string{"%+v", "%+s"} {
		got := fmt.Sprintf(verb, v)
		for _, sub := range []string{
			"SRS ip=10.0.0.1",
			"id=srv-svc-9",
			"pid=9, server=srv, service=svc",
			"device=dev1",
			"rtmp=[:1935,:1936]",
			"http=[:8080]",
			"api=[:1985]",
			"srt=[:10080]",
			"rtc=[:8000]",
			"update=2026-05-16 10:30:45.123",
		} {
			if !strings.Contains(got, sub) {
				t.Fatalf("Sprintf(%q)=%q missing %q", verb, got, sub)
			}
		}
	}
}

func TestOriginServerFormat_PlusVerbMinimal(t *testing.T) {
	// Plus verb with no optional fields populated exercises the false
	// branches of every "if len(X) > 0 / X != \"\"" guard in Format.
	v := &OriginServer{ServerID: "srv", ServiceID: "svc", PID: "9"}
	got := fmt.Sprintf("%+v", v)

	if !strings.Contains(got, "pid=9, server=srv, service=svc") {
		t.Fatalf("%%+v output %q missing core ids", got)
	}
	if !strings.Contains(got, "update=") {
		t.Fatalf("%%+v output %q missing update timestamp", got)
	}
	for _, sub := range []string{"device=", "rtmp=", "http=", "api=", "srt=", "rtc="} {
		if strings.Contains(got, sub) {
			t.Fatalf("%%+v output %q should not contain %q for an empty field", got, sub)
		}
	}
}

func TestOriginServerFormat_OtherVerb(t *testing.T) {
	// A non-v/s verb falls through to the default branch, which recursively
	// formats with %v and appends ", fmt=%<verb>".
	v := &OriginServer{IP: "1.2.3.4", ServerID: "srv", ServiceID: "svc", PID: "p"}
	got := fmt.Sprintf("%d", v)
	want := "SRS ip=1.2.3.4, id=srv-svc-p, fmt=%d"
	if got != want {
		t.Fatalf("%%d output %q, want %q", got, want)
	}
}

func TestNewOriginServer(t *testing.T) {
	t.Run("no opts", func(t *testing.T) {
		v := NewOriginServer()
		if v == nil {
			t.Fatal("NewOriginServer() returned nil")
		}
		if v.IP != "" || v.DeviceID != "" || v.ServerID != "" || v.ServiceID != "" || v.PID != "" {
			t.Fatalf("expected zero value, got %+v", v)
		}
		if len(v.RTMP)+len(v.HTTP)+len(v.API)+len(v.SRT)+len(v.RTC) != 0 {
			t.Fatalf("expected empty endpoints, got %+v", v)
		}
		if !v.UpdatedAt.IsZero() {
			t.Fatalf("expected zero UpdatedAt, got %v", v.UpdatedAt)
		}
	})

	t.Run("with opts", func(t *testing.T) {
		v := NewOriginServer(
			func(s *OriginServer) { s.IP = "9.9.9.9" },
			func(s *OriginServer) { s.ServerID = "abc" },
			func(s *OriginServer) { s.RTMP = []string{":1935"} },
		)
		if v.IP != "9.9.9.9" || v.ServerID != "abc" || len(v.RTMP) != 1 || v.RTMP[0] != ":1935" {
			t.Fatalf("opts not applied: got %+v", v)
		}
	})
}
