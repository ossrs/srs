// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// An FFmpeg is one ffmpeg binary and what it can encode.
type FFmpeg struct {
	Path string
	// Version is the first line of ffmpeg -version.
	Version  string
	Encoders map[string]bool
}

// Has tells whether the ffmpeg has an encoder.
func (ff *FFmpeg) Has(encoder string) bool {
	return ff != nil && ff.Encoders[encoder]
}

// Usable tells whether the ffmpeg can make the base media: libx264, libx265
// and an Opus encoder. It returns what it lacks otherwise.
func (ff *FFmpeg) Usable() (missing []string) {
	for _, e := range []string{"libx264", "libx265"} {
		if !ff.Has(e) {
			missing = append(missing, e)
		}
	}
	if !ff.Has("libopus") && !ff.Has("opus") {
		missing = append(missing, "libopus or opus")
	}
	return missing
}

// Complete tells whether the ffmpeg can make every recipe on macOS, where the
// setup script builds one with every encoder.
func (ff *FFmpeg) Complete() bool {
	return len(lacking(ff)) == 0
}

// ProbeFFmpeg runs the ffmpeg at path to read its version and encoders.
func ProbeFFmpeg(path string) (*FFmpeg, error) {
	out, err := exec.Command(path, "-hide_banner", "-version").Output()
	if err != nil {
		return nil, fmt.Errorf("run %v -version: %w", path, err)
	}
	version, _, _ := strings.Cut(string(out), "\n")

	out, err = exec.Command(path, "-hide_banner", "-encoders").Output()
	if err != nil {
		return nil, fmt.Errorf("run %v -encoders: %w", path, err)
	}
	return &FFmpeg{Path: path, Version: strings.TrimSpace(version), Encoders: parseEncoders(string(out))}, nil
}

// parseEncoders reads the encoder names of ffmpeg -encoders: each line after
// the "------" separator is " <flags> <name> <description>".
func parseEncoders(out string) map[string]bool {
	encoders := map[string]bool{}
	_, list, ok := strings.Cut(out, "------\n")
	if !ok {
		return encoders
	}
	for _, line := range strings.Split(list, "\n") {
		if f := strings.Fields(line); len(f) >= 2 {
			encoders[f[1]] = true
		}
	}
	return encoders
}

// A Finder looks for ffmpeg the way the suite does: $FFMPEG, then PATH,
// then ~/.local/bin. Its fields are the outside world, so tests can fake it.
type Finder struct {
	Getenv   func(string) string
	LookPath func(string) (string, error)
	Home     func() (string, error)
	Exists   func(string) bool
	Probe    func(string) (*FFmpeg, error)
}

// DefaultFinder looks on this machine.
func DefaultFinder() Finder {
	return Finder{
		Getenv:   os.Getenv,
		LookPath: exec.LookPath,
		Home:     os.UserHomeDir,
		Exists: func(p string) bool {
			st, err := os.Stat(p)
			return err == nil && !st.IsDir()
		},
		Probe: ProbeFFmpeg,
	}
}

// Find returns the first ffmpeg that is usable (see Usable). It fails with
// what each candidate lacks when none is.
func (f Finder) Find() (*FFmpeg, error) {
	var candidates []string
	if p := f.Getenv("FFMPEG"); p != "" {
		candidates = append(candidates, p)
	}
	if p, err := f.LookPath("ffmpeg"); err == nil {
		candidates = append(candidates, p)
	}
	if home, err := f.Home(); err == nil {
		if p := filepath.Join(home, ".local", "bin", "ffmpeg"); f.Exists(p) {
			candidates = append(candidates, p)
		}
	}

	var reasons []string
	seen := map[string]bool{}
	for _, p := range candidates {
		if seen[p] {
			continue
		}
		seen[p] = true
		ff, err := f.Probe(p)
		if err != nil {
			reasons = append(reasons, err.Error())
			continue
		}
		if missing := ff.Usable(); len(missing) > 0 {
			reasons = append(reasons, fmt.Sprintf("%v lacks %v", p, strings.Join(missing, ", ")))
			continue
		}
		return ff, nil
	}
	if len(reasons) == 0 {
		return nil, errors.New("no ffmpeg found in $FFMPEG, PATH or ~/.local/bin")
	}
	return nil, fmt.Errorf("no usable ffmpeg: %v", strings.Join(reasons, "; "))
}

