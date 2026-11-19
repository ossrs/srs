// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/media"
)

// TestMeasureUsage refuses a measure command without its URL, file or a
// valid matrix, and fails when the server is not there.
func TestMeasureUsage(t *testing.T) {
	const api = "http://127.0.0.1:1"
	file := media.Path(t, media.H264BaselineOpus)
	for _, tc := range []struct {
		name   string
		args   []string
		code   int
		stdout string
		stderr string
	}{
		{"Help", []string{"measure", "-h"}, 0, "Usage: srs-integ measure [flags] <api-url>", ""},
		{"NoURL", []string{"measure", "-file", file}, 2, "", "Usage: srs-integ measure [flags] <api-url>"},
		{"ExtraArgs", []string{"measure", "-file", file, api, "x"}, 2, "", "Usage: srs-integ measure [flags] <api-url>"},
		{"BadFlag", []string{"measure", "-nothing", api}, 2, "", "Usage: srs-integ measure [flags] <api-url>"},
		{"NoFile", []string{"measure", api}, 2, "", "-file"},
		{"BadDelay", []string{"measure", "-file", file, "-delays", "20", api}, 2, "", "-delays"},
		{"NegativeDelay", []string{"measure", "-file", file, "-delays", "-1ms", api}, 2, "", "-delays"},
		{"BadLoss", []string{"measure", "-file", file, "-losses", "120", api}, 2, "", "-losses"},
		{"BadSped", []string{"measure", "-file", file, "-sped", "maybe", api}, 2, "", "-sped"},
		{"BadRuns", []string{"measure", "-file", file, "-runs", "0", api}, 2, "", "-runs"},
		{"BadParallel", []string{"measure", "-file", file, "-parallel", "0", api}, 2, "", "-parallel"},
		{"MissingFile", []string{"measure", "-file", "/nothing/a.mp4", api}, 1, "", "/nothing/a.mp4"},
		{"Refused", []string{"measure", "-file", file, "-timeout", "1s", api}, 1, "", "connection refused"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var stdout, stderr bytes.Buffer
			code := run(tc.args, &stdout, &stderr)
			if code != tc.code {
				t.Errorf("exit code %v, want %v; stderr %q", code, tc.code, stderr.String())
			}
			checkOutput(t, "stdout", stdout.String(), tc.stdout)
			checkOutput(t, "stderr", stderr.String(), tc.stderr)
		})
	}
}

// TestMeasureMatrix takes each loss and mode at each delay, without SPED
// before with it, so the two rows to compare are next to each other.
func TestMeasureMatrix(t *testing.T) {
	got, err := measureMatrix("0ms,150ms", "0,25", "off,on")
	if err != nil {
		t.Fatal(err)
	}
	want := []measurePoint{
		{0, 0, false}, {0, 0, true}, {0, 0.25, false}, {0, 0.25, true},
		{150 * time.Millisecond, 0, false}, {150 * time.Millisecond, 0, true},
		{150 * time.Millisecond, 0.25, false}, {150 * time.Millisecond, 0.25, true},
	}
	if len(got) != len(want) {
		t.Fatalf("matrix %v, want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("point %v is %+v, want %+v", i, got[i], want[i])
		}
	}
	if got, err := measureMatrix("20ms", "10", "on"); err != nil || len(got) != 1 || got[0] != (measurePoint{20 * time.Millisecond, 0.1, true}) {
		t.Errorf("one point %+v, %v", got, err)
	}
	for _, bad := range [][3]string{{"", "0", "off"}, {"0ms", "", "off"}, {"0ms", "0", ""}, {"0ms", "-1", "off"}, {"0ms", "x", "off"}, {"0ms", "0", "off,off"}} {
		if _, err := measureMatrix(bad[0], bad[1], bad[2]); err == nil {
			t.Errorf("matrix %q is not refused", bad)
		}
	}
}

