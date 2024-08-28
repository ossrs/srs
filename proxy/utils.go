// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"os"
	"reflect"
	"time"

	"srs-proxy/errors"
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

func envRtmpServer() string {
	return os.Getenv("PROXY_RTMP_SERVER")
}

func envSystemAPI() string {
	return os.Getenv("PROXY_SYSTEM_API")
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

func envDefaultBackendEnabled() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_ENABLED")
}

func envDefaultBackendIP() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_IP")
}

func envDefaultBackendRTMP() string {
	return os.Getenv("PROXY_DEFAULT_BACKEND_RTMP")
}

func envLoadBalancerType() string {
	return os.Getenv("PROXY_LOAD_BALANCER_TYPE")
}

func apiResponse(ctx context.Context, w http.ResponseWriter, r *http.Request, data any) {
	w.Header().Set("Server", fmt.Sprintf("%v/%v", Signature(), Version()))

	b, err := json.Marshal(data)
	if err != nil {
		apiError(ctx, w, r, errors.Wrapf(err, "marshal %v %v", reflect.TypeOf(data), data))
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	w.Write(b)
}

func apiError(ctx context.Context, w http.ResponseWriter, r *http.Request, err error) {
	logger.Wf(ctx, "HTTP API error %+v", err)
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.WriteHeader(http.StatusInternalServerError)
	fmt.Fprintln(w, fmt.Sprintf("%v", err))
}

func parseGracefullyQuitTimeout() (time.Duration, error) {
	if t, err := time.ParseDuration(envGraceQuitTimeout()); err != nil {
		return 0, errors.Wrapf(err, "parse duration %v", envGraceQuitTimeout())
	} else {
		return t, nil
	}
}

// ParseBody read the body from r, and unmarshal JSON to v.
func ParseBody(r io.ReadCloser, v interface{}) error {
	b, err := ioutil.ReadAll(r)
	if err != nil {
		return errors.Wrapf(err, "read body")
	}
	defer r.Close()

	if len(b) == 0 {
		return nil
	}

	if err := json.Unmarshal(b, v); err != nil {
		return errors.Wrapf(err, "json unmarshal %v", string(b))
	}

	return nil
}
