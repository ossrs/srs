// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"errors"
	"fmt"
	"strings"
	"testing"
	"time"
)

// fakeTB is the parent of a case under test. It records what the helper tells
// the parent; any other testing.TB method panics on the nil embedded TB.
type fakeTB struct {
	testing.TB
	name     string
	errors   []string
	logs     []string
	skips    []string
	cleanups []func()
}

func (f *fakeTB) Helper()      {}
func (f *fakeTB) Name() string { return f.name }
func (f *fakeTB) Errorf(format string, args ...any) {
	f.errors = append(f.errors, fmt.Sprintf(format, args...))
}
func (f *fakeTB) Logf(format string, args ...any) {
	f.logs = append(f.logs, fmt.Sprintf(format, args...))
}
func (f *fakeTB) Skip(args ...any)  { f.skips = append(f.skips, fmt.Sprint(args...)) }
func (f *fakeTB) Cleanup(fn func()) { f.cleanups = append(f.cleanups, fn) }

// fakeClock moves only when a case sleeps on it.
type fakeClock struct{ now time.Time }

func (c *fakeClock) Now() time.Time          { return c.now }
func (c *fakeClock) Sleep(d time.Duration)   { c.now = c.now.Add(d) }
func newRunner(clock *fakeClock) *runner     { return &runner{now: clock.Now, results: &registry{}} }
func joined(lines []string) string           { return strings.Join(lines, "\n") }
func contains(lines []string, s string) bool { return strings.Contains(joined(lines), s) }

func TestClassLimits(t *testing.T) {
	for class, want := range map[Class]time.Duration{Fast: 100 * time.Millisecond, Normal: 500 * time.Millisecond, Timer: 10 * time.Second} {
		if got := class.Limit(); got != want {
			t.Errorf("%v.Limit() = %v, want %v", class, got, want)
		}
	}
	for class, want := range map[Class]string{Fast: "Fast", Normal: "Normal", Timer: "Timer"} {
		if got := class.String(); got != want {
			t.Errorf("String() = %q, want %q", got, want)
		}
	}
}

func TestPassingCase(t *testing.T) {
	clock := &fakeClock{}
	r := newRunner(clock)
	tb := &fakeTB{name: "TestRtmp_Common/Play"}

	res := r.run(tb, Normal, func(c *C) {
		clock.Sleep(499 * time.Millisecond)
		c.Check(true, "always")
		c.CheckEqual(3, 3, "frames")
		c.CheckNoError(nil, "play")
	})

	if len(tb.errors) != 0 || len(tb.skips) != 0 {
		t.Errorf("errors %q, skips %q, want none", tb.errors, tb.skips)
	}
	if res.Outcome != Passed || res.Elapsed != 499*time.Millisecond || res.Class != Normal || res.Name != "TestRtmp_Common/Play" {
		t.Errorf("result %+v, want passed in 499ms", res)
	}
}

func TestSlowCaseFails(t *testing.T) {
	clock := &fakeClock{}
	r := newRunner(clock)
	tb := &fakeTB{name: "TestApi_Common/Slow"}

	res := r.run(tb, Fast, func(c *C) { clock.Sleep(150 * time.Millisecond) })

	if res.Outcome != Failed || !contains(tb.errors, "too slow: 150ms > 100ms") {
		t.Errorf("outcome %v, errors %q; want failed with too slow: 150ms > 100ms", res.Outcome, tb.errors)
	}
}

// TestUntimedIsNotCounted covers a server start inside a case, such as the
// Common server started by the first case that uses it.
func TestUntimedIsNotCounted(t *testing.T) {
	clock := &fakeClock{}
	r := newRunner(clock)
	tb := &fakeTB{name: "TestApi_Common/First"}
	ran := false

	res := r.run(tb, Fast, func(c *C) {
		c.Untimed(func() { ran = true; clock.Sleep(2 * time.Second) })
		clock.Sleep(30 * time.Millisecond)
	})

	if !ran || res.Outcome != Passed || res.Elapsed != 30*time.Millisecond || len(tb.errors) != 0 {
		t.Errorf("result %+v, errors %q; want passed in 30ms", res, tb.errors)
	}
}

func TestFailedChecksReachTheParent(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestApi_Common/Checks"}

	res := r.run(tb, Fast, func(c *C) {
		c.Check(false, "status %d, want %d", 404, 200)
		c.CheckEqual("aac", "opus", "audio codec")
		c.CheckNoError(errors.New("refused"), "connect")
	})

	if res.Outcome != Failed || len(tb.errors) != 3 {
		t.Fatalf("outcome %v, errors %q; want failed with 3 errors", res.Outcome, tb.errors)
	}
	for _, want := range []string{"status 404, want 200", `audio codec: got "aac", want "opus"`, "connect: refused"} {
		if !contains(tb.errors, want) {
			t.Errorf("errors %q lack %q", tb.errors, want)
		}
	}
}

