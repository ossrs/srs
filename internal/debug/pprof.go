// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package debug

import (
	"context"
	"net/http"
	_ "net/http/pprof"

	"srsx/internal/env"
	"srsx/internal/logger"
)

func HandleGoPprof(ctx context.Context, environment env.ProxyEnvironment) {
	if addr := environment.GoPprof(); addr != "" {
		go func() {
			logger.Debug(ctx, "Start Go pprof at %v", addr)
			http.ListenAndServe(addr, nil)
		}()
	}
}
