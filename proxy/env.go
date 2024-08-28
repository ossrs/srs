// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"os"
	"path"

	"srs-proxy/errors"
	"srs-proxy/logger"

	"github.com/joho/godotenv"
)

// loadEnvFile loads the environment variables from file. Note that we only use .env file.
func loadEnvFile(ctx context.Context) error {
	if workDir, err := os.Getwd(); err != nil {
		return errors.Wrapf(err, "getpwd")
	} else {
		envFile := path.Join(workDir, ".env")
		if _, err := os.Stat(envFile); err == nil {
			if err := godotenv.Overload(envFile); err != nil {
				return errors.Wrapf(err, "load %v", envFile)
			}
		}
	}

	return nil
}

func setupDefaultEnv(ctx context.Context) {
	// Whether enable the Go pprof.
	setEnvDefault("GO_PPROF", "")
	// Force shutdown timeout.
	setEnvDefault("PROXY_FORCE_QUIT_TIMEOUT", "30s")
	// Graceful quit timeout.
	setEnvDefault("PROXY_GRACE_QUIT_TIMEOUT", "20s")

	// The HTTP API server.
	setEnvDefault("PROXY_HTTP_API", "11985")
	// The HTTP web server.
	setEnvDefault("PROXY_HTTP_SERVER", "18080")
	// The RTMP media server.
	setEnvDefault("PROXY_RTMP_SERVER", "11935")
	// The API server of proxy itself.
	setEnvDefault("PROXY_SYSTEM_API", "12025")

	// Default backend server IP, for debugging.
	//setEnvDefault("PROXY_DEFAULT_BACKEND_IP", "127.0.0.1")
	// Default backend server port, for debugging.
	//setEnvDefault("PROXY_DEFAULT_BACKEND_PORT", "1935")

	logger.Df(ctx, "load .env as GO_PPROF=%v, "+
		"PROXY_FORCE_QUIT_TIMEOUT=%v, PROXY_GRACE_QUIT_TIMEOUT=%v, "+
		"PROXY_HTTP_API=%v, PROXY_HTTP_SERVER=%v, PROXY_RTMP_SERVER=%v, "+
		"PROXY_SYSTEM_API=%v, PROXY_DEFAULT_BACKEND_IP=%v, PROXY_DEFAULT_BACKEND_PORT=%v",
		envGoPprof(),
		envForceQuitTimeout(), envGraceQuitTimeout(),
		envHttpAPI(), envHttpServer(), envRtmpServer(),
		envSystemAPI(), envDefaultBackendIP(), envDefaultBackendPort(),
	)
}
