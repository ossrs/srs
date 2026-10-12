// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package server is the adapter between the cases and the SRS binary. It is
// the only package that knows the binary, its env var names, its ports and its
// paths, so a rewritten server needs only a new adapter.
package server

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"
)

var (
	flagSRS   = flag.String("integ.srs", "", "the SRS binary; default trunk/objs/srs of this repository")
	flagTimer = flag.Bool("integ.timer", false, "allocate ports from the timer process's part of SRS_INTEG_PORTS")
)

const (
	// workRoot holds one directory per run, named by the pid of the run.
	workRoot = "/tmp/srs-integ"
	// defaultPorts is the suite's port range when SRS_INTEG_PORTS is not set.
	defaultPorts = "14000-14999"
	// buildFlags is how to build an SRS that has every feature the suite needs.
	buildFlags = "./configure --srt=on --gb28181=on --rtsp=on --apm=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on"

	readyTimeout = 10 * time.Second
	readyPoll    = 20 * time.Millisecond
	stopTimeout  = 5 * time.Second
	tailLines    = 30
)

// requiredFeatures are the build features of /api/v1/features the suite needs.
var requiredFeatures = []string{"srt", "rtsp", "gb28181", "rtc", "simulator", "sanitizer"}

// The state of this process's run: its port range, its directory and the
// servers it started.
var run struct {
	once      sync.Once
	err       error
	ports     PortRange
	alloc     *Allocator
	dir       string
	binary    string
	tls       TLSFiles
	started   atomic.Int64
	seq       atomic.Int64
	features  sync.Once
	featErr   error
	commonMu  sync.Mutex
	common    *Server
	commonErr error
}

func setup() error {
	run.once.Do(func() {
		run.err = func() error {
			spec := os.Getenv("SRS_INTEG_PORTS")
			if spec == "" {
				spec = defaultPorts
			}
			r, err := ParseRange(spec)
			if err != nil {
				return fmt.Errorf("SRS_INTEG_PORTS: %w", err)
			}
			run.ports = r.Split(*flagTimer)
			run.alloc = NewAllocator(run.ports, nil)

			root := moduleRoot()
			run.binary = *flagSRS
			if run.binary == "" {
				run.binary = filepath.Join(root, "..", "..", "trunk", "objs", "srs")
			}
			if run.binary, err = filepath.Abs(run.binary); err != nil {
				return err
			}
			if _, err := os.Stat(run.binary); err != nil {
				return fmt.Errorf("no SRS binary at %s: build it in trunk with %s && make, or set -integ.srs", run.binary, buildFlags)
			}
			run.tls = TLSFiles{
				Key:  filepath.Join(root, "testdata", "server.key"),
				Cert: filepath.Join(root, "testdata", "server.crt"),
			}

			cleanStaleRuns(workRoot, pidAlive)
			run.dir = filepath.Join(workRoot, strconv.Itoa(os.Getpid()))
			return os.MkdirAll(run.dir, 0o755)
		}()
	})
	return run.err
}

// moduleRoot returns the directory of the srs-integ module.
func moduleRoot() string {
	_, file, _, _ := runtime.Caller(0)
	return filepath.Dir(filepath.Dir(filepath.Dir(file)))
}

func pidAlive(pid int) bool {
	err := syscall.Kill(pid, 0)
	return err == nil || errors.Is(err, syscall.EPERM)
}

// ProcessRange returns the ports this process allocates from.
func ProcessRange() (PortRange, error) {
	if err := setup(); err != nil {
		return PortRange{}, err
	}
	return run.ports, nil
}

// Server is one running SRS process.
type Server struct {
	// Dir is the server's own directory: its working directory, pid file and log.
	Dir string
	// Ports maps each listener that is on to its port.
	Ports map[Listener]int

	cmd  *exec.Cmd
	tail *logTail
	done chan struct{}
	code int
}