func TestFatalStopsTheCase(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestApi_Common/Fatal"}
	after := false

	res := r.run(tb, Fast, func(c *C) {
		c.Must(errors.New("no server"), "start")
		after = true
	})

	if after {
		t.Error("the case went on after Must failed")
	}
	if res.Outcome != Failed || !contains(tb.errors, "start: no server") {
		t.Errorf("outcome %v, errors %q; want failed with start: no server", res.Outcome, tb.errors)
	}
}

func TestPanicFailsTheCase(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestApi_Common/Panic"}

	res := r.run(tb, Fast, func(c *C) { panic("boom") })

	if res.Outcome != Failed || !contains(tb.errors, "panic: boom") {
		t.Errorf("outcome %v, errors %q; want failed with panic: boom", res.Outcome, tb.errors)
	}
}

func TestKnownBugThatFailsPasses(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestRtmp_Common/Bug"}

	res := r.run(tb, Normal, func(c *C) {
		KnownBug(c, "no onStatus on unpublish")
		c.Check(false, "onStatus missing")
	})

	if res.Outcome != KnownBugFailed || len(tb.errors) != 0 {
		t.Errorf("outcome %v, errors %q; want a known bug with no errors", res.Outcome, tb.errors)
	}
	if !contains(tb.logs, "KNOWN BUG: no onStatus on unpublish") || !contains(tb.logs, "onStatus missing") {
		t.Errorf("logs %q, want KNOWN BUG: no onStatus on unpublish and the failed check", tb.logs)
	}
	if res.Reason != "no onStatus on unpublish" {
		t.Errorf("reason %q", res.Reason)
	}
}

func TestKnownBugStoppedByMustPasses(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestRtmp_Common/BugMust"}

	res := r.run(tb, Normal, func(c *C) {
		KnownBug(c, "play is refused")
		c.Must(errors.New("refused"), "play")
	})

	if res.Outcome != KnownBugFailed || len(tb.errors) != 0 {
		t.Errorf("outcome %v, errors %q; want a known bug with no errors", res.Outcome, tb.errors)
	}
}

func TestKnownBugThatPassesFails(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestRtmp_Common/Fixed"}

	res := r.run(tb, Normal, func(c *C) {
		KnownBug(c, "no onStatus on unpublish")
		c.Check(true, "onStatus missing")
	})

	if res.Outcome != Failed || !contains(tb.errors, "bug fixed, remove the KnownBug mark") {
		t.Errorf("outcome %v, errors %q; want failed with bug fixed, remove the KnownBug mark", res.Outcome, tb.errors)
	}
}

func TestKnownBugsFailMode(t *testing.T) {
	r := newRunner(&fakeClock{})
	r.knownBugsFail = true
	tb := &fakeTB{name: "TestRtmp_Common/Bug"}

	res := r.run(tb, Normal, func(c *C) {
		KnownBug(c, "no onStatus on unpublish")
		c.Check(false, "onStatus missing")
	})

	if res.Outcome != Failed || !contains(tb.errors, "onStatus missing") || !contains(tb.logs, "KNOWN BUG: no onStatus on unpublish") {
		t.Errorf("outcome %v, errors %q, logs %q; want failed like a normal case", res.Outcome, tb.errors, tb.logs)
	}
}

func TestKnownBugTooSlowStillFails(t *testing.T) {
	clock := &fakeClock{}
	r := newRunner(clock)
	tb := &fakeTB{name: "TestRtmp_Common/SlowBug"}

	res := r.run(tb, Fast, func(c *C) {
		KnownBug(c, "late reply")
		c.Check(false, "no reply")
		clock.Sleep(time.Second)
	})

	if res.Outcome != Failed || !contains(tb.errors, "too slow: 1000ms > 100ms") {
		t.Errorf("outcome %v, errors %q; want failed with too slow", res.Outcome, tb.errors)
	}
}

func TestSkipHasAReason(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestRtc_Common/Av1"}
	after := false

	res := r.run(tb, Normal, func(c *C) {
		c.Skip("ffmpeg lacks libsvtav1")
		after = true
	})

	if after {
		t.Error("the case went on after Skip")
	}
	if res.Outcome != Skipped || res.Reason != "ffmpeg lacks libsvtav1" || !contains(tb.skips, "ffmpeg lacks libsvtav1") || len(tb.errors) != 0 {
		t.Errorf("result %+v, skips %q, errors %q; want skipped with its reason", res, tb.skips, tb.errors)
	}
}

