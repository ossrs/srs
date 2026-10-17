// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"flag"
	"testing"
	"time"
)

// scaledRunner is a runner whose class limits are multiplied by scale.
func scaledRunner(clock *fakeClock, scale float64) *runner {
	r := newRunner(clock)
	r.limitScale = scale
	return r
}

func TestLimitScaleAllowsSlowerCases(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	tb := &fakeTB{name: "TestRtmp_Common/Play"}
	res := scaledRunner(clock, 3).run(tb, Normal, func(c *C) { clock.Sleep(1400 * time.Millisecond) })
	if res.Outcome != Passed || len(tb.errors) != 0 {
		t.Errorf("outcome %v, errors %q; want passed in 1400ms, under 3 times 500ms", res.Outcome, tb.errors)
	}
}

func TestLimitScaleStillFailsTooSlow(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	tb := &fakeTB{name: "TestApi_Common/Versions"}
	res := scaledRunner(clock, 2.5).run(tb, Fast, func(c *C) { clock.Sleep(300 * time.Millisecond) })
	if res.Outcome != Failed || !contains(tb.errors, "too slow: 300ms > 250ms") {
		t.Errorf("outcome %v, errors %q; want failed with too slow: 300ms > 250ms", res.Outcome, tb.errors)
	}
}

func TestLimitScaleOffByDefault(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	tb := &fakeTB{name: "TestRtmp_Common/Play"}
	res := newRunner(clock).run(tb, Normal, func(c *C) { clock.Sleep(600 * time.Millisecond) })
	if res.Outcome != Failed || !contains(tb.errors, "too slow: 600ms > 500ms") {
		t.Errorf("outcome %v, errors %q; want failed with too slow: 600ms > 500ms", res.Outcome, tb.errors)
	}
}

func TestLimitScaleFlag(t *testing.T) {
	f := flag.Lookup("integ.limit-scale")
	if f == nil {
		t.Fatal("no -integ.limit-scale flag")
	}
	defer f.Value.Set("1")
	for _, bad := range []string{"0", "0.5", "-2", "abc"} {
		if err := f.Value.Set(bad); err == nil {
			t.Errorf("-integ.limit-scale=%v accepted, want an error", bad)
		}
	}
	if err := f.Value.Set("4"); err != nil || limitScale != 4 {
		t.Errorf("-integ.limit-scale=4: err %v, scale %v; want 4", err, limitScale)
	}
}

func TestScaled(t *testing.T) {
	defer func(s float64) { limitScale = s }(limitScale)
	limitScale = 1
	if got := Scaled(300 * time.Millisecond); got != 300*time.Millisecond {
		t.Errorf("Scaled(300ms) = %v at scale 1, want 300ms", got)
	}
	limitScale = 4
	if got := Scaled(300 * time.Millisecond); got != 1200*time.Millisecond {
		t.Errorf("Scaled(300ms) = %v at scale 4, want 1.2s", got)
	}
}
