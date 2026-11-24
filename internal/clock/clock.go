// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package clock

import (
	"sync"
	"sync/atomic"
	"time"
)

var (
	// Tests set a smaller one before the first Now, so they wait less.
	Resolution = 100 * time.Millisecond

	start  sync.Once
	cached atomic.Pointer[time.Time]
)

// Reading the clock once per packet took 6% of the WebRTC proxy's CPU in a
// Docker VM, where time.Now costs about 400ns.
func Now() time.Time {
	start.Do(func() {
		update()
		go func() {
			for range time.Tick(Resolution) {
				update()
			}
		}()
	})
	return *cached.Load()
}

func update() {
	now := time.Now()
	cached.Store(&now)
}
