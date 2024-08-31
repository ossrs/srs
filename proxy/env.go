// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"os"
	"path"

	"github.com/joho/godotenv"

	"srs-proxy/errors"
	"srs-proxy/logger"
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

	// The load balancer, use redis or memory.
	setEnvDefault("PROXY_LOAD_BALANCER_TYPE", "memory")
	// The redis server host.
	setEnvDefault("PROXY_REDIS_HOST", "127.0.0.1")
	// The redis server port.
	setEnvDefault("PROXY_REDIS_PORT", "6379")
	// The redis server password.
	setEnvDefault("PROXY_REDIS_PASSWORD", "")
	// The redis server db.
	setEnvDefault("PROXY_REDIS_DB", "0")

	// Whether enable the default backend server, for debugging.
	setEnvDefault("PROXY_DEFAULT_BACKEND_ENABLED", "off")
	// Default backend server IP, for debugging.
	setEnvDefault("PROXY_DEFAULT_BACKEND_IP", "127.0.0.1")
	// Default backend server port, for debugging.
	setEnvDefault("PROXY_DEFAULT_BACKEND_RTMP", "1935")

	logger.Df(ctx, "load .env as GO_PPROF=%v, "+
		"PROXY_FORCE_QUIT_TIMEOUT=%v, PROXY_GRACE_QUIT_TIMEOUT=%v, "+
		"PROXY_HTTP_API=%v, PROXY_HTTP_SERVER=%v, PROXY_RTMP_SERVER=%v, "+
		"PROXY_SYSTEM_API=%v, PROXY_DEFAULT_BACKEND_ENABLED=%v, "+
		"PROXY_DEFAULT_BACKEND_IP=%v, PROXY_DEFAULT_BACKEND_RTMP=%v, "+
		"PROXY_DEFAULT_BACKEND_HTTP=%v, "+
		"PROXY_LOAD_BALANCER_TYPE=%v, PROXY_REDIS_HOST=%v, PROXY_REDIS_PORT=%v, "+
		"PROXY_REDIS_PASSWORD=%v, PROXY_REDIS_DB=%v",
		envGoPprof(),
		envForceQuitTimeout(), envGraceQuitTimeout(),
		envHttpAPI(), envHttpServer(), envRtmpServer(),
		envSystemAPI(), envDefaultBackendEnabled(),
		envDefaultBackendIP(), envDefaultBackendRTMP(),
		envDefaultBackendHttp(),
		envLoadBalancerType(), envRedisHost(), envRedisPort(),
		envRedisPassword(), envRedisDB(),
	)
}

func envDefaultBackendHttp() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_HTTP")
}

func envRedisDB() string {
	return os.Getenv("PROXY_REDIS_DB")
}

func envRedisPassword() string {
	return os.Getenv("PROXY_REDIS_PASSWORD")
}

func envRedisPort() string {
	return os.Getenv("PROXY_REDIS_PORT")
}

func envRedisHost() string {
	return os.Getenv("PROXY_REDIS_HOST")
}

func envLoadBalancerType() string {
	return os.Getenv("PROXY_LOAD_BALANCER_TYPE")
}

func envDefaultBackendRTMP() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_RTMP")
}

func envDefaultBackendIP() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_IP")
}

func envDefaultBackendEnabled() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_ENABLED")
}

func envGraceQuitTimeout() string {
	return os.Getenv("PROXY_GRACE_QUIT_TIMEOUT")
}

func envForceQuitTimeout() string {
	return os.Getenv("PROXY_FORCE_QUIT_TIMEOUT")
}

func envGoPprof() string {
	return os.Getenv("GO_PPROF")
}

func envSystemAPI() string {
	return os.Getenv("PROXY_SYSTEM_API")
}

func envRtmpServer() string {
	return os.Getenv("PROXY_RTMP_SERVER")
}

func envHttpServer() string {
	return os.Getenv("PROXY_HTTP_SERVER")
}

func envHttpAPI() string {
	return os.Getenv("PROXY_HTTP_API")
}

// setEnvDefault set env key=value if not set.
func setEnvDefault(key, value string) {
	if os.Getenv(key) == "" {
		os.Setenv(key, value)
	}
}