// TestMeasurePointRuns does every run of a point, no more than parallel at
// once, keeps the samples of those that succeed, counts those that fail and
// keeps the first failure; when ctx ends, the runs not done fail.
func TestMeasurePointRuns(t *testing.T) {
	p := measurePoint{delay: 20 * time.Millisecond, sped: true}
	var now, peak, calls atomic.Int32
	r := measurePointRuns(context.Background(), p, 10, 3, func(ctx context.Context, got measurePoint) (measureSample, error) {
		if got != p {
			t.Errorf("run for %+v, want %+v", got, p)
		}
		n := calls.Add(1)
		cur := now.Add(1)
		for {
			old := peak.Load()
			if cur <= old || peak.CompareAndSwap(old, cur) {
				break
			}
		}
		time.Sleep(10 * time.Millisecond)
		now.Add(-1)
		if n%5 == 0 {
			return measureSample{}, errors.New("lost")
		}
		return measureSample{dtls: time.Duration(n) * time.Millisecond, media: time.Duration(n+1) * time.Millisecond}, nil
	})
	if calls.Load() != 10 || len(r.samples) != 8 || r.fails != 2 || r.err == nil || r.point != p {
		t.Errorf("%v calls, result %+v, want 10 runs, 8 samples and 2 failures", calls.Load(), r)
	}
	if peak.Load() != 3 {
		t.Errorf("%v runs at once, want 3", peak.Load())
	}

	ctx, cancel := context.WithCancel(context.Background())
	var once sync.Once
	r = measurePointRuns(ctx, p, 10, 1, func(ctx context.Context, _ measurePoint) (measureSample, error) {
		once.Do(cancel)
		return measureSample{dtls: time.Millisecond, media: time.Millisecond}, nil
	})
	if len(r.samples)+r.fails != 10 || r.fails < 9 || !errors.Is(r.err, context.Canceled) {
		t.Errorf("result %+v after ctx ended, want the runs not done failed", r)
	}
}

// TestPercentile is by the nearest rank: the smallest value with at least
// p percent of the values at or below it.
func TestPercentile(t *testing.T) {
	var d []time.Duration
	for i := 30; i >= 1; i-- {
		d = append(d, time.Duration(i)*time.Millisecond)
	}
	for _, tc := range []struct {
		p    int
		want time.Duration
	}{{50, 15 * time.Millisecond}, {95, 29 * time.Millisecond}, {100, 30 * time.Millisecond}, {1, time.Millisecond}} {
		if got := percentile(d, tc.p); got != tc.want {
			t.Errorf("p%v %v, want %v", tc.p, got, tc.want)
		}
	}
	if d[0] != 30*time.Millisecond {
		t.Error("percentile sorted its input")
	}
	if got := percentile([]time.Duration{7 * time.Millisecond}, 95); got != 7*time.Millisecond {
		t.Errorf("p95 of one is %v", got)
	}
	if got := percentile(nil, 50); got != 0 {
		t.Errorf("p50 of none is %v", got)
	}
}

// TestMeasureRow prints a point and its p50 and p95 in ms under the header,
// and dashes for a point with no run that succeeded.
func TestMeasureRow(t *testing.T) {
	r := measureResult{point: measurePoint{60 * time.Millisecond, 0.1, true}, fails: 1}
	for i := 1; i <= 20; i++ {
		r.samples = append(r.samples, measureSample{time.Duration(i) * time.Millisecond, time.Duration(100+i) * time.Millisecond})
	}
	if got, want := strings.Fields(measureHeader), []string{"delay_ms", "loss_%", "sped", "runs", "fails", "dtls_p50", "dtls_p95", "media_p50", "media_p95"}; strings.Join(got, " ") != strings.Join(want, " ") {
		t.Errorf("header %q, want the columns %v", measureHeader, want)
	}
	if got, want := strings.Fields(r.row()), []string{"60", "10", "on", "21", "1", "10", "19", "110", "119"}; strings.Join(got, " ") != strings.Join(want, " ") {
		t.Errorf("row %q, want %v", r.row(), want)
	}
	if len(r.row()) != len(measureHeader) {
		t.Errorf("row %q is not as wide as the header %q", r.row(), measureHeader)
	}
	none := measureResult{point: measurePoint{0, 0.25, false}, fails: 3}
	if got, want := strings.Fields(none.row()), []string{"0", "25", "off", "3", "3", "-", "-", "-", "-"}; strings.Join(got, " ") != strings.Join(want, " ") {
		t.Errorf("row %q, want %v", none.row(), want)
	}
}

// TestDelayTransport delays a request and its response by the one-way
// delay each, so the signaling takes a round trip of the path.
func TestDelayTransport(t *testing.T) {
	var reached time.Time
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		reached = time.Now()
		w.WriteHeader(http.StatusCreated)
	}))
	defer srv.Close()
	const delay = 50 * time.Millisecond
	c := &http.Client{Transport: &delayTransport{base: http.DefaultTransport, delay: delay}}
	start := time.Now()
	res, err := c.Get(srv.URL)
	if err != nil {
		t.Fatal(err)
	}
	res.Body.Close()
	if d := reached.Sub(start); d < delay {
		t.Errorf("the request reached the server after %v, want at least %v", d, delay)
	}
	if d := time.Since(start); d < 2*delay {
		t.Errorf("the response came after %v, want at least %v", d, 2*delay)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Millisecond)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, srv.URL, nil)
	if _, err := c.Do(req); !errors.Is(err, context.DeadlineExceeded) {
		t.Errorf("request with a short deadline: %v, want the deadline", err)
	}
}
