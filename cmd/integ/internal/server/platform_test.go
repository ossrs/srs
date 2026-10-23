// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"slices"
	"strings"
	"syscall"
	"testing"
	"time"
)

// TestPlatformOf checks what the adapter does on each OS: the Unix binary
// and work dir, and on Windows the srs.exe of the Windows CMake build, a work
// dir in the temp dir, and only the features that build has.
func TestPlatformOf(t *testing.T) {
	for _, goos := range []string{"linux", "darwin"} {
		p := platformOf(goos)
		if p.binary != "srs" || p.workRoot != "/tmp/srs-integ" {
			t.Errorf("%s: %+v, want srs and /tmp/srs-integ", goos, p)
		}
		if !slices.Equal(p.features, []string{"srt", "rtsp", "gb28181", "rtc", "simulator", "sanitizer"}) {
			t.Errorf("%s: features %v", goos, p.features)
		}
		if !strings.Contains(p.build, "./configure --srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on && make") {
			t.Errorf("%s: build %q, want the configure flags", goos, p.build)
		}
	}

	p := platformOf("windows")
	if p.binary != filepath.Join("windows", "srs.exe") {
		t.Errorf("windows: binary %q, want windows/srs.exe", p.binary)
	}
	if p.workRoot != filepath.Join(os.TempDir(), "srs-integ") {
		t.Errorf("windows: work root %q, want srs-integ in %s", p.workRoot, os.TempDir())
	}
	if !slices.Equal(p.features, []string{"srt", "rtsp", "gb28181", "rtc", "simulator"}) {
		t.Errorf("windows: features %v, want srt, rtsp, gb28181, rtc and simulator", p.features)
	}
	if !strings.Contains(p.build, "srs-windows-runner.sh build") {
		t.Errorf("windows: build %q, want the runner's build step", p.build)
	}
}

// TestBuildFeatures checks that the cases get the features of this OS's
// build, and a copy a case cannot change.
func TestBuildFeatures(t *testing.T) {
	got := BuildFeatures()
	if !slices.Equal(got, host.features) {
		t.Errorf("BuildFeatures %v, want %v", got, host.features)
	}
	got[0] = "changed"
	if host.features[0] == "changed" {
		t.Errorf("BuildFeatures gave the platform's own list")
	}
}

// TestPlatformBuiltListeners checks a server, Common too, has every listener
// on Unix and Windows, whose builds have every feature, and only those of the
// features a build has, as SRS refuses to start with a listener of a feature
// it is built without.
func TestPlatformBuiltListeners(t *testing.T) {
	all := []Listener{RTMP, HTTPAPI, HTTPServer, RTCUDP, RTCTCP, SRT, RTSP, Exporter, GB, HTTPSAPI, HTTPSServer, RTMPS}
	for _, goos := range []string{"linux", "darwin", "windows"} {
		if got := platformOf(goos).builtListeners(all); !slices.Equal(got, all) {
			t.Errorf("%s: %v, want %v", goos, got, all)
		}
	}
	want := []Listener{RTMP, HTTPAPI, HTTPServer, RTCUDP, RTCTCP, SRT, Exporter, HTTPSAPI, HTTPSServer, RTMPS}
	if got := (platform{features: []string{"srt", "rtc"}}).builtListeners(all); !slices.Equal(got, want) {
		t.Errorf("srt and rtc only: %v, want %v", got, want)
	}
	if !slices.Equal(CommonListeners, host.builtListeners(all)) {
		t.Errorf("CommonListeners %v, want those of this platform", CommonListeners)
	}
}

// TestPidAlive checks the stale-run check sees this process alive and a
// process that exited dead, on every OS.
func TestPidAlive(t *testing.T) {
	if !pidAlive(os.Getpid()) {
		t.Error("this process is not alive")
	}
	exited := exec.Command(os.Args[0], "-test.run=^$")
	if err := exited.Run(); err != nil {
		t.Fatal(err)
	}
	if pidAlive(exited.Process.Pid) {
		t.Errorf("pid %d that exited is alive", exited.Process.Pid)
	}
}

// TestHelperQuit is the server of TestStopQuits: it runs until it gets the
// quit Stop sends, SIGTERM or on Windows Ctrl+Break, and exits with 0, as SRS
// does.
func TestHelperQuit(t *testing.T) {
	if os.Getenv("SRS_INTEG_HELPER_QUIT") == "" {
		t.Skip("a helper process")
	}
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt, syscall.SIGTERM)
	select {
	case <-quit:
		os.Exit(0)
	case <-time.After(30 * time.Second):
		os.Exit(3)
	}
}

// TestStopQuits checks Stop asks a running server to quit, on every OS, and
// takes its exit code: the server quits at once with 0, and Stop does not
// end it. A server that had already exited is still reported by its code.
func TestStopQuits(t *testing.T) {
	cmd := exec.Command(os.Args[0], "-test.run=^TestHelperQuit$")
	cmd.Env = append(os.Environ(), "SRS_INTEG_HELPER_QUIT=1")
	newProcessGroup(cmd)
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	s := &Server{cmd: cmd, done: make(chan struct{})}
	go func() {
		cmd.Wait()
		s.code = cmd.ProcessState.ExitCode()
		s.signal = exitSignal(cmd.ProcessState)
		close(s.done)
	}()
	// The helper must reach signal.Notify before the quit.
	time.Sleep(500 * time.Millisecond)

	start := time.Now()
	if err := s.Stop(); err != nil {
		t.Errorf("stop a running server: %v", err)
	}
	if d := time.Since(start); d > 2*time.Second {
		t.Errorf("stop took %v, want at once", d)
	}
	if s.code != 0 {
		t.Errorf("exit code %d, want 0 from the quit", s.code)
	}

	done := make(chan struct{})
	close(done)
	if err := (&Server{done: done, code: 1}).Stop(); err == nil || !strings.Contains(err.Error(), "code 1") {
		t.Errorf("a server that exited 1: %v, want code 1", err)
	}
}
