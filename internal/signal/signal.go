// Copyright (c) 2026 Winlin
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

// Handler installs OS signal handlers and the force-quit timer. The notify
// and exit indirections are struct fields (not package globals) so concurrent
// tests can each construct a handler with their own fakes without racing on
// shared state.
type Handler struct {
	notify func(c chan<- os.Signal, sig ...os.Signal)
	exit   func(code int)
}

// NewHandler returns a Handler wired to the real OS implementations.
func NewHandler() *Handler {
	return &Handler{
		notify: signal.Notify,
		exit:   os.Exit,
	}
}

func (h *Handler) InstallSignals(ctx context.Context, cancel context.CancelFunc) {
	sc := make(chan os.Signal, 1)
	h.notify(sc, syscall.SIGINT, syscall.SIGTERM, os.Interrupt)

	go func() {
		for s := range sc {
			logger.Debug(ctx, "Got signal %v", s)
			cancel()
		}
	}()
}

func (h *Handler) InstallForceQuit(ctx context.Context, environment env.ProxyEnvironment) error {
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
		logger.Warn(ctx, "Force to exit by timeout")
		h.exit(1)
	}()
	return nil
}
