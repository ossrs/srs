// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"encoding/json"
	stdErr "errors"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"os"
	"reflect"
	"strings"
	"syscall"
	"time"

	"srs-proxy/errors"
	"srs-proxy/logger"
)

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

// buildStreamURL build as vhost/app/stream for stream URL r.
func buildStreamURL(r string) (string, error) {
	u, err := url.Parse(r)
	if err != nil {
		return "", errors.Wrapf(err, "parse url %v", r)
	}

	// If not domain or ip in hostname, it's __defaultVhost__.
	defaultVhost := !strings.Contains(u.Hostname(), ".")

	// If hostname is actually an IP address, it's __defaultVhost__.
	if ip := net.ParseIP(u.Hostname()); ip.To4() != nil {
		defaultVhost = true
	}

	if defaultVhost {
		return fmt.Sprintf("__defaultVhost__%v", u.Path), nil
	}

	// Ignore port, only use hostname as vhost.
	return fmt.Sprintf("%v%v", u.Hostname(), u.Path), nil
}

// isPeerClosedError indicates whether peer object closed the connection.
func isPeerClosedError(err error) bool {
	causeErr := errors.Cause(err)
	if stdErr.Is(causeErr, io.EOF) ||
		stdErr.Is(causeErr, net.ErrClosed) ||
		stdErr.Is(causeErr, syscall.EPIPE) {
		return true
	}

	if netErr, ok := causeErr.(*net.OpError); ok {
		if sysErr, ok := netErr.Err.(*os.SyscallError); ok {
			if stdErr.Is(sysErr.Err, syscall.ECONNRESET) {
				return true
			}
		}
	}

	return false
}
