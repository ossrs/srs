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

// swapNotify replaces signalNotify with a capturing fake and returns a getter
// for the channel registered by the code under test plus a restore func.
func swapNotify(t *testing.T) (func() chan<- os.Signal, func()) {
	t.Helper()
	orig := signalNotify
	var (
		mu sync.Mutex
		ch chan<- os.Signal
	)
	signalNotify = func(c chan<- os.Signal, _ ...os.Signal) {
		mu.Lock()
		defer mu.Unlock()
		ch = c
	}
	return func() chan<- os.Signal {
			mu.Lock()
			defer mu.Unlock()
			return ch
		}, func() {
			signalNotify = orig
		}
}

func swapExit(t *testing.T) (*int32, chan int, func()) {
	t.Helper()
	orig := osExit
	var called int32
	done := make(chan int, 1)
	osExit = func(code int) {
		atomic.StoreInt32(&called, 1)
		select {
		case done <- code:
		default:
		}
		// Block to mimic os.Exit never returning; the goroutine holding us
		// here is abandoned when the test ends.
		select {}
	}
	return &called, done, func() { osExit = orig }
}

func TestInstallSignals_CancelsOnSignal(t *testing.T) {
	getCh, restore := swapNotify(t)
	defer restore()

	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()

	InstallSignals(ctx, cancel)

	ch := getCh()
	if ch == nil {
		t.Fatal("signalNotify was not called")
	}
	ch <- syscall.SIGINT

	select {
	case <-ctx.Done():
	case <-time.After(time.Second):
		t.Fatal("ctx was not canceled after signal")
	}
}

func TestInstallSignals_HandlesRepeatedSignals(t *testing.T) {
	getCh, restore := swapNotify(t)
	defer restore()

	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()

	InstallSignals(ctx, cancel)
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
	fakeEnv := &envfakes.FakeEnvironment{}
	fakeEnv.ForceQuitTimeoutReturns("not-a-duration")

	err := InstallForceQuit(t.Context(), fakeEnv)
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
	called, done, restore := swapExit(t)
	defer restore()

	fakeEnv := &envfakes.FakeEnvironment{}
	fakeEnv.ForceQuitTimeoutReturns("1ms")

	ctx, cancel := context.WithCancel(t.Context())
	if err := InstallForceQuit(ctx, fakeEnv); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}

	// Before cancel, the goroutine is blocked and exit must not fire.
	if atomic.LoadInt32(called) != 0 {
		t.Fatal("osExit called before ctx cancel")
	}
	cancel()

	select {
	case code := <-done:
		if code != 1 {
			t.Fatalf("exit code = %d, want 1", code)
		}
	case <-time.After(time.Second):
		t.Fatal("osExit not called after cancel + timeout")
	}
}

func TestInstallForceQuit_WaitsForCancelBeforeSleeping(t *testing.T) {
	called, done, restore := swapExit(t)
	defer restore()

	fakeEnv := &envfakes.FakeEnvironment{}
	fakeEnv.ForceQuitTimeoutReturns("10ms")

	// Intentionally use a never-canceled context and leak the goroutine:
	// if we canceled at test end, the goroutine would wake and race with
	// restore() writing osExit.
	if err := InstallForceQuit(context.Background(), fakeEnv); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}

	select {
	case <-done:
		t.Fatal("osExit fired without ctx cancel")
	case <-time.After(30 * time.Millisecond):
	}
	if atomic.LoadInt32(called) != 0 {
		t.Fatal("osExit called unexpectedly")
	}
}
