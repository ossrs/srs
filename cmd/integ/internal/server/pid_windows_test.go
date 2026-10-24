// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"os"
	"strings"
	"syscall"
	"testing"
)

// TestSignalProcessOnlyTerm checks the only signal the adapter sends on
// Windows is SIGTERM, as Ctrl+Break: the others have no console event, so
// sending one fails, with nothing sent.
func TestSignalProcessOnlyTerm(t *testing.T) {
	self, err := os.FindProcess(os.Getpid())
	if err != nil {
		t.Fatal(err)
	}
	for _, sig := range []syscall.Signal{syscall.SIGHUP, syscall.SIGQUIT, syscall.SIGINT, syscall.Signal(10)} {
		if err := signalProcess(self, sig); err == nil || !strings.Contains(err.Error(), "Windows") {
			t.Errorf("signal %d: %v, want an error that Windows has no such event", int(sig), err)
		}
	}
}