var unsafeName = regexp.MustCompile(`[^A-Za-z0-9_.-]+`)

// Start starts an SRS process with cfg and waits until its API answers.
func Start(name string, cfg Config) (*Server, error) {
	if err := setup(); err != nil {
		return nil, err
	}

	dir := filepath.Join(run.dir, fmt.Sprintf("%s-%d", unsafeName.ReplaceAllString(name, "_"), run.seq.Add(1)))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, err
	}

	ports := map[Listener]int{}
	for _, l := range cfg.Listeners {
		if _, ok := ports[l]; ok {
			continue
		}
		port, err := run.alloc.Alloc()
		if err != nil {
			return nil, err
		}
		ports[l] = port
	}

	env, err := BuildEnv(cfg, ports, dir, run.tls)
	if err != nil {
		return nil, err
	}
	if err := os.WriteFile(filepath.Join(dir, "env.txt"), []byte(strings.Join(env, "\n")+"\n"), 0o644); err != nil {
		return nil, err
	}

	logFile, err := os.Create(filepath.Join(dir, "console.log"))
	if err != nil {
		return nil, err
	}

	s := &Server{Dir: dir, Ports: ports, tail: newLogTail(logFile, tailLines), done: make(chan struct{})}
	s.cmd = exec.Command(run.binary, "-e")
	s.cmd.Dir = dir
	s.cmd.Env = append(processEnv(), env...)
	s.cmd.Stdout = s.tail
	s.cmd.Stderr = s.tail
	if err := s.cmd.Start(); err != nil {
		logFile.Close()
		return nil, err
	}
	run.started.Add(1)

	go func() {
		s.cmd.Wait()
		s.code = s.cmd.ProcessState.ExitCode()
		logFile.Close()
		close(s.done)
	}()

	if err := s.waitReady(); err != nil {
		s.kill()
		return nil, fmt.Errorf("start SRS in %s: %w\n%s", dir, err, strings.Join(s.LogTail(), "\n"))
	}

	run.features.Do(func() {
		run.featErr = s.checkFeatures()
	})
	if run.featErr != nil {
		s.kill()
		return nil, run.featErr
	}
	return s, nil
}

// processEnv is the env of this process without its SRS_* vars, so only the
// adapter configures the server.
func processEnv() []string {
	var out []string
	for _, kv := range os.Environ() {
		if !strings.HasPrefix(kv, "SRS_") {
			out = append(out, kv)
		}
	}
	// The macOS sanitizer warns about the nano zone without it.
	return append(out, "MallocNanoZone=0")
}

func (s *Server) waitReady() error {
	client := &http.Client{Timeout: time.Second}
	deadline := time.Now().Add(readyTimeout)
	for {
		res, err := client.Get(s.APIURL("/api/v1/versions"))
		if err == nil {
			io.Copy(io.Discard, res.Body)
			res.Body.Close()
			if res.StatusCode == http.StatusOK {
				return nil
			}
		}

		select {
		case <-s.done:
			return fmt.Errorf("exited with code %d before it was ready", s.code)
		case <-time.After(readyPoll):
		}
		if time.Now().After(deadline) {
			return fmt.Errorf("API not ready in %v: %v", readyTimeout, err)
		}
	}
}

func (s *Server) checkFeatures() error {
	res, err := http.Get(s.APIURL("/api/v1/features"))
	if err != nil {
		return err
	}
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	if err != nil {
		return err
	}
	missing, err := missingFeatures(body)
	if err != nil {
		return fmt.Errorf("read the build features of %s: %w", run.binary, err)
	}
	if len(missing) > 0 {
		return fmt.Errorf("SRS %s is built without %s: rebuild it in trunk with %s && make",
			run.binary, strings.Join(missing, ", "), buildFlags)
	}
	return nil
}

// Addr returns the 127.0.0.1:port address of a listener that is on.
func (s *Server) Addr(l Listener) string {
	return "127.0.0.1:" + strconv.Itoa(s.Ports[l])
}

