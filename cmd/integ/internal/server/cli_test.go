// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"errors"
	"slices"
	"strings"
	"sync"
	"testing"
	"time"
)

// TestCLIEnv checks the env of a run of the binary: the process settings,
// every listener that has an enable var off and no port set, then the run's
// own settings, which may set anything of SRS's.
func TestCLIEnv(t *testing.T) {
	env, err := CLIEnv("/tmp/x", map[string]string{
		"SRS_RTMP_LISTEN":      "1000",
		"SRS_HTTP_API_ENABLED": "on",
		"SRS_MAX_CONNECTIONS":  "0",
	})
	if err != nil {
		t.Fatal(err)
	}
	if !slices.IsSorted(env) {
		t.Errorf("env not sorted: %v", env)
	}
	for _, want := range []string{
		"SRS_DAEMON=off",
		"SRS_LOG_TANK=console",
		"SRS_PID=/tmp/x/srs.pid",
		"SRS_QUERY_LATEST_VERSION=off",
		"SRS_CIRCUIT_BREAKER_ENABLED=off",
		"SRS_STREAM_CASTER_ENABLED=off",
		"SRS_HTTP_SERVER_ENABLED=off",
		"SRS_RTC_SERVER_ENABLED=off",
		"SRS_RTC_SERVER_TCP_ENABLED=off",
		"SRS_SRT_SERVER_ENABLED=off",
		"SRS_RTSP_SERVER_ENABLED=off",
		"SRS_EXPORTER_ENABLED=off",
		"SRS_HTTP_API_HTTPS_ENABLED=off",
		"SRS_HTTP_SERVER_HTTPS_ENABLED=off",
		"SRS_RTMPS_ENABLED=off",
		// The run's own settings replace the adapter's.
		"SRS_HTTP_API_ENABLED=on",
		"SRS_RTMP_LISTEN=1000",
		"SRS_MAX_CONNECTIONS=0",
	} {
		if !slices.Contains(env, want) {
			t.Errorf("env lacks %s: %v", want, env)
		}
	}
	if slices.Contains(env, "SRS_HTTP_API_ENABLED=off") {
		t.Errorf("env keeps the adapter's SRS_HTTP_API_ENABLED: %v", env)
	}
	for _, kv := range env {
		if strings.HasSuffix(strings.SplitN(kv, "=", 2)[0], "_LISTEN") && kv != "SRS_RTMP_LISTEN=1000" {
			t.Errorf("env sets a port the run didn't: %s", kv)
		}
	}

	if _, err := CLIEnv("/tmp/x", map[string]string{"PATH": "/bin"}); err == nil {
		t.Error("env PATH accepted, want only SRS_ settings")
	}
}

// TestWaitBelowPeak checks the gate of a command run: it goes at once while
// the process runs fewer SRS than its peak, and waits, up to its limit, while
// the process is at its peak, until a server exits.
func TestWaitBelowPeak(t *testing.T) {
	var p procCount
	p.started()
	p.started()
	p.exited()
	if !p.waitBelowPeak(time.Second, time.Millisecond) {
		t.Error("1 live of a peak of 2: waited, want at once")
	}

	p.started()
	start := time.Now()
	if p.waitBelowPeak(30*time.Millisecond, time.Millisecond) {
		t.Error("2 live of a peak of 2: went, want the wait to time out")
	}
	if d := time.Since(start); d < 30*time.Millisecond {
		t.Errorf("waited %v, want 30ms", d)
	}

	go func() {
		time.Sleep(20 * time.Millisecond)
		p.exited()
	}()
	if !p.waitBelowPeak(time.Second, time.Millisecond) {
		t.Error("a server exited during the wait: timed out, want to go")
	}
}

// TestCLIEnvTZ checks a run may set its time zone, TZ, beside SRS's settings.
func TestCLIEnvTZ(t *testing.T) {
	env, err := CLIEnv("/tmp/x", map[string]string{"TZ": "INTEG-5:45", "SRS_UTC_TIME": "on"})
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"TZ=INTEG-5:45", "SRS_UTC_TIME=on"} {
		if !slices.Contains(env, want) {
			t.Errorf("env lacks %s: %v", want, env)
		}
	}
}

// TestStartGated checks a gated start: it starts and counts the process at
// once while the process runs fewer SRS than its peak, waits while it is at
// its peak until a server exits, and counts nothing when the start fails.
func TestStartGated(t *testing.T) {
	var p procCount
	var mu sync.Mutex
	p.started()
	p.started()
	p.exited()

	starts := 0
	start := func() error { starts++; return nil }
	if err := p.startGated(&mu, time.Second, start); err != nil || starts != 1 {
		t.Fatalf("below the peak: err=%v starts=%d, want 1 start", err, starts)
	}
	if p.live.Load() != 2 || p.peak.Load() != 2 {
		t.Errorf("live %d peak %d, want 2 and 2", p.live.Load(), p.peak.Load())
	}

	go func() {
		time.Sleep(30 * time.Millisecond)
		p.exited()
	}()
	begin := time.Now()
	if err := p.startGated(&mu, time.Second, start); err != nil || starts != 2 {
		t.Fatalf("at the peak: err=%v starts=%d, want 2 starts", err, starts)
	}
	if d := time.Since(begin); d < 30*time.Millisecond {
		t.Errorf("started after %v, want after the exit at 30ms", d)
	}
	if p.peak.Load() != 2 {
		t.Errorf("peak %d, want 2: the gate raised it", p.peak.Load())
	}

	p.exited()
	if err := p.startGated(&mu, time.Second, func() error { return errors.New("no binary") }); err == nil {
		t.Error("a failed start: no error")
	}
	if p.live.Load() != 1 || p.total.Load() != 4 {
		t.Errorf("live %d total %d after a failed start, want 1 and 4", p.live.Load(), p.total.Load())
	}
}
