// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"os"
	"os/signal"
	"syscall"
	"time"

	"srs-proxy/errors"
	"srs-proxy/logger"
)

func installSignals(ctx context.Context, cancel context.CancelFunc) {
	sc := make(chan os.Signal, 1)
	signal.Notify(sc, syscall.SIGINT, syscall.SIGTERM, os.Interrupt)

	go func() {
		for s := range sc {
			logger.Df(ctx, "Got signal %v", s)
			cancel()
		}
	}()
}

func installForceQuit(ctx context.Context) error {
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
	return nil
}
