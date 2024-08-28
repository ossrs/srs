// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"os"
	"srs-proxy/errors"
	"srs-proxy/logger"
)

func main() {
	ctx := logger.WithContext(context.Background())
	logger.Df(ctx, "%v/%v started", Signature(), Version())

	// Install signals.
	ctx, cancel := context.WithCancel(ctx)
	installSignals(ctx, cancel)

	// Start the main loop, ignore the user cancel error.
	err := doMain(ctx)
	if err != nil && ctx.Err() != context.Canceled {
		logger.Ef(ctx, "main: %+v", err)
		os.Exit(-1)
	}

	logger.Df(ctx, "%v done", Signature())
}

func doMain(ctx context.Context) error {
	// Setup the environment variables.
	if err := loadEnvFile(ctx); err != nil {
		return errors.Wrapf(err, "load env")
	}

	setupDefaultEnv(ctx)

	// When cancelled, the program is forced to exit due to a timeout. Normally, this doesn't occur
	// because the main thread exits after the context is cancelled. However, sometimes the main thread
	// may be blocked for some reason, so a forced exit is necessary to ensure the program terminates.
	if err := installForceQuit(ctx); err != nil {
		return errors.Wrapf(err, "install force quit")
	}

	// Start the Go pprof if enabled.
	handleGoPprof(ctx)

	// Parse the gracefully quit timeout.
	gracefulQuitTimeout, err := parseGracefullyQuitTimeout()
	if err != nil {
		return errors.Wrapf(err, "parse gracefully quit timeout")
	}

	// Start the RTMP server.
	rtmpServer := NewRtmpServer()
	defer rtmpServer.Close()
	if err := rtmpServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "rtmp server")
	}

	// Start the HTTP web server.
	httpServer := NewHttpServer(func(server *httpServer) {
		server.gracefulQuitTimeout = gracefulQuitTimeout
	})
	defer httpServer.Close()
	if err := httpServer.ListenAndServe(ctx); err != nil {
		return errors.Wrapf(err, "http server")
	}

	return nil
}
