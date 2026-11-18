// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package env

import (
	"bufio"
	"context"
	"io"
	"os"
	"strings"

	"srsx/internal/errors"
	"srsx/internal/logger"
)

// Tests swap these so they do not touch the real process env or the filesystem.
var (
	getEnv    = os.Getenv
	setEnv    = os.Setenv
	lookupEnv = os.LookupEnv
	openFile  = func(name string) (io.ReadCloser, error) {
		return os.Open(name)
	}
)

type ProxyEnvironment interface {
	GoPprof() string
	GraceQuitTimeout() string
	ForceQuitTimeout() string
	HttpAPI() string
	HttpServer() string
	RtmpServer() string
	// UDP port.
	WebRTCServer() string
	// UDP port.
	SRTServer() string
	// The API server of the proxy itself.
	SystemAPI() string
	// Authentication of the proxy registration API.
	HttpAPIAuthEnabled() string
	HttpAPIAuthType() string
	HttpAPIAuthToken() string
	StaticFiles() string
	// memory or redis.
	LoadBalancerType() string
	RedisHost() string
	RedisPort() string
	RedisPassword() string
	RedisDB() string
	RedisKeyPrefix() string
	// Lifetime of an origin server registration.
	OriginServerTTL() string
	// The default backend server is for debugging.
	DefaultBackendEnabled() string
	DefaultBackendIP() string
	DefaultBackendRTMP() string
	DefaultBackendHttp() string
	DefaultBackendAPI() string
	// UDP port.
	DefaultBackendRTC() string
	// UDP port.
	DefaultBackendSRT() string
}

type proxyEnvironment struct{}

func NewProxyEnvironment(ctx context.Context) (ProxyEnvironment, error) {
	if err := loadEnvFile(ctx); err != nil {
		return nil, err
	}
	buildDefaultEnvironmentVariables(ctx)
	environment := &proxyEnvironment{}
	if err := environment.validate(); err != nil {
		return nil, err
	}
	return environment, nil
}

func (e *proxyEnvironment) GoPprof() string {
	return getEnv("GO_PPROF")
}

func (e *proxyEnvironment) GraceQuitTimeout() string {
	return getEnv("PROXY_GRACE_QUIT_TIMEOUT")
}

func (e *proxyEnvironment) ForceQuitTimeout() string {
	return getEnv("PROXY_FORCE_QUIT_TIMEOUT")
}

func (e *proxyEnvironment) HttpAPI() string {
	return getEnv("PROXY_HTTP_API")
}

func (e *proxyEnvironment) HttpServer() string {
	return getEnv("PROXY_HTTP_SERVER")
}

func (e *proxyEnvironment) RtmpServer() string {
	return getEnv("PROXY_RTMP_SERVER")
}

func (e *proxyEnvironment) WebRTCServer() string {
	return getEnv("PROXY_WEBRTC_SERVER")
}

func (e *proxyEnvironment) SRTServer() string {
	return getEnv("PROXY_SRT_SERVER")
}

func (e *proxyEnvironment) SystemAPI() string {
	return getEnv("PROXY_SYSTEM_API")
}

func (e *proxyEnvironment) HttpAPIAuthEnabled() string {
	return getEnv("PROXY_HTTP_API_AUTH_ENABLED")
}

func (e *proxyEnvironment) HttpAPIAuthType() string {
	return getEnv("PROXY_HTTP_API_AUTH_TYPE")
}

func (e *proxyEnvironment) HttpAPIAuthToken() string {
	return getEnv("PROXY_HTTP_API_AUTH_TOKEN")
}

func (e *proxyEnvironment) StaticFiles() string {
	return getEnv("PROXY_STATIC_FILES")
}

func (e *proxyEnvironment) LoadBalancerType() string {
	return getEnv("PROXY_LOAD_BALANCER_TYPE")
}

func (e *proxyEnvironment) RedisHost() string {
	return getEnv("PROXY_REDIS_HOST")
}

func (e *proxyEnvironment) RedisPort() string {
	return getEnv("PROXY_REDIS_PORT")
}

func (e *proxyEnvironment) RedisPassword() string {
	return getEnv("PROXY_REDIS_PASSWORD")
}

func (e *proxyEnvironment) RedisDB() string {
	return getEnv("PROXY_REDIS_DB")
}

func (e *proxyEnvironment) RedisKeyPrefix() string {
	return getEnv("PROXY_REDIS_KEY_PREFIX")
}

