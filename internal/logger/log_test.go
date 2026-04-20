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
	"strings"
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

func TestLevelLabel_Known(t *testing.T) {
	cases := map[slog.Level]string{
		levelVerb:  "verb",
		levelDebug: "debug",
		levelWarn:  "warn",
		levelError: "error",
	}
	for lvl, want := range cases {
		if got := levelLabel(lvl); got != want {
			t.Errorf("levelLabel(%v) = %q, want %q", lvl, got, want)
		}
	}
}

func TestLevelLabel_UnknownFallsBackToString(t *testing.T) {
	got := levelLabel(slog.Level(99))
	if got == "" {
		t.Fatalf("levelLabel(99) returned empty")
	}
	if got == "verb" || got == "debug" || got == "warn" || got == "error" {
		t.Fatalf("levelLabel(99) = %q, want slog.Level.String() form", got)
	}
}

func TestPrintf_EmitsAllFields(t *testing.T) {
	var buf bytes.Buffer
	lp := bufLoggerPlus(&buf, levelDebug)
	ctx := withContextID(context.Background(), "abc1234")
	lp.Printf(ctx, "hello %s %d", "world", 42)

	m := decodeLine(t, buf.Bytes())
	if m["level"] != "debug" {
		t.Errorf("level = %v, want debug", m["level"])
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
	if !ok || !strings.HasSuffix(ts, "Z") {
		t.Errorf("time = %v, want UTC suffix Z", m["time"])
	}
	if _, err := time.Parse(time.RFC3339Nano, ts); err != nil {
		t.Errorf("time %q not RFC3339Nano: %v", ts, err)
	}
}

func TestPrintf_OmitsCIDWhenAbsent(t *testing.T) {
	var buf bytes.Buffer
	bufLoggerPlus(&buf, levelWarn).Printf(context.Background(), "no cid here")

	m := decodeLine(t, buf.Bytes())
	if v, present := m["cid"]; present {
		t.Errorf("cid should be absent, got %v", v)
	}
	if m["level"] != "warn" {
		t.Errorf("level = %v, want warn", m["level"])
	}
}

func TestPrintf_AllLevelsMapToLabel(t *testing.T) {
	cases := []struct {
		level slog.Level
		label string
	}{
		{levelVerb, "verb"},
		{levelDebug, "debug"},
		{levelWarn, "warn"},
		{levelError, "error"},
	}
	for _, tc := range cases {
		var buf bytes.Buffer
		bufLoggerPlus(&buf, tc.level).Printf(context.Background(), "hi")
		m := decodeLine(t, buf.Bytes())
		if m["level"] != tc.label {
			t.Errorf("level(%v) rendered as %v, want %q", tc.level, m["level"], tc.label)
		}
	}
}

func TestNewJSONLogger_GroupedAttrsPassThrough(t *testing.T) {
	var buf bytes.Buffer
	lg := newJSONLogger(&buf)
	lg.LogAttrs(context.Background(), levelDebug, "grouped",
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
	origV, origD, origW, origE := verboseLogger, debugLogger, warnLogger, errorLogger
	t.Cleanup(func() {
		verboseLogger, debugLogger, warnLogger, errorLogger = origV, origD, origW, origE
	})

	vBuf, dBuf, wBuf, eBuf := &bytes.Buffer{}, &bytes.Buffer{}, &bytes.Buffer{}, &bytes.Buffer{}
	verboseLogger = bufLoggerPlus(vBuf, levelVerb)
	debugLogger = bufLoggerPlus(dBuf, levelDebug)
	warnLogger = bufLoggerPlus(wBuf, levelWarn)
	errorLogger = bufLoggerPlus(eBuf, levelError)

	ctx := context.Background()
	Vf(ctx, "v=%d", 1)
	Df(ctx, "d=%d", 2)
	Wf(ctx, "w=%d", 3)
	Ef(ctx, "e=%d", 4)

	checks := []struct {
		name  string
		buf   *bytes.Buffer
		label string
		msg   string
	}{
		{"Vf", vBuf, "verb", "v=1"},
		{"Df", dBuf, "debug", "d=2"},
		{"Wf", wBuf, "warn", "w=3"},
		{"Ef", eBuf, "error", "e=4"},
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
