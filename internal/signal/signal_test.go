// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package signal

import (
	"context"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"srsx/internal/env/envfakes"
)

// captureNotify returns a Handler whose notify field records the channel
// passed by the code under test, plus a getter that retrieves it.
func captureNotify() (*Handler, func() chan<- os.Signal) {
	var (
		mu sync.Mutex
		ch chan<- os.Signal
	)
	h := &Handler{
		notify: func(c chan<- os.Signal, _ ...os.Signal) {
			mu.Lock()
			defer mu.Unlock()
			ch = c
		},
		exit: os.Exit,
	}
	return h, func() chan<- os.Signal {
		mu.Lock()
		defer mu.Unlock()
		return ch
	}
}

// captureExit returns a Handler whose exit field records the code and never
// returns, plus a flag and channel that observe the call.
func captureExit() (*Handler, *int32, chan int) {
	var called int32
	done := make(chan int, 1)
	h := &Handler{
		notify: func(chan<- os.Signal, ...os.Signal) {},
		exit: func(code int) {
			atomic.StoreInt32(&called, 1)
			select {
			case done <- code:
			default:
			}
			// Block to mimic os.Exit never returning; the goroutine holding us
			// here is abandoned when the test ends.
			select {}
		},
	}
	return h, &called, done
}

func TestInstallSignals_CancelsOnSignal(t *testing.T) {
	h, getCh := captureNotify()

	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()

	h.InstallSignals(ctx, cancel)

	ch := getCh()
	if ch == nil {
		t.Fatal("notify was not called")
	}
	ch <- syscall.SIGINT

	select {
	case <-ctx.Done():
	case <-time.After(time.Second):
		t.Fatal("ctx was not canceled after signal")
	}
}

func TestInstallSignals_HandlesRepeatedSignals(t *testing.T) {
	h, getCh := captureNotify()

	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()

	h.InstallSignals(ctx, cancel)
	ch := getCh()

	// Multiple signals must not panic; cancel() is idempotent.
	ch <- syscall.SIGINT
	ch <- syscall.SIGTERM
	ch <- os.Interrupt

	select {
	case <-ctx.Done():
	case <-time.After(time.Second):
		t.Fatal("ctx was not canceled")
	}
}

func TestInstallForceQuit_InvalidDurationReturnsError(t *testing.T) {
	fakeEnv := &envfakes.FakeProxyEnvironment{}
	fakeEnv.ForceQuitTimeoutReturns("not-a-duration")

	err := NewHandler().InstallForceQuit(t.Context(), fakeEnv)
	if err == nil {
		t.Fatal("want error for bad duration")
	}
	if !strings.Contains(err.Error(), "parse force timeout") {
		t.Fatalf("err = %v", err)
	}
	if !strings.Contains(err.Error(), "not-a-duration") {
		t.Fatalf("err missing input: %v", err)
	}
}

func TestInstallForceQuit_ExitsAfterTimeout(t *testing.T) {
	h, called, done := captureExit()

	fakeEnv := &envfakes.FakeProxyEnvironment{}
	fakeEnv.ForceQuitTimeoutReturns("1ms")

	ctx, cancel := context.WithCancel(t.Context())
	if err := h.InstallForceQuit(ctx, fakeEnv); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}

	// Before cancel, the goroutine is blocked and exit must not fire.
	if atomic.LoadInt32(called) != 0 {
		t.Fatal("exit called before ctx cancel")
	}
	cancel()

	select {
	case code := <-done:
		if code != 1 {
			t.Fatalf("exit code = %d, want 1", code)
		}
	case <-time.After(time.Second):
		t.Fatal("exit not called after cancel + timeout")
	}
}

func TestInstallForceQuit_WaitsForCancelBeforeSleeping(t *testing.T) {
	h, called, done := captureExit()

	fakeEnv := &envfakes.FakeProxyEnvironment{}
	fakeEnv.ForceQuitTimeoutReturns("10ms")

	// Intentionally use a never-canceled context and leak the goroutine: the
	// handler's exit closure is owned by this test instance, so leaving the
	// goroutine alive doesn't race other tests.
	if err := h.InstallForceQuit(context.Background(), fakeEnv); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}

	select {
	case <-done:
		t.Fatal("exit fired without ctx cancel")
	case <-time.After(30 * time.Millisecond):
	}
	if atomic.LoadInt32(called) != 0 {
		t.Fatal("exit called unexpectedly")
	}
}

func TestNewHandler_UsesRealOSDefaults(t *testing.T) {
	h := NewHandler()
	if h.notify == nil {
		t.Error("notify default not set")
	}
	if h.exit == nil {
		t.Error("exit default not set")
	}
}
