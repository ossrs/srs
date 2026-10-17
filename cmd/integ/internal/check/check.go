// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package check is the case helper. Every case runs through Case, which
// declares its class, times it against the class limit, collects its checks,
// handles known bugs and records the result for the summary of TestMain.
package check

import (
	"flag"
	"fmt"
	"reflect"
	"runtime"
	"runtime/debug"
	"sort"
	"strings"
	"sync"
	"testing"
	"time"
)

var knownBugsFail bool

func init() {
	flag.Func("integ.known-bugs", "pass (default): a known bug that fails passes; fail: it fails like a normal case", func(v string) error {
		switch v {
		case "pass":
			knownBugsFail = false
		case "fail":
			knownBugsFail = true
		default:
			return fmt.Errorf("want pass or fail, got %q", v)
		}
		return nil
	})
}

// Class is how long a case may take, measured inside the case.
type Class int

const (
	// Fast is for API, hooks, security, static files, and a play from the GOP cache.
	Fast Class = iota + 1
	// Normal is for most streaming cases, handshakes, segment groups and chains.
	Normal
	// Timer is for cases that wait on a whole-second SRS timer or a 1s tick,
	// only in TestTimer_ groups.
	Timer
)

// Limit returns the longest a case of the class may take.
func (c Class) Limit() time.Duration {
	switch c {
	case Fast:
		return 100 * time.Millisecond
	case Normal:
		return 500 * time.Millisecond
	case Timer:
		return 10 * time.Second
	}
	return 0
}

func (c Class) String() string {
	switch c {
	case Fast:
		return "Fast"
	case Normal:
		return "Normal"
	case Timer:
		return "Timer"
	}
	return fmt.Sprintf("Class(%d)", int(c))
}

// Outcome is how a case ended.
type Outcome int

const (
	Passed Outcome = iota + 1
	Failed
	Skipped
	// KnownBugFailed is a known bug that failed as expected; it passes.
	KnownBugFailed
)

func (o Outcome) String() string {
	return [...]string{"", "passed", "failed", "skipped", "known bug"}[o]
}

// Result is the record of one case for the summary.
type Result struct {
	Name    string
	Class   Class
	Elapsed time.Duration
	Outcome Outcome
	// Reason is why a case skipped, or the reason of its known bug.
	Reason string
}

// C is the case. It is a testing.TB, so the adapter and the clients take it
// like a *testing.T, but its failures are collected and decided by the helper:
// Errorf and the Check methods record a failure and go on, Fatalf, FailNow and
// Must record one and stop the case, Skip stops it with a reason, and Cleanup
// runs inside the case when it ends.
type C struct {
	testing.TB

	mu       sync.Mutex
	failures []string
	skip     string
	skipped  bool
	knownBug string
	cleanups []func()
	now      func() time.Time
	untimed  time.Duration
}

// Untimed runs fn and leaves its time out of the case's time. The adapter uses
// it for a server start inside a case.
func (c *C) Untimed(fn func()) {
	start := c.now()
	defer func() {
		d := c.now().Sub(start)
		c.mu.Lock()
		c.untimed += d
		c.mu.Unlock()
	}()
	fn()
}

// Cleanup registers fn to run when the case ends, last registered first. It
// runs inside the case, so a failure in it fails the case, but its time is not
// part of the case's time.
func (c *C) Cleanup(fn func()) {
	c.mu.Lock()
	c.cleanups = append(c.cleanups, fn)
	c.mu.Unlock()
}

// runCleanups runs the cleanups, each one even when an earlier one panics or
// stops the case.
func (c *C) runCleanups() {
	c.mu.Lock()
	n := len(c.cleanups)
	if n == 0 {
		c.mu.Unlock()
		return
	}
	fn := c.cleanups[n-1]
	c.cleanups = c.cleanups[:n-1]
	c.mu.Unlock()

	defer c.runCleanups()
	defer c.recoverPanic()
	fn()
}

func (c *C) recoverPanic() {
	if v := recover(); v != nil {
		c.fail(fmt.Sprintf("panic: %v\n%s", v, debug.Stack()))
	}
}

