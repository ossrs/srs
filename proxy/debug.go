// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"net/http"

	"srs-proxy/logger"
)

func handleGoPprof(ctx context.Context) {
	if addr := envGoPprof(); addr != "" {
		go func() {
			logger.Df(ctx, "Start Go pprof at %v", addr)
			http.ListenAndServe(addr, nil)
		}()
	}
}