// APIURL returns the URL of an HTTP API path, such as /api/v1/versions.
func (s *Server) APIURL(path string) string {
	return "http://" + s.Addr(HTTPAPI) + path
}

// Features returns the boolean features of /api/v1/features.
func (s *Server) Features() (map[string]bool, error) {
	res, err := http.Get(s.APIURL("/api/v1/features"))
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()

	var v struct {
		Code int `json:"code"`
		Data struct {
			Features map[string]any `json:"features"`
		} `json:"data"`
	}
	if err := json.NewDecoder(res.Body).Decode(&v); err != nil {
		return nil, err
	}
	if v.Code != 0 {
		return nil, fmt.Errorf("features code %d", v.Code)
	}
	out := map[string]bool{}
	for k, val := range v.Data.Features {
		if b, ok := val.(bool); ok {
			out[k] = b
		}
	}
	return out, nil
}

// Signal sends a signal to the process.
func (s *Server) Signal(sig os.Signal) error {
	select {
	case <-s.done:
		return fmt.Errorf("SRS already exited with code %d", s.code)
	default:
	}
	return s.cmd.Process.Signal(sig)
}

// Wait waits up to timeout for the process to exit and returns its exit code,
// -1 when a signal killed it.
func (s *Server) Wait(timeout time.Duration) (int, error) {
	select {
	case <-s.done:
		return s.code, nil
	case <-time.After(timeout):
		return 0, fmt.Errorf("SRS did not exit in %v", timeout)
	}
}

// Stop stops the process with SIGTERM, or SIGKILL when it does not exit in
// time. It fails when the process had to be killed or did not exit with 0.
func (s *Server) Stop() error {
	select {
	case <-s.done:
	default:
		s.cmd.Process.Signal(syscall.SIGTERM)
		if _, err := s.Wait(stopTimeout); err != nil {
			s.kill()
			return fmt.Errorf("SRS did not exit in %v after SIGTERM, killed it", stopTimeout)
		}
	}
	if s.code != 0 {
		return fmt.Errorf("SRS exited with code %d", s.code)
	}
	return nil
}

func (s *Server) kill() {
	s.cmd.Process.Kill()
	<-s.done
}

// LogTail returns the last lines of the server's console log.
func (s *Server) LogTail() []string {
	return s.tail.Lines()
}

func (s *Server) logTo(t testing.TB) {
	t.Logf("SRS log in %s, last lines:\n%s", s.Dir, strings.Join(s.LogTail(), "\n"))
}

// CommonConfig is the config of the Common server: every Common listener, the
// lowest play latency, so a player gets each message at once rather than in
// batches of 350ms of media, which would put every streaming case near its
// limit, HTTP-FLV for every stream, and HLS and DASH, both with 10s segments
// (HLS by default; DASH defaults to 30s, the length of a whole test file). A
// case that tests the default batching needs a variant.
func CommonConfig() Config {
	return Config{
		Listeners: CommonListeners,
		Env: map[string]string{
			"SRS_VHOST_MIN_LATENCY":        "on",
			"SRS_VHOST_PLAY_MW_LATENCY":    "0",
			"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
			"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream].flv",
			"SRS_VHOST_HLS_ENABLED":        "on",
			"SRS_VHOST_DASH_ENABLED":       "on",
			"SRS_VHOST_DASH_DASH_FRAGMENT": "10",
		},
	}
}

// Common returns the Common server, shared by every top-level test of the
// process and started by the first case that asks for it. Main stops it.
func Common(t testing.TB) *Server {
	t.Helper()
	var s *Server
	var err error
	untimed(t, func() {
		run.commonMu.Lock()
		defer run.commonMu.Unlock()
		if run.common == nil && run.commonErr == nil {
			run.common, run.commonErr = Start("common", CommonConfig())
		}
		s, err = run.common, run.commonErr
	})

	if err != nil {
		t.Fatalf("Common server: %v", err)
	}
	t.Cleanup(func() {
		if t.Failed() {
			s.logTo(t)
		}
	})
	return s
}

