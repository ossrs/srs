// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"bytes"
	"io"
	"strings"
	"sync"
)

// logTail copies a process's output to w and keeps its last n lines, to
// attach to a failing case.
type logTail struct {
	mu      sync.Mutex
	w       io.Writer
	n       int
	lines   []string
	partial []byte
}

func newLogTail(w io.Writer, n int) *logTail {
	return &logTail{w: w, n: n}
}

func (l *logTail) Write(p []byte) (int, error) {
	l.mu.Lock()
	defer l.mu.Unlock()

	data := append(l.partial, p...)
	for {
		i := bytes.IndexByte(data, '\n')
		if i < 0 {
			break
		}
		l.add(strings.TrimSuffix(string(data[:i]), "\r"))
		data = data[i+1:]
	}
	l.partial = append([]byte(nil), data...)

	if _, err := l.w.Write(p); err != nil {
		return 0, err
	}
	return len(p), nil
}

func (l *logTail) add(line string) {
	l.lines = append(l.lines, line)
	if len(l.lines) > l.n {
		l.lines = l.lines[len(l.lines)-l.n:]
	}
}

// Lines returns the last lines, oldest first, including a last line that has
// no newline yet.
func (l *logTail) Lines() []string {
	l.mu.Lock()
	defer l.mu.Unlock()

	out := append([]string(nil), l.lines...)
	if len(l.partial) > 0 {
		out = append(out, string(l.partial))
	}
	if len(out) > l.n {
		out = out[len(out)-l.n:]
	}
	return out
}
