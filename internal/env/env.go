// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package env

import (
	"bufio"
	"context"
	"os"
	"strings"

	"srsx/internal/errors"
	"srsx/internal/logger"
)

// Environment provides access to environment variables.
type Environment interface {
	// Go pprof profiling
	GoPprof() string
	// Graceful quit timeout
	GraceQuitTimeout() string
	// Force quit timeout
	ForceQuitTimeout() string
	// HTTP API server port
	HttpAPI() string
	// HTTP web server port
	HttpServer() string
	// RTMP media server port
	RtmpServer() string
	// WebRTC media server port (UDP)
	WebRTCServer() string
	// SRT media server port (UDP)
	SRTServer() string
	// System API server port
	SystemAPI() string
	// Static files directory
	StaticFiles() string
	// Load balancer type (memory or redis)
	LoadBalancerType() string
	// Redis host
	RedisHost() string
	// Redis port
	RedisPort() string
	// Redis password
	RedisPassword() string
	// Redis database
	RedisDB() string
	// Default backend enabled
	DefaultBackendEnabled() string
	// Default backend IP
	DefaultBackendIP() string
	// Default backend RTMP port
	DefaultBackendRTMP() string
	// Default backend HTTP port
	DefaultBackendHttp() string
	// Default backend API port
	DefaultBackendAPI() string
	// Default backend RTC port (UDP)
	DefaultBackendRTC() string
	// Default backend SRT port (UDP)
	DefaultBackendSRT() string
}

type environment struct{}

// NewEnvironment creates a new Environment instance, loading and building default environment variables.
func NewEnvironment(ctx context.Context) (Environment, error) {
	if err := loadEnvFile(ctx); err != nil {
		return nil, err
	}
	buildDefaultEnvironmentVariables(ctx)
	return &environment{}, nil
}

func (e *environment) GoPprof() string {
	return os.Getenv("GO_PPROF")
}

func (e *environment) GraceQuitTimeout() string {
	return os.Getenv("PROXY_GRACE_QUIT_TIMEOUT")
}

func (e *environment) ForceQuitTimeout() string {
	return os.Getenv("PROXY_FORCE_QUIT_TIMEOUT")
}

func (e *environment) HttpAPI() string {
	return os.Getenv("PROXY_HTTP_API")
}

func (e *environment) HttpServer() string {
	return os.Getenv("PROXY_HTTP_SERVER")
}

func (e *environment) RtmpServer() string {
	return os.Getenv("PROXY_RTMP_SERVER")
}

func (e *environment) WebRTCServer() string {
	return os.Getenv("PROXY_WEBRTC_SERVER")
}

func (e *environment) SRTServer() string {
	return os.Getenv("PROXY_SRT_SERVER")
}

func (e *environment) SystemAPI() string {
	return os.Getenv("PROXY_SYSTEM_API")
}

func (e *environment) StaticFiles() string {
	return os.Getenv("PROXY_STATIC_FILES")
}

func (e *environment) LoadBalancerType() string {
	return os.Getenv("PROXY_LOAD_BALANCER_TYPE")
}

func (e *environment) RedisHost() string {
	return os.Getenv("PROXY_REDIS_HOST")
}

func (e *environment) RedisPort() string {
	return os.Getenv("PROXY_REDIS_PORT")
}

func (e *environment) RedisPassword() string {
	return os.Getenv("PROXY_REDIS_PASSWORD")
}

func (e *environment) RedisDB() string {
	return os.Getenv("PROXY_REDIS_DB")
}

func (e *environment) DefaultBackendEnabled() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_ENABLED")
}

func (e *environment) DefaultBackendIP() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_IP")
}

func (e *environment) DefaultBackendRTMP() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_RTMP")
}

func (e *environment) DefaultBackendHttp() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_HTTP")
}

func (e *environment) DefaultBackendAPI() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_API")
}

func (e *environment) DefaultBackendRTC() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_RTC")
}

func (e *environment) DefaultBackendSRT() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_SRT")
}

