// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"encoding/json"
	"net/http"
	"os"
	"srs-proxy/logger"
)

// setEnvDefault set env key=value if not set.
func setEnvDefault(key, value string) {
	if os.Getenv(key) == "" {
		os.Setenv(key, value)
	}
}

func envHttpAPI() string {
	return os.Getenv("PROXY_HTTP_API")
}

func envHttpServer() string {
	return os.Getenv("PROXY_HTTP_SERVER")
}

func envGoPprof() string {
	return os.Getenv("GO_PPROF")
}

func envForceQuitTimeout() string {
	return os.Getenv("PROXY_FORCE_QUIT_TIMEOUT")
}

func envGraceQuitTimeout() string {
	return os.Getenv("PROXY_GRACE_QUIT_TIMEOUT")
}

func apiResponse(ctx context.Context, w http.ResponseWriter, r *http.Request, data any) {
	b, err := json.Marshal(data)
	if err != nil {
		logger.Wf(ctx, "marshal %v err %v", data, err)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	w.Write(b)
}