func (e *proxyEnvironment) OriginServerTTL() string {
	return getEnv("PROXY_ORIGIN_SERVER_TTL")
}

func (e *proxyEnvironment) DefaultBackendEnabled() string {
	return getEnv("PROXY_DEFAULT_BACKEND_ENABLED")
}

func (e *proxyEnvironment) DefaultBackendIP() string {
	return getEnv("PROXY_DEFAULT_BACKEND_IP")
}

func (e *proxyEnvironment) DefaultBackendRTMP() string {
	return getEnv("PROXY_DEFAULT_BACKEND_RTMP")
}

func (e *proxyEnvironment) DefaultBackendHttp() string {
	return getEnv("PROXY_DEFAULT_BACKEND_HTTP")
}

func (e *proxyEnvironment) DefaultBackendAPI() string {
	return getEnv("PROXY_DEFAULT_BACKEND_API")
}

func (e *proxyEnvironment) DefaultBackendRTC() string {
	return getEnv("PROXY_DEFAULT_BACKEND_RTC")
}

func (e *proxyEnvironment) DefaultBackendSRT() string {
	return getEnv("PROXY_DEFAULT_BACKEND_SRT")
}

func loadEnvFile(ctx context.Context) error {
	envMap, err := parseEnvFile(".env")
	if err != nil {
		if os.IsNotExist(err) {
			logger.Debug(ctx, "no .env file found, skipping")
			return nil
		}
		return errors.Wrapf(err, "load .env file")
	}

	for key, value := range envMap {
		if _, ok := lookupEnv(key); !ok {
			setEnv(key, value)
		}
	}

	logger.Debug(ctx, "successfully loaded .env file")
	return nil
}

func parseEnvFile(filename string) (map[string]string, error) {
	file, err := openFile(filename)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	return parseEnvReader(file)
}

