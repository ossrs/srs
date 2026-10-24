// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"testing"
	"time"
)

// settledRunner is a runner whose Fast and Normal cases start no earlier than
// settle after the clock's start.
func settledRunner(clock *fakeClock, settle time.Duration) *runner {
	r := newRunner(clock)
	r.settleUntil = clock.now.Add(settle)
	r.sleep = clock.Sleep
	return r
}

func TestSettleDelaysShortClasses(t *testing.T) {
	for _, class := range []Class{Fast, Normal} {
		clock := &fakeClock{now: time.Unix(1000, 0)}
		r := settledRunner(clock, time.Second)
		var started time.Time
		res := r.run(&fakeTB{name: "TestTimer_Hls_Cleanup/ExpiredKept"}, class, func(c *C) {
			started = clock.Now()
			clock.Sleep(50 * time.Millisecond)
		})
		if want := time.Unix(1001, 0); !started.Equal(want) {
			t.Errorf("%v case started at %v, want %v, after the settle", class, started, want)
		}
		if res.Outcome != Passed || res.Elapsed != 50*time.Millisecond {
			t.Errorf("%v result %+v, want passed in 50ms, the settle not counted", class, res)
		}
	}
}

func TestSettleKeepsTimerAtOnce(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	r := settledRunner(clock, time.Second)
	var started time.Time
	r.run(&fakeTB{name: "TestTimer_Hls_Dispose/FilesRemoved"}, Timer, func(c *C) { started = clock.Now() })
	if want := time.Unix(1000, 0); !started.Equal(want) {
		t.Errorf("timer case started at %v, want %v, at once", started, want)
	}
}

func TestSettlePassed(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	r := settledRunner(clock, time.Second)
	clock.Sleep(2 * time.Second)
	var started time.Time
	r.run(&fakeTB{name: "TestTimer_Signal_Console/Usr1KeepsConsole"}, Normal, func(c *C) { started = clock.Now() })
	if want := time.Unix(1002, 0); !started.Equal(want) {
		t.Errorf("case started at %v, want %v, at once after the settle", started, want)
	}
}

func TestSettleOffByDefault(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	r := newRunner(clock)
	var started time.Time
	r.run(&fakeTB{name: "TestRtmp_Common/Play"}, Fast, func(c *C) { started = clock.Now() })
	if want := time.Unix(1000, 0); !started.Equal(want) {
		t.Errorf("case started at %v, want %v, at once", started, want)
	}
}
