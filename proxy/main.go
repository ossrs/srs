// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"net/http"
	"os"
	"os/signal"
	"path"
	"syscall"
	"time"

	"srs-proxy/errors"
	"srs-proxy/logger"

	"github.com/joho/godotenv"
)

func main() {
	ctx := logger.WithContext(context.Background())
	logger.Df(ctx, "SRS %v/%v started", Signature(), Version())

	// Install signals.
	sc := make(chan os.Signal, 1)
	signal.Notify(sc, syscall.SIGINT, syscall.SIGTERM, os.Interrupt)
	ctx, cancel := context.WithCancel(ctx)
	go func() {
		for s := range sc {
			logger.Df(ctx, "Got signal %v", s)
			cancel()
		}
	}()

	// Start the main loop, ignore the user cancel error.
	err := doMain(ctx)
	if err != nil && ctx.Err() == context.Canceled {
		logger.Ef(ctx, "main: %v", err)
		os.Exit(-1)
	}

	logger.Df(ctx, "Server %v done", Signature())
}

func doMain(ctx context.Context) error {
	// Load the environment variables from file. Note that we only use .env file.
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

	// Whether enable the Go pprof.
	setEnvDefault("GO_PPROF", "")
	// Force shutdown timeout.
	setEnvDefault("PROXY_FORCE_QUIT_TIMEOUT", "30s")
	// Graceful quit timeout.
	setEnvDefault("PROXY_GRACE_QUIT_TIMEOUT", "20s")

	// The HTTP API server.
	setEnvDefault("PROXY_HTTP_API", "1985")
	// The HTTP web server.
	setEnvDefault("PROXY_HTTP_SERVER", "8080")

	logger.Df(ctx, "load .env as GO_PPROF=%v, "+
		"PROXY_FORCE_QUIT_TIMEOUT=%v, PROXY_GRACE_QUIT_TIMEOUT=%v, "+
		"PROXY_HTTP_API=%v, PROXY_HTTP_SERVER=%v",
		envGoPprof(),
		envForceQuitTimeout(), envGraceQuitTimeout(),
		envHttpAPI(), envHttpServer(),
	)

	// When cancelled, the program is forced to exit due to a timeout. Normally, this doesn't occur
	// because the main thread exits after the context is cancelled. However, sometimes the main thread
	// may be blocked for some reason, so a forced exit is necessary to ensure the program terminates.
	var forceTimeout time.Duration
	if t, err := time.ParseDuration(envForceQuitTimeout()); err != nil {
		return errors.Wrapf(err, "parse force timeout %v", envForceQuitTimeout())
	} else {
		forceTimeout = t
	}

	go func() {
		<-ctx.Done()
		time.Sleep(forceTimeout)
		logger.Wf(ctx, "Force to exit by timeout")
		os.Exit(1)
	}()

	// Start the Go pprof if enabled.
	if addr := envGoPprof(); addr != "" {
		go func() {
			logger.Df(ctx, "Start Go pprof at %v", addr)
			http.ListenAndServe(addr, nil)
		}()
	}

	// Start the HTTP web server.
	httpServer := NewHttpServer()
	defer httpServer.Close()
	if err := httpServer.ListenAndServe(ctx); err != nil {
		return errors.Wrapf(err, "http server")
	}

	return nil
}