// loadEnvFile loads the environment variables from .env file.
func loadEnvFile(ctx context.Context) error {
	envMap, err := parseEnvFile(".env")
	if err != nil {
		if os.IsNotExist(err) {
			logger.Df(ctx, "no .env file found, skipping")
			return nil
		}
		return errors.Wrapf(err, "load .env file")
	}

	// Build a set of existing environment variable keys, so we don't overwrite them.
	currentEnv := make(map[string]bool)
	for _, entry := range os.Environ() {
		key, _, _ := strings.Cut(entry, "=")
		currentEnv[key] = true
	}

	for key, value := range envMap {
		if !currentEnv[key] {
			os.Setenv(key, value)
		}
	}

	logger.Df(ctx, "successfully loaded .env file")
	return nil
}

// parseEnvFile reads a .env file and returns a map of key-value pairs.
func parseEnvFile(filename string) (map[string]string, error) {
	file, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	envMap := make(map[string]string)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		// Skip empty lines and comments.
		if line == "" || line[0] == '#' {
			continue
		}

		// Strip optional "export " prefix.
		if strings.HasPrefix(line, "export ") {
			line = strings.TrimPrefix(line, "export ")
			line = strings.TrimSpace(line)
		}

		// Split on first '=' to get key and value.
		key, value, found := strings.Cut(line, "=")
		if !found {
			continue
		}

		key = strings.TrimSpace(key)
		value = strings.TrimSpace(value)

		// Handle quoted values.
		if len(value) >= 2 {
			if value[0] == '\'' && value[len(value)-1] == '\'' {
				// Single-quoted: raw literal, no escaping.
				value = value[1 : len(value)-1]
			} else if value[0] == '"' && value[len(value)-1] == '"' {
				// Double-quoted: process escape sequences.
				value = value[1 : len(value)-1]
				value = strings.ReplaceAll(value, `\n`, "\n")
				value = strings.ReplaceAll(value, `\r`, "\r")
				value = strings.ReplaceAll(value, `\"`, `"`)
				value = strings.ReplaceAll(value, `\\`, `\`)
			} else {
				// Unquoted: strip inline comments.
				if idx := strings.Index(value, " #"); idx != -1 {
					value = strings.TrimSpace(value[:idx])
				}
			}
		} else {
			// Unquoted short value: strip inline comments.
			if idx := strings.Index(value, " #"); idx != -1 {
				value = strings.TrimSpace(value[:idx])
			}
		}

		envMap[key] = value
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return envMap, nil
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
	// The static directory for web server, optional.
	setEnvDefault("PROXY_STATIC_FILES", "./trunk/research")

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
		os.Getenv("GO_PPROF"),
		os.Getenv("PROXY_FORCE_QUIT_TIMEOUT"), os.Getenv("PROXY_GRACE_QUIT_TIMEOUT"),
		os.Getenv("PROXY_HTTP_API"), os.Getenv("PROXY_HTTP_SERVER"), os.Getenv("PROXY_RTMP_SERVER"),
		os.Getenv("PROXY_WEBRTC_SERVER"), os.Getenv("PROXY_SRT_SERVER"),
		os.Getenv("PROXY_SYSTEM_API"), os.Getenv("PROXY_STATIC_FILES"), os.Getenv("PROXY_DEFAULT_BACKEND_ENABLED"),
		os.Getenv("PROXY_DEFAULT_BACKEND_IP"), os.Getenv("PROXY_DEFAULT_BACKEND_RTMP"),
		os.Getenv("PROXY_DEFAULT_BACKEND_HTTP"), os.Getenv("PROXY_DEFAULT_BACKEND_API"),
		os.Getenv("PROXY_DEFAULT_BACKEND_RTC"), os.Getenv("PROXY_DEFAULT_BACKEND_SRT"),
		os.Getenv("PROXY_LOAD_BALANCER_TYPE"), os.Getenv("PROXY_REDIS_HOST"), os.Getenv("PROXY_REDIS_PORT"),
		os.Getenv("PROXY_REDIS_PASSWORD"), os.Getenv("PROXY_REDIS_DB"),
	)
}

// setEnvDefault set env key=value if not set.
func setEnvDefault(key, value string) {
	if os.Getenv(key) == "" {
		os.Setenv(key, value)
	}
}
