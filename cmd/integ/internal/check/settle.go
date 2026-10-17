// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"flag"
	"time"
)

// processStart is when this test process started, about when its first cases
// and their servers start.
var processStart = time.Now()

// settle is how long after the process starts its Fast and Normal cases wait
// before they start their time. The timer process starts every case at once,
// beside the main process's first cases, and its few short cases would
// otherwise be timed in the busiest second of the run, on servers each case
// of the process publishes to at once. Its long timer cases set its run time,
// so waiting costs nothing.
var settle time.Duration

func init() {
	flag.DurationVar(&settle, "integ.settle", 0, "start the Fast and Normal cases no earlier than this after the process starts; timer cases start at once")
}

// waitSettle waits, before a case of the class starts its time, until the
// runner's settle is over.
func (r *runner) waitSettle(class Class) {
	if class == Timer || r.settleUntil.IsZero() {
		return
	}
	if d := r.settleUntil.Sub(r.now()); d > 0 {
		r.sleep(d)
	}
}

// settleDeadline is when the settle of this process ends, or zero for none.
func settleDeadline() time.Time {
	if settle <= 0 {
		return time.Time{}
	}
	return processStart.Add(settle)
}
