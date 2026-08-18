// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package logger

import (
	"context"
	"fmt"
	"io"
	"log/slog"
	"os"
	"strings"

	"srsx/internal/version"
)

type logger interface {
	Log(ctx context.Context, msg string, args ...any)
}

type loggerPlus struct {
	logger *slog.Logger
	level  slog.Level
}

func newLoggerPlus(opts ...func(*loggerPlus)) *loggerPlus {
	v := &loggerPlus{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *loggerPlus) Log(ctx context.Context, msg string, args ...any) {
	attrs := []any{
		"pid", os.Getpid(),
		"version", version.Version(),
	}

	if cid := ContextID(ctx); cid != "" {
		attrs = append(attrs, "cid", cid)
	}

	// Keep compatibility with the old *f call sites while exposing the new
	// slog-style API. New code should pass structured key/value args.
	if len(args) > 0 && strings.Contains(msg, "%") {
		msg = fmt.Sprintf(msg, args...)
		args = nil
	}
	attrs = append(attrs, args...)
	v.logger.Log(ctx, v.level, msg, attrs...)
}

var debugLogger logger

func Debug(ctx context.Context, msg string, args ...any) {
	debugLogger.Log(ctx, msg, args...)
}

var infoLogger logger

func Info(ctx context.Context, msg string, args ...any) {
	infoLogger.Log(ctx, msg, args...)
}

var warnLogger logger

func Warn(ctx context.Context, msg string, args ...any) {
	warnLogger.Log(ctx, msg, args...)
}

var errorLogger logger

func Error(ctx context.Context, msg string, args ...any) {
	errorLogger.Log(ctx, msg, args...)
}

// newJSONLogger builds a slog.Logger that writes JSON records to w.
func newJSONLogger(w io.Writer) *slog.Logger {
	h := slog.NewJSONHandler(w, &slog.HandlerOptions{
		Level: slog.LevelDebug,
	})
	return slog.New(h)
}

func init() {
	debugLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(os.Stdout)
		l.level = slog.LevelDebug
	})
	infoLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(os.Stdout)
		l.level = slog.LevelInfo
	})
	warnLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(os.Stderr)
		l.level = slog.LevelWarn
	})
	errorLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(os.Stderr)
		l.level = slog.LevelError
	})
}