func (c *C) fail(msg string) {
	c.mu.Lock()
	c.failures = append(c.failures, msg)
	c.mu.Unlock()
}

// Check records a failure with the message when ok is false. It returns ok.
func (c *C) Check(ok bool, format string, args ...any) bool {
	if !ok {
		c.fail(fmt.Sprintf(format, args...))
	}
	return ok
}

// CheckEqual records a failure when got is not deeply equal to want.
func (c *C) CheckEqual(got, want any, what string) bool {
	if reflect.DeepEqual(got, want) {
		return true
	}
	c.fail(fmt.Sprintf("%s: got %#v, want %#v", what, got, want))
	return false
}

// CheckNoError records a failure when err is not nil.
func (c *C) CheckNoError(err error, what string) bool {
	if err == nil {
		return true
	}
	c.fail(fmt.Sprintf("%s: %v", what, err))
	return false
}

// Must stops the case with a failure when err is not nil.
func (c *C) Must(err error, what string) {
	if err != nil {
		c.fail(fmt.Sprintf("%s: %v", what, err))
		runtime.Goexit()
	}
}

func (c *C) Error(args ...any)                 { c.fail(fmt.Sprintln(args...)) }
func (c *C) Errorf(format string, args ...any) { c.fail(fmt.Sprintf(format, args...)) }
func (c *C) Fail()                             { c.fail("failed") }
func (c *C) FailNow()                          { c.fail("failed"); runtime.Goexit() }
func (c *C) Fatal(args ...any)                 { c.Error(args...); runtime.Goexit() }
func (c *C) Fatalf(format string, args ...any) { c.Errorf(format, args...); runtime.Goexit() }

// Failed reports whether the case has a failure, known bug or not.
func (c *C) Failed() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return len(c.failures) > 0
}

func (c *C) Skip(args ...any)                 { c.skipNow(fmt.Sprint(args...)) }
func (c *C) Skipf(format string, args ...any) { c.skipNow(fmt.Sprintf(format, args...)) }
func (c *C) SkipNow()                         { c.skipNow("") }
func (c *C) Skipped() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.skipped
}

// skipNow stops the case as skipped, with a reason.
func (c *C) skipNow(reason string) {
	c.mu.Lock()
	c.skipped, c.skip = true, reason
	c.mu.Unlock()
	runtime.Goexit()
}

// KnownBug marks the case as finding an SRS bug: it still runs, a failure
// passes and logs KNOWN BUG, and a pass fails until the mark is removed.
func KnownBug(c *C, reason string) {
	c.mu.Lock()
	c.knownBug = reason
	c.mu.Unlock()
}

// Case runs fn as a parallel subtest of t, of the class it declares. With
// -integ.scale=N it runs N copies, each a case of its own.
func Case(t *testing.T, name string, class Class, fn func(*C)) {
	t.Helper()
	for _, n := range copyNames(name, class, scale) {
		t.Run(n, func(t *testing.T) {
			t.Parallel()
			r := &runner{now: time.Now, knownBugsFail: knownBugsFail, results: results, settleUntil: settleDeadline(), sleep: time.Sleep, limitScale: limitScale}
			r.run(t, class, fn)
		})
	}
}

// runner runs one case on a parent, which is its *testing.T.
type runner struct {
	now           func() time.Time
	knownBugsFail bool
	results       *registry
	// settleUntil is when a Fast or Normal case may start, zero for at once.
	settleUntil time.Time
	sleep       func(time.Duration)
	// limitScale multiplies the class limits; 1 or less keeps them.
	limitScale float64
}

