// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package debug

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/http/pprof"
	"runtime"
	"testing"
)

type ratesBody struct {
	Mutex int `json:"mutex"`
	Block int `json:"block"`
}

func getRates(t *testing.T, mux *http.ServeMux, query string) (int, ratesBody) {
	t.Helper()
	w := httptest.NewRecorder()
	mux.ServeHTTP(w, httptest.NewRequest(http.MethodGet, "/debug/pprof/rates"+query, nil))
	var body ratesBody
	if w.Code == http.StatusOK {
		if err := json.Unmarshal(w.Body.Bytes(), &body); err != nil {
			t.Fatalf("body %q: %v", w.Body.String(), err)
		}
	}
	return w.Code, body
}

func resetRates() {
	runtime.SetMutexProfileFraction(0)
	runtime.SetBlockProfileRate(0)
	blockRate.Store(0)
}

func TestProfileRatesSetAndReport(t *testing.T) {
	resetRates()
	defer resetRates()
	mux := http.NewServeMux()
	RegisterProfileRates(mux)

	code, body := getRates(t, mux, "")
	if code != http.StatusOK || body.Mutex != 0 || body.Block != 0 {
		t.Fatalf("off by default: got %d %+v", code, body)
	}

	code, body = getRates(t, mux, "?mutex=5&block=10000")
	if code != http.StatusOK || body.Mutex != 5 || body.Block != 10000 {
		t.Fatalf("set: got %d %+v", code, body)
	}
	if got := runtime.SetMutexProfileFraction(-1); got != 5 {
		t.Fatalf("runtime mutex fraction %d, want 5", got)
	}

	// One rate alone leaves the other as it is.
	code, body = getRates(t, mux, "?mutex=0")
	if code != http.StatusOK || body.Mutex != 0 || body.Block != 10000 {
		t.Fatalf("mutex off: got %d %+v", code, body)
	}
	code, body = getRates(t, mux, "?block=0")
	if code != http.StatusOK || body.Mutex != 0 || body.Block != 0 {
		t.Fatalf("block off: got %d %+v", code, body)
	}
}

// HandleGoPprof registers the rates beside net/http/pprof's own patterns on
// the default mux, which panics on a conflicting pattern.
func TestProfileRatesBesidePprofIndex(t *testing.T) {
	resetRates()
	defer resetRates()
	mux := http.NewServeMux()
	mux.HandleFunc("GET /debug/pprof/", pprof.Index)
	RegisterProfileRates(mux)

	if code, body := getRates(t, mux, "?mutex=3"); code != http.StatusOK || body.Mutex != 3 {
		t.Fatalf("got %d %+v", code, body)
	}
}

func TestGCStatsCountAGC(t *testing.T) {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /debug/pprof/", pprof.Index)
	RegisterGCStats(mux)

	get := func() map[string]uint64 {
		w := httptest.NewRecorder()
		mux.ServeHTTP(w, httptest.NewRequest(http.MethodGet, "/debug/pprof/gc", nil))
		if w.Code != http.StatusOK {
			t.Fatalf("got %d", w.Code)
		}
		var body map[string]uint64
		if err := json.Unmarshal(w.Body.Bytes(), &body); err != nil {
			t.Fatalf("body %q: %v", w.Body.String(), err)
		}
		if _, ok := body["num_gc"]; !ok {
			t.Fatalf("no num_gc in %q", w.Body.String())
		}
		if _, ok := body["pause_total_ns"]; !ok {
			t.Fatalf("no pause_total_ns in %q", w.Body.String())
		}
		return body
	}
	before := get()
	runtime.GC()
	after := get()
	if after["num_gc"] <= before["num_gc"] || after["pause_total_ns"] < before["pause_total_ns"] {
		t.Fatalf("a GC not counted: before %v, after %v", before, after)
	}
}

func TestProfileRatesRefuseBadValues(t *testing.T) {
	resetRates()
	defer resetRates()
	mux := http.NewServeMux()
	RegisterProfileRates(mux)

	for _, query := range []string{"?mutex=-1", "?block=-5", "?mutex=x", "?block=1.5"} {
		if code, _ := getRates(t, mux, query); code != http.StatusBadRequest {
			t.Errorf("%s: got %d, want 400", query, code)
		}
	}
	if _, body := getRates(t, mux, ""); body.Mutex != 0 || body.Block != 0 {
		t.Fatalf("a refused request changed the rates: %+v", body)
	}
}
