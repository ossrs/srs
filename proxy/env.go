// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"io"
	"os"
	"path"
	"strings"

	"srs-proxy/errors"
	"srs-proxy/logger"
)

// loadEnvFile loads the environment variables from file. Note that we only use .env file.
func loadEnvFile(ctx context.Context) error {
	workDir, err := os.Getwd()
	if err != nil {
		return errors.Wrapf(err, "getpwd")
	}

	envFile := path.Join(workDir, ".env")
	if _, err := os.Stat(envFile); err != nil {
		return nil
	}

	file, err := os.Open(envFile)
	if err != nil {
		return errors.Wrapf(err, "open %v", envFile)
	}
	defer file.Close()

	b, err := io.ReadAll(file)
	if err != nil {
		return errors.Wrapf(err, "read %v", envFile)
	}

	lines := strings.Split(strings.Replace(string(b), "\r\n", "\n", -1), "\n")
	logger.Df(ctx, "load env file %v, lines=%v", envFile, len(lines))

	for _, line := range lines {
		if strings.HasPrefix(strings.TrimSpace(line), "#") {
			continue
		}

		if pos := strings.IndexByte(line, '='); pos > 0 {
			key := strings.TrimSpace(line[:pos])
			value := strings.TrimSpace(line[pos+1:])
			if v := os.Getenv(key); v != "" {
				continue
			}

			os.Setenv(key, value)
		}
	}

	return nil
}

// buildDefaultEnvironmentVariables setups the default environment variables.
func buildDefaultEnvironmentVariables(ctx context.Context) {
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
	// The WebRTC media server, via UDP protocol.
	setEnvDefault("PROXY_WEBRTC_SERVER", "18000")
	// The SRT media server, via UDP protocol.
	setEnvDefault("PROXY_SRT_SERVER", "20080")
	// The API server of proxy itself.
	setEnvDefault("PROXY_SYSTEM_API", "12025")
	// The static directory for web server.
	setEnvDefault("PROXY_STATIC_FILES", "../trunk/research")

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
	// Default backend api port, for debugging.
	setEnvDefault("PROXY_DEFAULT_BACKEND_API", "1985")
	// Default backend udp rtc port, for debugging.
	setEnvDefault("PROXY_DEFAULT_BACKEND_RTC", "8000")
	// Default backend udp srt port, for debugging.
	setEnvDefault("PROXY_DEFAULT_BACKEND_SRT", "10080")

	logger.Df(ctx, "load .env as GO_PPROF=%v, "+
		"PROXY_FORCE_QUIT_TIMEOUT=%v, PROXY_GRACE_QUIT_TIMEOUT=%v, "+
		"PROXY_HTTP_API=%v, PROXY_HTTP_SERVER=%v, PROXY_RTMP_SERVER=%v, "+
		"PROXY_WEBRTC_SERVER=%v, PROXY_SRT_SERVER=%v, "+
		"PROXY_SYSTEM_API=%v, PROXY_STATIC_FILES=%v, PROXY_DEFAULT_BACKEND_ENABLED=%v, "+
		"PROXY_DEFAULT_BACKEND_IP=%v, PROXY_DEFAULT_BACKEND_RTMP=%v, "+
		"PROXY_DEFAULT_BACKEND_HTTP=%v, PROXY_DEFAULT_BACKEND_API=%v, "+
		"PROXY_DEFAULT_BACKEND_RTC=%v, PROXY_DEFAULT_BACKEND_SRT=%v, "+
		"PROXY_LOAD_BALANCER_TYPE=%v, PROXY_REDIS_HOST=%v, PROXY_REDIS_PORT=%v, "+
		"PROXY_REDIS_PASSWORD=%v, PROXY_REDIS_DB=%v",
		envGoPprof(),
		envForceQuitTimeout(), envGraceQuitTimeout(),
		envHttpAPI(), envHttpServer(), envRtmpServer(),
		envWebRTCServer(), envSRTServer(),
		envSystemAPI(), envStaticFiles(), envDefaultBackendEnabled(),
		envDefaultBackendIP(), envDefaultBackendRTMP(),
		envDefaultBackendHttp(), envDefaultBackendAPI(),
		envDefaultBackendRTC(), envDefaultBackendSRT(),
		envLoadBalancerType(), envRedisHost(), envRedisPort(),
		envRedisPassword(), envRedisDB(),
	)
}

func envStaticFiles() string {
	return os.Getenv("PROXY_STATIC_FILES")
}

func envDefaultBackendSRT() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_SRT")
}

func envDefaultBackendRTC() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_RTC")
}

func envDefaultBackendAPI() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_API")
}

func envSRTServer() string {
	return os.Getenv("PROXY_SRT_SERVER")
}

func envWebRTCServer() string {
	return os.Getenv("PROXY_WEBRTC_SERVER")
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