func (r *runner) run(parent testing.TB, class Class, fn func(*C)) Result {
	parent.Helper()
	res := Result{Name: parent.Name(), Class: class}
	defer func() { r.results.add(res) }()

	if class == Timer && !strings.HasPrefix(res.Name, "TestTimer_") {
		parent.Errorf("Timer class only in TestTimer_ groups, not in %s", res.Name)
		res.Outcome = Failed
		return res
	}

	c := &C{TB: parent, now: r.now}
	r.waitSettle(class)
	start := r.now()
	done := make(chan struct{})
	go func() {
		// Deferred calls run last first: recover a panic, take the time, then
		// run the cleanups, so their failures count but their time doesn't.
		defer close(done)
		defer c.runCleanups()
		defer func() { res.Elapsed = r.now().Sub(start) - c.untimedTime() }()
		defer c.recoverPanic()
		fn(c)
	}()
	<-done

	c.mu.Lock()
	failures, skipped, knownBug := c.failures, c.skipped, c.knownBug
	c.mu.Unlock()

	failed := false
	errorf := func(format string, args ...any) {
		parent.Errorf(format, args...)
		failed = true
	}

	switch {
	case knownBug != "" && !r.knownBugsFail && len(failures) > 0:
		parent.Logf("KNOWN BUG: %s\n%s", knownBug, strings.Join(failures, "\n"))
		res.Reason = knownBug
	case knownBug != "" && !r.knownBugsFail && !skipped:
		errorf("bug fixed, remove the KnownBug mark: %s", knownBug)
	default:
		if knownBug != "" && len(failures) > 0 {
			parent.Logf("KNOWN BUG: %s", knownBug)
		}
		for _, f := range failures {
			errorf("%s", f)
		}
	}

	if limit := r.limit(class); res.Elapsed > limit {
		errorf("too slow: %dms > %dms", res.Elapsed.Milliseconds(), limit.Milliseconds())
	}

	switch {
	case failed:
		res.Outcome, res.Reason = Failed, ""
	case res.Reason != "":
		res.Outcome = KnownBugFailed
	case skipped:
		res.Outcome, res.Reason = Skipped, c.skip
		parent.Skip(c.skip)
	default:
		res.Outcome = Passed
	}
	return res
}

// registry holds the results of every case of the process.
type registry struct {
	mu  sync.Mutex
	all []Result
}

var results = &registry{}

func (r *registry) add(res Result) {
	r.mu.Lock()
	r.all = append(r.all, res)
	r.mu.Unlock()
}

func (r *registry) list() []Result {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]Result(nil), r.all...)
}

func (r *registry) summary(label string) string {
	all := r.list()
	count := map[Outcome]int{}
	for _, res := range all {
		count[res.Outcome]++
	}

	var b strings.Builder
	fmt.Fprintf(&b, "srs-integ %s: %d cases, %d passed, %d failed, %d skipped, %d known bugs\n",
		label, len(all), count[Passed], count[Failed], count[Skipped], count[KnownBugFailed])

	byName := append([]Result(nil), all...)
	sort.Slice(byName, func(i, j int) bool { return byName[i].Name < byName[j].Name })
	for _, res := range byName {
		switch res.Outcome {
		case Failed:
			fmt.Fprintf(&b, "  failed %s\n", res.Name)
		case Skipped:
			fmt.Fprintf(&b, "  skipped %s: %s\n", res.Name, res.Reason)
		case KnownBugFailed:
			fmt.Fprintf(&b, "  known bug %s: %s\n", res.Name, res.Reason)
		}
	}

	if len(all) == 0 {
		return b.String()
	}
	slow := append([]Result(nil), all...)
	sort.SliceStable(slow, func(i, j int) bool { return slow[i].Elapsed > slow[j].Elapsed })
	if len(slow) > 10 {
		slow = slow[:10]
	}
	b.WriteString("  slowest:\n")
	for _, res := range slow {
		fmt.Fprintf(&b, "    %dms %s %s\n", res.Elapsed.Milliseconds(), res.Class, res.Name)
	}
	return b.String()
}

// Summary returns the summary of every case of the process: the counts, the
// skipped cases with their reasons, the known bugs, the failed cases and the
// 10 slowest cases. The label names the process, such as main or timer.
func Summary(label string) string {
	return results.summary(label)
}

func (c *C) untimedTime() time.Duration {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.untimed
}
