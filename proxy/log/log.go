// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package log

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

func newLoggerPlus(l *stdLog.Logger, level string) *loggerPlus {
	return &loggerPlus{logger: l, level: level}
}

func (v *loggerPlus) Printf(ctx context.Context, f string, a ...interface{}) {
	format, args := f, a
	if cid, ok := ctx.Value(cidKey).(string); ok {
		format, args = "[%v][%v][%v] "+format, append([]interface{}{v.level, os.Getpid(), cid}, a...)
	}

	v.logger.Printf(format, args...)
}

var verboseLogger logger

func Vf(ctx context.Context, format string, a ...interface{}) {
	verboseLogger.Printf(ctx, format, a...)
}

var infoLogger logger

func If(ctx context.Context, format string, a ...interface{}) {
	infoLogger.Printf(ctx, format, a...)
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
	logInfoLabel    = "info"
	logWarnLabel    = "warn"
	logErrorLabel   = "error"
)

func init() {
	verboseLogger = newLoggerPlus(stdLog.New(ioutil.Discard, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds), logVerboseLabel)
	infoLogger = newLoggerPlus(stdLog.New(os.Stdout, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds), logInfoLabel)
	warnLogger = newLoggerPlus(stdLog.New(os.Stderr, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds), logWarnLabel)
	errorLogger = newLoggerPlus(stdLog.New(os.Stderr, "", stdLog.Ldate|stdLog.Ltime|stdLog.Lmicroseconds), logErrorLabel)
}
