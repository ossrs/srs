// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package signal

import (
	"context"
	"os"
	"os/signal"
	"syscall"
	"time"

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/logger"
)

func InstallSignals(ctx context.Context, cancel context.CancelFunc) {
	sc := make(chan os.Signal, 1)
	signal.Notify(sc, syscall.SIGINT, syscall.SIGTERM, os.Interrupt)

	go func() {
		for s := range sc {
			logger.Df(ctx, "Got signal %v", s)
			cancel()
		}
	}()
}

func InstallForceQuit(ctx context.Context, environment env.Environment) error {
	var forceTimeout time.Duration
	timeoutStr := environment.ForceQuitTimeout()
	if t, err := time.ParseDuration(timeoutStr); err != nil {
		return errors.Wrapf(err, "parse force timeout %v", timeoutStr)
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
