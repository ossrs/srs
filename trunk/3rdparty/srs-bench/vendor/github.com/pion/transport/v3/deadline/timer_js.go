// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build js
// +build js

package deadline

import (
	"sync"
	"time"
)

// jsTimer is a timer utility for wasm with a working Reset function.
type jsTimer struct {
	f       func()
	mu      sync.Mutex
	timer   *time.Timer
	version uint64
	started bool
}

func afterFunc(d time.Duration, f func()) timer {
	t := &jsTimer{f: f}
	t.Reset(d)
	return t
}

func (t *jsTimer) Stop() bool {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.version++
	t.timer.Stop()

	started := t.started
	t.started = false
	return started
}

func (t *jsTimer) Reset(d time.Duration) bool {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.timer != nil {
		t.timer.Stop()
	}

	t.version++
	version := t.version
	t.timer = time.AfterFunc(d, func() {
		t.mu.Lock()
		if version != t.version {
			t.mu.Unlock()
			return
		}

		t.started = false
		t.mu.Unlock()

		t.f()
	})

	started := t.started
	t.started = true
	return started
}