func parseEnvReader(r io.Reader) (map[string]string, error) {
	envMap := make(map[string]string)
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		if line == "" || line[0] == '#' {
			continue
		}

		if strings.HasPrefix(line, "export ") {
			line = strings.TrimPrefix(line, "export ")
			line = strings.TrimSpace(line)
		}

		key, value, found := strings.Cut(line, "=")
		if !found {
			continue
		}

		key = strings.TrimSpace(key)
		value = strings.TrimSpace(value)

		if len(value) >= 2 {
			if value[0] == '\'' && value[len(value)-1] == '\'' {
				value = value[1 : len(value)-1]
			} else if value[0] == '"' && value[len(value)-1] == '"' {
				value = value[1 : len(value)-1]
				value = strings.ReplaceAll(value, `\n`, "\n")
				value = strings.ReplaceAll(value, `\r`, "\r")
				value = strings.ReplaceAll(value, `\"`, `"`)
				value = strings.ReplaceAll(value, `\\`, `\`)
			} else {
				if idx := strings.Index(value, " #"); idx != -1 {
					value = strings.TrimSpace(value[:idx])
				}
			}
		} else {
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

func buildDefaultEnvironmentVariables(ctx context.Context) {
	setEnvDefault("GO_PPROF", "")
	setEnvDefault("PROXY_FORCE_QUIT_TIMEOUT", "30s")
	setEnvDefault("PROXY_GRACE_QUIT_TIMEOUT", "20s")

	setEnvDefault("PROXY_HTTP_API", "11985")
	setEnvDefault("PROXY_HTTP_SERVER", "18080")
	setEnvDefault("PROXY_RTMP_SERVER", "11935")
	setEnvDefault("PROXY_WEBRTC_SERVER", "18000")
	setEnvDefault("PROXY_SRT_SERVER", "20080")
	setEnvDefault("PROXY_SYSTEM_API", "12025")
	setEnvDefault("PROXY_HTTP_API_AUTH_ENABLED", "off")
	setEnvDefault("PROXY_HTTP_API_AUTH_TYPE", "")
	setEnvDefault("PROXY_HTTP_API_AUTH_TOKEN", "")
	setEnvDefault("PROXY_STATIC_FILES", "./trunk/research")

	setEnvDefault("PROXY_LOAD_BALANCER_TYPE", "memory")
	setEnvDefault("PROXY_REDIS_HOST", "127.0.0.1")
	setEnvDefault("PROXY_REDIS_PORT", "6379")
	setEnvDefault("PROXY_REDIS_PASSWORD", "")
	setEnvDefault("PROXY_REDIS_DB", "0")
	setEnvDefault("PROXY_REDIS_KEY_PREFIX", "")
	setEnvDefault("PROXY_ORIGIN_SERVER_TTL", "300s")

	setEnvDefault("PROXY_DEFAULT_BACKEND_ENABLED", "off")
	setEnvDefault("PROXY_DEFAULT_BACKEND_IP", "127.0.0.1")
	setEnvDefault("PROXY_DEFAULT_BACKEND_RTMP", "1935")
	setEnvDefault("PROXY_DEFAULT_BACKEND_API", "1985")
	setEnvDefault("PROXY_DEFAULT_BACKEND_RTC", "8000")
	setEnvDefault("PROXY_DEFAULT_BACKEND_SRT", "10080")

	logger.Debug(ctx, "load .env as GO_PPROF=%v, "+
		"PROXY_FORCE_QUIT_TIMEOUT=%v, PROXY_GRACE_QUIT_TIMEOUT=%v, "+
		"PROXY_HTTP_API=%v, PROXY_HTTP_SERVER=%v, PROXY_RTMP_SERVER=%v, "+
		"PROXY_WEBRTC_SERVER=%v, PROXY_SRT_SERVER=%v, "+
		"PROXY_SYSTEM_API=%v, PROXY_STATIC_FILES=%v, PROXY_DEFAULT_BACKEND_ENABLED=%v, "+
		"PROXY_DEFAULT_BACKEND_IP=%v, PROXY_DEFAULT_BACKEND_RTMP=%v, "+
		"PROXY_DEFAULT_BACKEND_HTTP=%v, PROXY_DEFAULT_BACKEND_API=%v, "+
		"PROXY_DEFAULT_BACKEND_RTC=%v, PROXY_DEFAULT_BACKEND_SRT=%v, "+
		"PROXY_LOAD_BALANCER_TYPE=%v, PROXY_REDIS_HOST=%v, PROXY_REDIS_PORT=%v, "+
		"PROXY_REDIS_PASSWORD=%v, PROXY_REDIS_DB=%v, PROXY_REDIS_KEY_PREFIX=%v, "+
		"PROXY_ORIGIN_SERVER_TTL=%v",
		getEnv("GO_PPROF"),
		getEnv("PROXY_FORCE_QUIT_TIMEOUT"), getEnv("PROXY_GRACE_QUIT_TIMEOUT"),
		getEnv("PROXY_HTTP_API"), getEnv("PROXY_HTTP_SERVER"), getEnv("PROXY_RTMP_SERVER"),
		getEnv("PROXY_WEBRTC_SERVER"), getEnv("PROXY_SRT_SERVER"),
		getEnv("PROXY_SYSTEM_API"), getEnv("PROXY_STATIC_FILES"), getEnv("PROXY_DEFAULT_BACKEND_ENABLED"),
		getEnv("PROXY_DEFAULT_BACKEND_IP"), getEnv("PROXY_DEFAULT_BACKEND_RTMP"),
		getEnv("PROXY_DEFAULT_BACKEND_HTTP"), getEnv("PROXY_DEFAULT_BACKEND_API"),
		getEnv("PROXY_DEFAULT_BACKEND_RTC"), getEnv("PROXY_DEFAULT_BACKEND_SRT"),
		getEnv("PROXY_LOAD_BALANCER_TYPE"), getEnv("PROXY_REDIS_HOST"), getEnv("PROXY_REDIS_PORT"),
		getEnv("PROXY_REDIS_PASSWORD"), getEnv("PROXY_REDIS_DB"), getEnv("PROXY_REDIS_KEY_PREFIX"),
		getEnv("PROXY_ORIGIN_SERVER_TTL"),
	)
}

func (e *proxyEnvironment) validate() error {
	if e.HttpAPIAuthEnabled() != "on" {
		return nil
	}

	if e.HttpAPIAuthType() == "" {
		return errors.Errorf("PROXY_HTTP_API_AUTH_TYPE is required when HTTP API authentication is enabled")
	}
	if e.HttpAPIAuthType() != "bearer" {
		return errors.Errorf("invalid PROXY_HTTP_API_AUTH_TYPE=%v, proxy only supports bearer", e.HttpAPIAuthType())
	}
	if e.HttpAPIAuthToken() == "" {
		return errors.Errorf("PROXY_HTTP_API_AUTH_TOKEN is required for bearer authentication")
	}

	return nil
}

func setEnvDefault(key, value string) {
	if getEnv(key) == "" {
		setEnv(key, value)
	}
}