func TestTimerClassOnlyInTimerGroups(t *testing.T) {
	clock := &fakeClock{}
	r := newRunner(clock)

	ran := false
	tb := &fakeTB{name: "TestHls_Common/Dispose"}
	res := r.run(tb, Timer, func(c *C) { ran = true })
	if ran || res.Outcome != Failed || !contains(tb.errors, "Timer class only in TestTimer_ groups") {
		t.Errorf("ran %v, outcome %v, errors %q; want failed without running", ran, res.Outcome, tb.errors)
	}

	tb = &fakeTB{name: "TestTimer_Hls_Dispose/FilesRemoved"}
	res = r.run(tb, Timer, func(c *C) { clock.Sleep(9 * time.Second) })
	if res.Outcome != Passed || len(tb.errors) != 0 {
		t.Errorf("outcome %v, errors %q; want passed", res.Outcome, tb.errors)
	}
}

func TestCleanupSeesTheFailure(t *testing.T) {
	r := newRunner(&fakeClock{})
	tb := &fakeTB{name: "TestApi_Common/Cleanup"}
	failed := false

	var order []int

	r.run(tb, Fast, func(c *C) {
		c.Cleanup(func() { order = append(order, 1); failed = c.Failed() })
		c.Cleanup(func() { order = append(order, 2) })
		c.Errorf("bad")
	})
	if !failed {
		t.Error("Failed() is false in the cleanup of a failed case")
	}
	if len(order) != 2 || order[0] != 2 || order[1] != 1 {
		t.Errorf("cleanups ran in order %v, want [2 1]", order)
	}
	if len(tb.cleanups) != 0 {
		t.Errorf("%d cleanups left to the parent, want 0: they run inside the case", len(tb.cleanups))
	}
}

// TestCleanupFailureFailsTheCase covers a cleanup that stops a server and
// fails: the case fails, every cleanup still runs, and their time is not part
// of the case's time.
func TestCleanupFailureFailsTheCase(t *testing.T) {
	clock := &fakeClock{}
	r := newRunner(clock)
	tb := &fakeTB{name: "TestApi_Common/CleanupFails"}
	first := false

	res := r.run(tb, Fast, func(c *C) {
		c.Cleanup(func() { first = true })
		c.Cleanup(func() { clock.Sleep(time.Second); c.Fatalf("stop: exit code 1") })
	})

	if !first {
		t.Error("an earlier cleanup did not run after a later one stopped the case")
	}
	if res.Outcome != Failed || !contains(tb.errors, "stop: exit code 1") || contains(tb.errors, "too slow") {
		t.Errorf("outcome %v, errors %q; want failed by the cleanup only, not too slow", res.Outcome, tb.errors)
	}
}

func TestSummary(t *testing.T) {
	reg := &registry{}
	add := func(name string, ms int, o Outcome, reason string) {
		reg.add(Result{Name: name, Class: Normal, Elapsed: time.Duration(ms) * time.Millisecond, Outcome: o, Reason: reason})
	}
	for i := 1; i <= 12; i++ {
		add(fmt.Sprintf("TestA_Common/Case%02d", i), i*10, Passed, "")
	}
	add("TestA_Common/Broken", 5, Failed, "")
	add("TestA_Common/Av1", 1, Skipped, "ffmpeg lacks libsvtav1")
	add("TestA_Common/Bug", 2, KnownBugFailed, "no onStatus")

	got := reg.summary("main")
	for _, want := range []string{
		"srs-integ main: 15 cases, 12 passed, 1 failed, 1 skipped, 1 known bugs",
		"skipped TestA_Common/Av1: ffmpeg lacks libsvtav1",
		"known bug TestA_Common/Bug: no onStatus",
		"failed TestA_Common/Broken",
		"slowest:",
		"120ms Normal TestA_Common/Case12",
		"30ms Normal TestA_Common/Case03",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("summary lacks %q:\n%s", want, got)
		}
	}
	// Only the 10 slowest, slowest first.
	if strings.Contains(got, "TestA_Common/Case02") || strings.Index(got, "Case12") > strings.Index(got, "Case11") {
		t.Errorf("summary does not list the 10 slowest, slowest first:\n%s", got)
	}

	if got := (&registry{}).summary("timer"); !strings.Contains(got, "srs-integ timer: 0 cases") {
		t.Errorf("empty summary %q", got)
	}
}

// TestCaseWithTesting runs cases through a real *testing.T.
func TestCaseWithTesting(t *testing.T) {
	before := len(results.list())
	t.Run("Group", func(t *testing.T) {
		Case(t, "Pass", Fast, func(c *C) { c.Check(true, "ok") })
		Case(t, "Skip", Fast, func(c *C) { c.Skip("no tool") })
	})
	got := results.list()[before:]
	if len(got) != 2 {
		t.Fatalf("%d results, want 2", len(got))
	}
	outcomes := map[string]Outcome{}
	for _, r := range got {
		outcomes[r.Name] = r.Outcome
	}
	if outcomes["TestCaseWithTesting/Group/Pass"] != Passed || outcomes["TestCaseWithTesting/Group/Skip"] != Skipped {
		t.Errorf("outcomes %v", outcomes)
	}
}