// FindFFmpeg is Find on this machine; cases use it for their interop runs.
func FindFFmpeg() (*FFmpeg, error) {
	return DefaultFinder().Find()
}

// A Setup builds an ffmpeg with every encoder on macOS, by the script of the
// srs-develop skill, but only when no other test may be using the binary it
// replaces.
type Setup struct {
	// Script is setup-ffmpeg-with-whip.sh; empty means never build.
	Script string
	GOOS   string
	// Busy tells why the shared ffmpeg is in use, or "" when it is idle.
	Busy func() string
	// Run runs the script.
	Run func(script string) error
	// Wait is the time between two idle checks, and Patience how long to try.
	Wait, Patience time.Duration
	Sleep          func(time.Duration)
	Log            io.Writer
}

// DefaultSetup runs script on this machine.
func DefaultSetup(script string, log io.Writer) Setup {
	return Setup{
		Script:   script,
		GOOS:     runtime.GOOS,
		Busy:     func() string { return busy(script) },
		Run:      runScript(log),
		Wait:     10 * time.Second,
		Patience: 30 * time.Minute,
		Sleep:    time.Sleep,
		Log:      log,
	}
}

// busy checks the Full-tier lock beside the setup script and any running
// ffmpeg process.
func busy(script string) string {
	lock := filepath.Join(filepath.Dir(script), "full-tier-lock.sh")
	out, _ := exec.Command("bash", lock, "status").Output()
	if status := strings.TrimSpace(string(out)); status != "free" {
		return fmt.Sprintf("the Full-tier lock is not free: %q", status)
	}
	if err := exec.Command("pgrep", "-x", "ffmpeg").Run(); err == nil {
		return "an ffmpeg process is running"
	}
	return ""
}

func runScript(log io.Writer) func(string) error {
	return func(script string) error {
		cmd := exec.Command("bash", script)
		cmd.Stdout, cmd.Stderr = log, log
		return cmd.Run()
	}
}

// Ensure finds an ffmpeg, and on macOS builds one with every encoder first
// when none is found or the one found lacks one.
func (s Setup) Ensure(f Finder) (*FFmpeg, error) {
	ff, err := f.Find()
	if s.GOOS != "darwin" || s.Script == "" || (err == nil && ff.Complete()) {
		return ff, err
	}

	why := "no usable ffmpeg"
	if err == nil {
		why = fmt.Sprintf("%v lacks %v", ff.Path, strings.Join(lacking(ff), ", "))
	}
	fmt.Fprintf(s.Log, "%v; building one with %v\n", why, s.Script)

	for waited := time.Duration(0); ; waited += s.Wait {
		reason := s.Busy()
		if reason == "" {
			break
		}
		if waited >= s.Patience {
			return nil, fmt.Errorf("the shared ffmpeg stayed busy for %v (%v); run make integ-prepare again later", s.Patience, reason)
		}
		fmt.Fprintf(s.Log, "waiting %v: %v\n", s.Wait, reason)
		s.Sleep(s.Wait)
	}

	if err := s.Run(s.Script); err != nil {
		return nil, fmt.Errorf("run %v: %w", s.Script, err)
	}
	return f.Find()
}

// lacking returns the encoders of Complete the ffmpeg lacks.
func lacking(ff *FFmpeg) []string {
	missing := ff.Usable()
	for _, e := range []string{"libvpx-vp9", "libsvtav1"} {
		if !ff.Has(e) {
			missing = append(missing, e)
		}
	}
	return missing
}

// HasProtocol tells whether the ffmpeg can both read and write a protocol,
// such as srt, from ffmpeg -protocols.
func (ff *FFmpeg) HasProtocol(name string) bool {
	if ff == nil {
		return false
	}
	out, err := exec.Command(ff.Path, "-hide_banner", "-protocols").Output()
	if err != nil {
		return false
	}
	return parseProtocols(string(out))[name]
}

// parseProtocols reads the protocols of ffmpeg -protocols that are in both
// its Input: and Output: lists.
func parseProtocols(out string) map[string]bool {
	in, outs := map[string]bool{}, map[string]bool{}
	var list map[string]bool
	for _, line := range strings.Split(out, "\n") {
		switch name := strings.TrimSpace(line); {
		case name == "Input:":
			list = in
		case name == "Output:":
			list = outs
		case list != nil && name != "" && strings.HasPrefix(line, " "):
			list[name] = true
		}
	}
	both := map[string]bool{}
	for name := range in {
		if outs[name] {
			both[name] = true
		}
	}
	return both
}
