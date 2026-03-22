// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package logger

import (
	"context"
	"io/ioutil"
	stdLog "log"
	"os"
)

type logger interface {
	Printf(ctx context.Context, format string, v ...any)
}

type loggerPlus struct {
	logger *stdLog.Logger
	level  string
}

func newLoggerPlus(opts ...func(*loggerPlus)) *loggerPlus {
	v := &loggerPlus{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *loggerPlus) Printf(ctx context.Context, f string, a ...interface{}) {
	format, args := f, a
	if cid := ContextID(ctx); cid != "" {
		format, args = "[%v][%v][%v] "+format, append([]interface{}{v.level, os.Getpid(), cid}, a...)
	}

	v.logger.Printf(format, args...)
}

var verboseLogger logger

func Vf(ctx context.Context, format string, a ...interface{}) {
	verboseLogger.Printf(ctx, format, a...)
}

var debugLogger logger

func Df(ctx context.Context, format string, a ...interface{}) {
	debugLogger.Printf(ctx, format, a...)
}

var warnLogger logger

func Wf(ctx context.Context, format string, a ...interface{}) {
	warnLogger.Printf(ctx, format, a...)
}

var errorLogger logger

func Ef(ctx context.Context, format string, a ...interface{}) {
	errorLogger.Printf(ctx, format, a...)
}

const (
	logVerboseLabel = "verb"
	logDebugLabel   = "debug"
	logWarnLabel    = "warn"
	logErrorLabel   = "error"
)

func init() {
	verboseLogger = newLoggerPlus(func(logger *loggerPlus) {
		logger.logger = stdLog.New(ioutil.Discard, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds)
		logger.level = logVerboseLabel
	})
	debugLogger = newLoggerPlus(func(logger *loggerPlus) {
		logger.logger = stdLog.New(os.Stdout, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds)
		logger.level = logDebugLabel
	})
	warnLogger = newLoggerPlus(func(logger *loggerPlus) {
		logger.logger = stdLog.New(os.Stderr, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds)
		logger.level = logWarnLabel
	})
	errorLogger = newLoggerPlus(func(logger *loggerPlus) {
		logger.logger = stdLog.New(os.Stderr, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds)
		logger.level = logErrorLabel
	})
}
