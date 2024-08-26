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
	"srs-proxy/errors"
	"srs-proxy/logger"
	"syscall"
	"time"

	"github.com/joho/godotenv"
)

func main() {
	ctx := logger.WithContext(context.Background())
	logger.Df(ctx, "SRS %v/%v started", Signature(), Version())

	if err := doMain(ctx); err != nil {
		logger.Ef(ctx, "main: %v", err)
		os.Exit(-1)
	}
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

	// When cancelled, the program is forced to exit due to a timeout. Normally, this doesn't occur
	// because the main thread exits after the context is cancelled. However, sometimes the main thread
	// may be blocked for some reason, so a forced exit is necessary to ensure the program terminates.
	go func() {
		<-ctx.Done()
		time.Sleep(30 * time.Second)
		logger.Wf(ctx, "Force to exit by timeout")
		os.Exit(1)
	}()

	// Whether enable the Go pprof.
	setEnvDefault("GO_PPROF", "")

	// The HTTP API server.
	setEnvDefault("PROXY_HTTP_API", "1985")

	logger.Df(ctx, "load .env as GO_PPROF=%v, PROXY_HTTP_API=%v", envGoPprof(), envHttpAPI())

	// Start the Go pprof if enabled.
	if addr := envGoPprof(); addr != "" {
		go func() {
			logger.Df(ctx, "Start Go pprof at %v", addr)
			http.ListenAndServe(addr, nil)
		}()
	}

	return nil
}
