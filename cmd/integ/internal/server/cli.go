// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"testing"
	"time"
)

// CLIRun is a run of the SRS binary as a command, not as a server a case
// talks to: its exit code and output.
type CLIRun struct {
	// Exited is false when the process still ran at the timeout and was killed.
	Exited bool
	// Code is the exit code, -1 when the process was killed.
	Code int
	// Stdout and Stderr are what the process wrote.
	Stdout, Stderr string
	// Dir is the run's own dir, its working dir, removed when the case
	// passes.
	Dir string
}

// cliSlots is how many runs of the binary as a command may go at once. Each
// starts only while the process runs fewer SRS than its peak so far, waiting
// at most cliGateWait, and the check and the start are one step under
// cliStart, so the runs, each a short-lived SRS process, never raise the peak
// the servers set.
const cliSlots = 4

// cliGateWait is the longest a run waits for the process to run fewer SRS than
// its peak, for a process that starts no more servers.
const cliGateWait = 2 * time.Second

var (
	cliSlot  = newSlots(cliSlots)
	cliStart sync.Mutex
)

// CLIEnv returns the SRS_* env vars, sorted, of a run of the binary in dir:
// the adapter's process settings and every listener off, then env, which may
// set anything of SRS's, a listener or a bad value too, and TZ, the run's
// time zone.
func CLIEnv(dir string, env map[string]string) ([]string, error) {
	vars := map[string]string{
		"SRS_DAEMON":                  "off",
		"SRS_LOG_TANK":                "console",
		"SRS_PID":                     filepath.Join(dir, "srs.pid"),
		"SRS_CIRCUIT_BREAKER_ENABLED": "off",
		"SRS_STREAM_CASTER_ENABLED":   "off",
	}
	// RTMP has no enable var: with -e it has no port unless one is set.
	for _, info := range listeners {
		if info.enable != "" {
			vars[info.enable] = "off"
		}
	}
	for k, v := range env {
		if !strings.HasPrefix(k, "SRS_") && k != "TZ" {
			return nil, fmt.Errorf("env %s is not an SRS_ setting or TZ", k)
		}
		vars[k] = v
	}

	out := make([]string, 0, len(vars))
	for k, v := range vars {
		out = append(out, k+"="+v)
	}
	sort.Strings(out)
	return out, nil
}

// Exec runs the SRS binary with args and the env of CLIEnv in a dir of its
// own, and waits up to timeout for it to exit; it kills a process that still
// runs then, which started as a server. The run waits for a run slot and to
// start out of the case's time. The dir is removed when t passes, and kept,
// with the output in t's log, when it fails.
func Exec(t testing.TB, args []string, env map[string]string, timeout time.Duration) *CLIRun {
	t.Helper()
	if err := setup(); err != nil {
		t.Fatal(err)
	}
	dir := filepath.Join(run.dir, fmt.Sprintf("cli-%d", run.seq.Add(1)))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	vars, err := CLIEnv(dir, env)
	if err != nil {
		t.Fatal(err)
	}

	free := cliSlot.acquire(t)
	defer free()

	var stdout, stderr bytes.Buffer
	cmd := exec.Command(run.binary, args...)
	cmd.Dir = dir
	cmd.Env = append(processEnv(), vars...)
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	untimed(t, func() { err = run.procs.startGated(&cliStart, cliGateWait, cmd.Start) })
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	go func() {
		cmd.Wait()
		close(done)
	}()

	r := &CLIRun{Exited: true, Dir: dir}
	select {
	case <-done:
	case <-time.After(timeout):
		r.Exited = false
		cmd.Process.Kill()
		<-done
	}
	run.procs.exited()
	r.Code = cmd.ProcessState.ExitCode()
	r.Stdout, r.Stderr = stdout.String(), stderr.String()

	t.Cleanup(func() {
		if t.Failed() {
			t.Logf("SRS %s in %s: exited %v, code %d\nenv:\n%s\nstdout:\n%s\nstderr:\n%s",
				strings.Join(args, " "), dir, r.Exited, r.Code, strings.Join(vars, "\n"), r.Stdout, r.Stderr)
			return
		}
		os.RemoveAll(dir)
	})
	return r
}

// waitBelowPeak waits, polling every poll, until the process runs fewer SRS
// than its peak so far, so one more never raises it, and reports whether it
// did before max.
func (p *procCount) waitBelowPeak(max, poll time.Duration) bool {
	deadline := time.Now().Add(max)
	for p.live.Load() >= p.peak.Load() {
		if !time.Now().Before(deadline) {
			return false
		}
		time.Sleep(poll)
	}
	return true
}

// startGated runs start, which starts an SRS process, once the process runs
// fewer SRS than its peak so far, waiting at most max, and counts it. The check
// and the start are one step under mu, so two gated starts never both take the
// last place below the peak.
func (p *procCount) startGated(mu *sync.Mutex, max time.Duration, start func() error) error {
	mu.Lock()
	defer mu.Unlock()
	p.waitBelowPeak(max, 10*time.Millisecond)
	if err := start(); err != nil {
		return err
	}
	p.started()
	return nil
}