// Group starts a server for a variant group, and stops it when the group ends.
// Its directory is removed when the group passes, and kept with its log when it
// fails.
func Group(t testing.TB, cfg Config) *Server {
	t.Helper()
	var s *Server
	var err error
	untimed(t, func() { s, err = Start(t.Name(), cfg) })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := s.Stop(); err != nil {
			t.Error(err)
		}
		if t.Failed() {
			s.logTo(t)
			return
		}
		os.RemoveAll(s.Dir)
	})
	return s
}

// Main runs the tests of a TestMain, then stops the Common server, prints the
// summary of the cases for this process (main or timer), and removes the run's
// directory when every test passed. It returns the exit code.
func Main(m *testing.M, summary func(label string) string) int {
	code := m.Run()

	if s := run.common; s != nil {
		if err := s.Stop(); err != nil {
			fmt.Fprintf(os.Stderr, "srs-integ: stop the Common server: %v\n%s\n", err, strings.Join(s.LogTail(), "\n"))
			code = 1
		}
	}

	label := "main"
	if *flagTimer {
		label = "timer"
	}
	fmt.Print(summary(label))

	if run.dir != "" {
		fmt.Printf("srs-integ: %d SRS processes, %d ports from %v\n", run.started.Load(), run.alloc.Allocated(), run.ports)
		if code == 0 {
			os.RemoveAll(run.dir)
		} else {
			fmt.Printf("srs-integ: logs kept in %s\n", run.dir)
		}
	}
	return code
}

// missingFeatures returns the build features that a /api/v1/features body
// lacks, from the ones the suite needs.
func missingFeatures(body []byte) ([]string, error) {
	var v struct {
		Code int `json:"code"`
		Data struct {
			Features map[string]any `json:"features"`
		} `json:"data"`
	}
	if err := json.Unmarshal(body, &v); err != nil {
		return nil, err
	}
	if v.Code != 0 {
		return nil, fmt.Errorf("code %d", v.Code)
	}

	var missing []string
	for _, name := range requiredFeatures {
		if on, _ := v.Data.Features[name].(bool); !on {
			missing = append(missing, name)
		}
	}
	return missing, nil
}

// cleanStaleRuns removes the run directories under root whose process is no
// longer alive. It only touches entries named by a pid.
func cleanStaleRuns(root string, alive func(pid int) bool) {
	entries, err := os.ReadDir(root)
	if err != nil {
		return
	}
	for _, e := range entries {
		pid, err := strconv.Atoi(e.Name())
		if err != nil || pid <= 0 || strconv.Itoa(pid) != e.Name() || !e.IsDir() {
			continue
		}
		if pid == os.Getpid() || alive(pid) {
			continue
		}
		os.RemoveAll(filepath.Join(root, e.Name()))
	}
}

// untimed runs a server start, and leaves its time out of the case's time
// when t is a case that measures its time.
func untimed(t testing.TB, start func()) {
	if c, ok := t.(interface{ Untimed(func()) }); ok {
		c.Untimed(start)
		return
	}
	start()
}

// RemuxConfig is the config of a variant that serves every stream over HTTP
// as ext, "ts", "mp3" or "aac", at /[app]/[stream].<ext>, because a vhost has
// one HTTP remux mount and Common's is FLV. It has RTMP to publish, the HTTP
// API and server, and Common's lowest play latency.
func RemuxConfig(ext string) Config {
	return Config{
		Listeners: []Listener{RTMP, HTTPAPI, HTTPServer},
		Env: map[string]string{
			"SRS_VHOST_MIN_LATENCY":        "on",
			"SRS_VHOST_PLAY_MW_LATENCY":    "0",
			"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
			"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream]." + ext,
		},
	}
}
