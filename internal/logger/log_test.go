// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package logger

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"log/slog"
	"os"
	"testing"
	"time"
)

func decodeLine(t *testing.T, line []byte) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal(bytes.TrimSpace(line), &m); err != nil {
		t.Fatalf("decode %q: %v", line, err)
	}
	return m
}

func bufLoggerPlus(w io.Writer, level slog.Level) *loggerPlus {
	return newLoggerPlus(func(l *loggerPlus) {
		l.logger = newJSONLogger(w)
		l.level = level
	})
}

func TestLog_EmitsAllFields(t *testing.T) {
	var buf bytes.Buffer
	lp := bufLoggerPlus(&buf, slog.LevelDebug)
	ctx := withContextID(context.Background(), "abc1234")
	lp.Log(ctx, "hello %s %d", "world", 42)

	m := decodeLine(t, buf.Bytes())
	if m["level"] != "DEBUG" {
		t.Errorf("level = %v, want DEBUG", m["level"])
	}
	if m["msg"] != "hello world 42" {
		t.Errorf("msg = %v, want %q", m["msg"], "hello world 42")
	}
	if m["cid"] != "abc1234" {
		t.Errorf("cid = %v, want abc1234", m["cid"])
	}
	pid, ok := m["pid"].(float64)
	if !ok || int(pid) != os.Getpid() {
		t.Errorf("pid = %v, want %d", m["pid"], os.Getpid())
	}
	ts, ok := m["time"].(string)
	if !ok {
		t.Fatalf("time = %v, want string", m["time"])
	}
	if _, err := time.Parse(time.RFC3339Nano, ts); err != nil {
		t.Errorf("time %q not RFC3339Nano: %v", ts, err)
	}
}

func TestLog_OmitsCIDWhenAbsent(t *testing.T) {
	var buf bytes.Buffer
	bufLoggerPlus(&buf, slog.LevelWarn).Log(context.Background(), "no cid here")

	m := decodeLine(t, buf.Bytes())
	if v, present := m["cid"]; present {
		t.Errorf("cid should be absent, got %v", v)
	}
	if m["level"] != "WARN" {
		t.Errorf("level = %v, want WARN", m["level"])
	}
}

func TestLog_EmitsStructuredArgs(t *testing.T) {
	var buf bytes.Buffer
	bufLoggerPlus(&buf, slog.LevelInfo).Log(context.Background(), "hello", "stream", "live/livestream", "retry", 2)

	m := decodeLine(t, buf.Bytes())
	if m["msg"] != "hello" {
		t.Errorf("msg = %v, want hello", m["msg"])
	}
	if m["stream"] != "live/livestream" {
		t.Errorf("stream = %v, want live/livestream", m["stream"])
	}
	if retry, ok := m["retry"].(float64); !ok || retry != 2 {
		t.Errorf("retry = %v, want 2", m["retry"])
	}
}

func TestLog_AllLevelsMapToLabel(t *testing.T) {
	cases := []struct {
		level slog.Level
		label string
	}{
		{slog.LevelInfo, "INFO"},
		{slog.LevelDebug, "DEBUG"},
		{slog.LevelWarn, "WARN"},
		{slog.LevelError, "ERROR"},
	}
	for _, tc := range cases {
		var buf bytes.Buffer
		bufLoggerPlus(&buf, tc.level).Log(context.Background(), "hi")
		m := decodeLine(t, buf.Bytes())
		if m["level"] != tc.label {
			t.Errorf("level(%v) rendered as %v, want %q", tc.level, m["level"], tc.label)
		}
	}
}

func TestNewJSONLogger_GroupedAttrsPassThrough(t *testing.T) {
	var buf bytes.Buffer
	lg := newJSONLogger(&buf)
	lg.LogAttrs(context.Background(), slog.LevelDebug, "grouped",
		slog.Group("meta", slog.String("inner", "v")))

	m := decodeLine(t, buf.Bytes())
	meta, ok := m["meta"].(map[string]any)
	if !ok {
		t.Fatalf("meta not an object: %v", m["meta"])
	}
	if meta["inner"] != "v" {
		t.Errorf("meta.inner = %v, want v", meta["inner"])
	}
}

func TestPackageWrappers_RouteToRightLogger(t *testing.T) {
	origI, origD, origW, origE := infoLogger, debugLogger, warnLogger, errorLogger
	t.Cleanup(func() {
		infoLogger, debugLogger, warnLogger, errorLogger = origI, origD, origW, origE
	})

	iBuf, dBuf, wBuf, eBuf := &bytes.Buffer{}, &bytes.Buffer{}, &bytes.Buffer{}, &bytes.Buffer{}
	infoLogger = bufLoggerPlus(iBuf, slog.LevelInfo)
	debugLogger = bufLoggerPlus(dBuf, slog.LevelDebug)
	warnLogger = bufLoggerPlus(wBuf, slog.LevelWarn)
	errorLogger = bufLoggerPlus(eBuf, slog.LevelError)

	ctx := context.Background()
	Info(ctx, "v=%d", 1)
	Debug(ctx, "d=%d", 2)
	Warn(ctx, "w=%d", 3)
	Error(ctx, "e=%d", 4)

	checks := []struct {
		name  string
		buf   *bytes.Buffer
		label string
		msg   string
	}{
		{"Info", iBuf, "INFO", "v=1"},
		{"Debug", dBuf, "DEBUG", "d=2"},
		{"Warn", wBuf, "WARN", "w=3"},
		{"Error", eBuf, "ERROR", "e=4"},
	}
	for _, c := range checks {
		m := decodeLine(t, c.buf.Bytes())
		if m["level"] != c.label {
			t.Errorf("%s level = %v, want %v", c.name, m["level"], c.label)
		}
		if m["msg"] != c.msg {
			t.Errorf("%s msg = %v, want %v", c.name, m["msg"], c.msg)
		}
	}
}
