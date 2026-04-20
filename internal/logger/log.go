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
)

type logger interface {
	Printf(ctx context.Context, format string, v ...any)
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

func (v *loggerPlus) Printf(ctx context.Context, f string, a ...any) {
	attrs := []slog.Attr{slog.Int("pid", os.Getpid())}
	if cid := ContextID(ctx); cid != "" {
		attrs = append(attrs, slog.String("cid", cid))
	}
	v.logger.LogAttrs(ctx, v.level, fmt.Sprintf(f, a...), attrs...)
}

var verboseLogger logger

func Vf(ctx context.Context, format string, a ...any) {
	verboseLogger.Printf(ctx, format, a...)
}

var debugLogger logger

func Df(ctx context.Context, format string, a ...any) {
	debugLogger.Printf(ctx, format, a...)
}

var warnLogger logger

func Wf(ctx context.Context, format string, a ...any) {
	warnLogger.Printf(ctx, format, a...)
}

var errorLogger logger

func Ef(ctx context.Context, format string, a ...any) {
	errorLogger.Printf(ctx, format, a...)
}

const (
	levelVerb  slog.Level = slog.LevelDebug - 4
	levelDebug slog.Level = slog.LevelDebug
	levelWarn  slog.Level = slog.LevelWarn
	levelError slog.Level = slog.LevelError
)

// newJSONLogger builds a slog.Logger that writes JSON records to w, renders the
// time in UTC, and maps our custom levels to short lowercase labels.
func newJSONLogger(w io.Writer) *slog.Logger {
	h := slog.NewJSONHandler(w, &slog.HandlerOptions{
		Level: levelVerb,
		ReplaceAttr: func(groups []string, a slog.Attr) slog.Attr {
			if len(groups) != 0 {
				return a
			}
			switch a.Key {
			case slog.TimeKey:
				return slog.Time(slog.TimeKey, a.Value.Time().UTC())
			case slog.LevelKey:
				return slog.String(slog.LevelKey, levelLabel(a.Value.Any().(slog.Level)))
			}
			return a
		},
	})
	return slog.New(h)
}

func levelLabel(l slog.Level) string {
	switch l {
	case levelVerb:
		return "verb"
	case levelDebug:
		return "debug"
	case levelWarn:
		return "warn"
	case levelError:
		return "error"
	}
	return l.String()
}

func init() {
	verboseLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(io.Discard)
		l.level = levelVerb
	})
	debugLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(os.Stdout)
		l.level = levelDebug
	})
	warnLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(os.Stderr)
		l.level = levelWarn
	})
	errorLogger = newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(os.Stderr)
		l.level = levelError
	})
}
