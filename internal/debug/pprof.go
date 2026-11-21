// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package debug

import (
	"context"
	"encoding/json"
	"net/http"
	_ "net/http/pprof"
	"runtime"
	"strconv"
	"sync/atomic"

	"srsx/internal/env"
	"srsx/internal/logger"
)

// The runtime has no getter for the block profile rate.
var blockRate atomic.Int64

func HandleGoPprof(ctx context.Context, environment env.ProxyEnvironment) {
	if addr := environment.GoPprof(); addr != "" {
		RegisterProfileRates(http.DefaultServeMux)
		RegisterGCStats(http.DefaultServeMux)
		go func() {
			logger.Debug(ctx, "Start Go pprof at %v", addr)
			http.ListenAndServe(addr, nil)
		}()
	}
}

// The runtime samples one in n mutex contention events and one blocking event
// per ns blocked, 0 for off. Both are off by default, so a profiler turns them
// on only while it records, since sampling costs the measured process.
func RegisterProfileRates(mux *http.ServeMux) {
	// GET like net/http/pprof's "GET /debug/pprof/", or the mux refuses the
	// pair as conflicting.
	mux.HandleFunc("GET /debug/pprof/rates", func(w http.ResponseWriter, r *http.Request) {
		mutex, okMutex := parseRate(r, "mutex")
		block, okBlock := parseRate(r, "block")
		if !okMutex || !okBlock {
			http.Error(w, "mutex and block must be integers of 0 or more", http.StatusBadRequest)
			return
		}
		if mutex >= 0 {
			runtime.SetMutexProfileFraction(mutex)
		}
		if block >= 0 {
			runtime.SetBlockProfileRate(block)
			blockRate.Store(int64(block))
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(struct {
			Mutex int   `json:"mutex"`
			Block int64 `json:"block"`
		}{runtime.SetMutexProfileFraction(-1), blockRate.Load()})
	})
}

// A profiler reads the GC count and pause total before and after it records.
// The text heap profile has the count but no pause total.
func RegisterGCStats(mux *http.ServeMux) {
	mux.HandleFunc("GET /debug/pprof/gc", func(w http.ResponseWriter, r *http.Request) {
		var stats runtime.MemStats
		runtime.ReadMemStats(&stats)

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(struct {
			NumGC        uint32 `json:"num_gc"`
			PauseTotalNs uint64 `json:"pause_total_ns"`
		}{stats.NumGC, stats.PauseTotalNs})
	})
}

func parseRate(r *http.Request, name string) (int, bool) {
	value := r.URL.Query().Get(name)
	if value == "" {
		return -1, true
	}
	rate, err := strconv.Atoi(value)
	if err != nil || rate < 0 {
		return 0, false
	}
	return rate, true
}
