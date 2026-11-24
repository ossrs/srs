// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package clock

import (
	"os"
	"testing"
	"time"
)

func TestMain(m *testing.M) {
	Resolution = time.Millisecond
	os.Exit(m.Run())
}

func TestNow_NeverAheadOfWallClock(t *testing.T) {
	for i := 0; i < 1000; i++ {
		got := Now()
		if now := time.Now(); got.After(now) {
			t.Fatalf("Now()=%v is ahead of the wall clock %v", got, now)
		}
	}
}

func TestNow_CatchesUpWithWallClock(t *testing.T) {
	for i := 0; i < 3; i++ {
		want := time.Now()
		deadline := want.Add(time.Second)
		for Now().Before(want) {
			if time.Now().After(deadline) {
				t.Fatalf("Now()=%v did not reach %v in a second", Now(), want)
			}
			time.Sleep(Resolution)
		}
	}
}
