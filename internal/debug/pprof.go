// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package debug

import (
	"context"
	"net/http"

	"srsx/internal/env"
	"srsx/internal/logger"
)

func HandleGoPprof(ctx context.Context, environment env.Environment) {
	if addr := environment.GoPprof(); addr != "" {
		go func() {
			logger.Df(ctx, "Start Go pprof at %v", addr)
			http.ListenAndServe(addr, nil)
		}()
	}
}
