// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package server is the adapter between the cases and the SRS binary. It is
// the only package that knows the binary, its env var names, its ports and its
// paths, so a rewritten server needs only a new adapter.
package server

import (
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"slices"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"srs-integ/internal/mock"
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
	procs     procCount
	seq       atomic.Int64
	features  sync.Once
	featErr   error
	commonMu  sync.Mutex
	common    []*Server
	commonErr []error
	shared    sharedSet
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
	// Hooks is the mock every hook calls, nil when the server has no hooks.
	Hooks *mock.Server

	cmd  *exec.Cmd
	tail *logTail
	done chan struct{}
	code int
	// start is when the process started, and cpu and wall what it used once
	// it exited.
	start     time.Time
	cpu, wall time.Duration
	burst     *slots
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

	// The hook mock gets a port of its own; it starts listening just before SRS.
	hooksAddr := ""
	if cfg.Hooks {
		port, err := run.alloc.Alloc()
		if err != nil {
			return nil, err
		}
		hooksAddr = "127.0.0.1:" + strconv.Itoa(port)
		// A down hook calls a port of the range that is never listened on
		// nor handed out again.
		down := ""
		if cfg.downHooks() {
			if port, err = run.alloc.Alloc(); err != nil {
				return nil, err
			}
			down = "http://127.0.0.1:" + strconv.Itoa(port)
		}
		if cfg, err = withHookURLs(cfg, "http://"+hooksAddr, down); err != nil {
			return nil, err
		}
	}

	// SRS fails to start on [::] where the host has no IPv6.
	cfg.IPv6 = cfg.IPv6 && HasIPv6()

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

	s := &Server{Dir: dir, Ports: ports, tail: newLogTail(logFile, tailLines), done: make(chan struct{}), burst: newSlots(burstSlots)}
	s.cmd = exec.Command(run.binary, "-e")
	s.cmd.Dir = dir
	s.cmd.Env = append(processEnv(), env...)
	s.cmd.Stdout = s.tail
	s.cmd.Stderr = s.tail
	if hooksAddr != "" {
		if s.Hooks, err = mock.Start(hooksAddr); err != nil {
			logFile.Close()
			return nil, err
		}
	}
	if err := s.cmd.Start(); err != nil {
		logFile.Close()
		if s.Hooks != nil {
			s.Hooks.Close()
		}
		return nil, err
	}
	s.start = time.Now()
	run.procs.started()

	// The mock answers hooks until SRS exits, such as its last on_close.
	go func() {
		s.cmd.Wait()
		s.code = s.cmd.ProcessState.ExitCode()
		s.cpu = s.cmd.ProcessState.UserTime() + s.cmd.ProcessState.SystemTime()
		s.wall = time.Since(s.start)
		run.procs.exited()
		logFile.Close()
		if s.Hooks != nil {
			s.Hooks.Close()
		}
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

// CommonConfig is the config of the Common server: every Common listener, with
// RTMP on IPv6 too, the lowest play latency, so a player gets each message at
// once rather than in
// batches of 350ms of media, which would put every streaming case near its
// limit, HTTP-FLV for every stream, and HLS and DASH, both with 10s segments
// (HLS by default; DASH defaults to 30s, the length of a whole test file), and
// every hook on one mock, which accepts unless a case scripts a reply for its
// own stream, and WebRTC over UDP and TCP with 127.0.0.1 as its candidate,
// so a session doesn't depend on the host's interfaces; the host of a WHIP or
// WHEP request is a candidate too, and SRT with its streams converted to RTMP
// and no receive latency (see srtEnv), and RTSP for every RTMP stream. A case
// that tests the default batching needs a variant.
func CommonConfig() Config {
	cfg := Config{
		Listeners: CommonListeners,
		Hooks:     true,
		IPv6:      true,
		Env: map[string]string{
			"SRS_VHOST_MIN_LATENCY":        "on",
			"SRS_VHOST_PLAY_MW_LATENCY":    "0",
			"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
			"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream].flv",
			"SRS_VHOST_HLS_ENABLED":        "on",
			"SRS_VHOST_DASH_ENABLED":       "on",
			"SRS_VHOST_DASH_DASH_FRAGMENT": "10",
			"SRS_VHOST_RTC_ENABLED":        "on",
			"SRS_RTC_SERVER_CANDIDATE":     "127.0.0.1",
			// A WHIP or WHEP request's host that is a name is a candidate as
			// it is, never resolved, so no case depends on DNS.
			"SRS_RTC_SERVER_KEEP_API_DOMAIN":    "on",
			"SRS_RTC_SERVER_RESOLVE_API_DOMAIN": "off",
			// RTSP plays every RTMP stream, by rtmp_to_rtsp, on by default.
			"SRS_VHOST_RTSP_ENABLED": "on",
		},
	}
	for k, v := range srtEnv {
		cfg.Env[k] = v
	}
	return cfg
}

// Common returns the Common server, shared by every top-level test of the
// process and started by the first case that asks for it. Main stops it. With
// -integ.shards=K there are K identical Common servers, and a case gets the
// one its name picks.
func Common(t testing.TB) *Server {
	t.Helper()
	var s *Server
	var err error
	untimed(t, func() {
		run.commonMu.Lock()
		defer run.commonMu.Unlock()
		if run.common == nil {
			run.common, run.commonErr = make([]*Server, shards), make([]error, shards)
		}
		i := shardOf(t.Name(), shards)
		if run.common[i] == nil && run.commonErr[i] == nil {
			name := "common"
			if shards > 1 {
				name = fmt.Sprintf("common-%d", i+1)
			}
			run.common[i], run.commonErr[i] = Start(name, CommonConfig())
		}
		s, err = run.common[i], run.commonErr[i]
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

// Main runs the tests of a TestMain, then stops the Common and shared
// servers, prints the summary of the cases for this process (main or timer),
// and removes the run's directory when every test passed. It returns the exit
// code.
func Main(m *testing.M, summary func(label string) string) int {
	code := m.Run()

	// The servers stop at once, since each takes about a second to quit.
	shared := run.shared.servers()
	stopErrs := make([]error, len(run.common))
	sharedErrs := make([]error, len(shared))
	var wg sync.WaitGroup
	for i, s := range run.common {
		if s != nil {
			wg.Add(1)
			go func() { defer wg.Done(); stopErrs[i] = s.Stop() }()
		}
	}
	for i, s := range shared {
		wg.Add(1)
		go func() { defer wg.Done(); sharedErrs[i] = s.Stop() }()
	}
	wg.Wait()
	for i, s := range shared {
		if err := sharedErrs[i]; err != nil {
			fmt.Fprintf(os.Stderr, "srs-integ: stop the shared server in %s: %v\n%s\n", s.Dir, err, strings.Join(s.LogTail(), "\n"))
			code = 1
		}
	}

	var cpu []string
	for i, s := range run.common {
		if s == nil {
			continue
		}
		name := "Common"
		if len(run.common) > 1 {
			name = fmt.Sprintf("Common shard %d of %d", i+1, len(run.common))
		}
		if err := stopErrs[i]; err != nil {
			fmt.Fprintf(os.Stderr, "srs-integ: stop the %s server: %v\n%s\n", name, err, strings.Join(s.LogTail(), "\n"))
			code = 1
		}
		cpu = append(cpu, cpuLine(name, s.cpu, s.wall))
	}

	label := "main"
	if *flagTimer {
		label = "timer"
	}
	fmt.Print(summary(label))

	if run.dir != "" {
		fmt.Printf("srs-integ: %d SRS processes, at most %d at once, %d ports from %v\n",
			run.procs.total.Load(), run.procs.peak.Load(), run.alloc.Allocated(), run.ports)
		for _, line := range cpu {
			fmt.Println(line)
		}
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

// Remux is a remux variant's settings of its vhost and HTTP server; the zero
// value keeps SRS's defaults.
type Remux struct {
	// Off turns the HTTP remux off, so the server serves no stream over HTTP.
	Off bool
	// GuessOff stops SRS guessing the FLV header's audio and video flags from
	// the first messages a player gets; it uses has_audio and has_video.
	GuessOff bool
	// NoAudio and NoVideo turn has_audio and has_video off.
	NoAudio, NoVideo bool
	// KeepUnmatched turns drop_if_not_match off, so SRS sends the tags of a
	// track the FLV header doesn't have.
	KeepUnmatched bool
	// FastCache is the audio cache of the HTTP audio streams.
	FastCache time.Duration
	// CrossdomainOff turns off the CORS headers of the HTTP server.
	CrossdomainOff bool
}

// RemuxVariant is RemuxConfig(ext) with the remux settings r.
func RemuxVariant(ext string, r Remux) Config {
	cfg := RemuxConfig(ext)
	env := cfg.Env
	if r.Off {
		env["SRS_VHOST_HTTP_REMUX_ENABLED"] = "off"
		delete(env, "SRS_VHOST_HTTP_REMUX_MOUNT")
	}
	off := map[string]bool{
		"SRS_VHOST_HTTP_REMUX_GUESS_HAS_AV":      r.GuessOff,
		"SRS_VHOST_HTTP_REMUX_HAS_AUDIO":         r.NoAudio,
		"SRS_VHOST_HTTP_REMUX_HAS_VIDEO":         r.NoVideo,
		"SRS_VHOST_HTTP_REMUX_DROP_IF_NOT_MATCH": r.KeepUnmatched,
		"SRS_HTTP_SERVER_CROSSDOMAIN":            r.CrossdomainOff,
	}
	for k, v := range off {
		if v {
			env[k] = "off"
		}
	}
	if r.FastCache > 0 {
		env["SRS_VHOST_HTTP_REMUX_FAST_CACHE"] = strconv.FormatFloat(r.FastCache.Seconds(), 'f', -1, 64)
	}
	return cfg
}

var hasIPv6 = sync.OnceValue(func() bool {
	ln, err := net.Listen("tcp6", "[::1]:0")
	if err != nil {
		return false
	}
	ln.Close()
	return true
})

// HasIPv6 reports whether the host can listen on the IPv6 loopback, ::1.
func HasIPv6() bool {
	return hasIPv6()
}

// Play is a play variant's settings of its vhost; the zero value keeps SRS's
// defaults.
type Play struct {
	// GopCacheOff turns the GOP cache off.
	GopCacheOff bool
	// GopCacheMaxFrames limits the messages the GOP cache holds.
	GopCacheMaxFrames int
	// QueueLength is the most media a player's queue holds, in whole seconds.
	QueueLength time.Duration
	// ReduceSequenceHeader drops a sequence header equal to the one before.
	ReduceSequenceHeader bool
	// TimeJitter is the timestamp correction of the players, "full", "zero" or
	// "off"; empty keeps SRS's default, full.
	TimeJitter string
	// ATC plays the publisher's absolute timestamps, and ATCAuto turns ATC on
	// for a stream whose metadata has bravo_atc "true".
	ATC, ATCAuto bool
	// MixCorrect sends audio and video in decode-time order.
	MixCorrect bool
	// SendMinInterval is the least time between two messages SRS sends a
	// player.
	SendMinInterval time.Duration
	// MergedWrite, when not nil, replaces Common's lowest latency with
	// min_latency off and the merged write and read it sets.
	MergedWrite *Merged
}

// Merged is how SRS merges the messages it writes to a player and reads from
// a publisher, with min_latency off.
type Merged struct {
	// WriteLatency and WriteMsgs are mw_latency and mw_msgs: SRS holds a
	// player's messages until it has more than WriteMsgs that span at least
	// WriteLatency.
	WriteLatency time.Duration
	WriteMsgs    int
	// ReadLatency turns merged read on: SRS waits that long after a small
	// read from a publisher.
	ReadLatency time.Duration
}

// PlayConfig is the config of a variant with the play settings p, RTMP to
// publish and play, the HTTP API, and Common's lowest play latency unless
// p.MergedWrite replaces it.
func PlayConfig(p Play) Config {
	env := map[string]string{
		"SRS_VHOST_MIN_LATENCY":     "on",
		"SRS_VHOST_PLAY_MW_LATENCY": "0",
	}
	if p.GopCacheOff {
		env["SRS_VHOST_PLAY_GOP_CACHE"] = "off"
	}
	if p.GopCacheMaxFrames > 0 {
		env["SRS_VHOST_PLAY_GOP_CACHE_MAX_FRAMES"] = strconv.Itoa(p.GopCacheMaxFrames)
	}
	if p.QueueLength > 0 {
		env["SRS_VHOST_PLAY_QUEUE_LENGTH"] = strconv.Itoa(int(p.QueueLength / time.Second))
	}
	if p.ReduceSequenceHeader {
		env["SRS_VHOST_PLAY_REDUCE_SEQUENCE_HEADER"] = "on"
	}
	if p.TimeJitter != "" {
		env["SRS_VHOST_PLAY_TIME_JITTER"] = p.TimeJitter
	}
	if p.ATC {
		env["SRS_VHOST_PLAY_ATC"] = "on"
	}
	if p.ATCAuto {
		env["SRS_VHOST_PLAY_ATC_AUTO"] = "on"
	}
	if p.MixCorrect {
		env["SRS_VHOST_PLAY_MIX_CORRECT"] = "on"
	}
	if p.SendMinInterval > 0 {
		env["SRS_VHOST_PLAY_SEND_MIN_INTERVAL"] = strconv.FormatInt(p.SendMinInterval.Milliseconds(), 10)
	}
	if m := p.MergedWrite; m != nil {
		env["SRS_VHOST_MIN_LATENCY"] = "off"
		env["SRS_VHOST_PLAY_MW_LATENCY"] = strconv.FormatInt(m.WriteLatency.Milliseconds(), 10)
		env["SRS_VHOST_PLAY_MW_MSGS"] = strconv.Itoa(m.WriteMsgs)
		if m.ReadLatency > 0 {
			env["SRS_VHOST_PUBLISH_MR"] = "on"
			env["SRS_VHOST_PUBLISH_MR_LATENCY"] = strconv.FormatInt(m.ReadLatency.Milliseconds(), 10)
		}
	}
	return Config{Listeners: []Listener{RTMP, HTTPAPI}, Env: env}
}

// Publish is a publish variant's settings of its vhost; the zero value keeps
// SRS's defaults.
type Publish struct {
	// FirstPacketTimeout is how long SRS waits for a publisher's first
	// message, in milliseconds.
	FirstPacketTimeout time.Duration
	// NormalTimeout is how long SRS waits for a publisher's later messages,
	// in milliseconds. SRS checks a publisher on this period, so it also sets
	// how soon KickoffForIdle acts.
	NormalTimeout time.Duration
	// KickoffForIdle closes a publisher that has had no player that long,
	// in seconds with a fraction.
	KickoffForIdle time.Duration
}

// PublishConfig is the config of a variant with the publish settings p, RTMP
// to publish and play, the HTTP API, and Common's lowest play latency.
func PublishConfig(p Publish) Config {
	env := map[string]string{
		"SRS_VHOST_MIN_LATENCY":     "on",
		"SRS_VHOST_PLAY_MW_LATENCY": "0",
	}
	if p.FirstPacketTimeout > 0 {
		env["SRS_VHOST_PUBLISH_FIRSTPKT_TIMEOUT"] = strconv.FormatInt(p.FirstPacketTimeout.Milliseconds(), 10)
	}
	if p.NormalTimeout > 0 {
		env["SRS_VHOST_PUBLISH_NORMAL_TIMEOUT"] = strconv.FormatInt(p.NormalTimeout.Milliseconds(), 10)
	}
	if p.KickoffForIdle > 0 {
		env["SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE"] = strconv.FormatFloat(p.KickoffForIdle.Seconds(), 'f', -1, 64)
	}
	return Config{Listeners: []Listener{RTMP, HTTPAPI}, Env: env}
}

// ClientTLS returns a TLS client config that trusts the test certificate of
// the servers' TLS listeners, for RTMPS and HTTPS clients.
func ClientTLS() (*tls.Config, error) {
	cert := filepath.Join(moduleRoot(), "testdata", "server.crt")
	pem, err := os.ReadFile(cert)
	if err != nil {
		return nil, err
	}
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(pem) {
		return nil, fmt.Errorf("no certificate in %v", cert)
	}
	return &tls.Config{RootCAs: pool}, nil
}

// HLS is an HLS variant's settings of its vhost; the zero value keeps SRS's
// defaults.
type HLS struct {
	// Fragment and Window are hls_fragment and hls_window, in seconds with a
	// fraction.
	Fragment, Window time.Duration
	// TDRatio and AOFRatio are hls_td_ratio, the segment length over
	// Fragment, and hls_aof_ratio, the same for a stream with no video.
	TDRatio, AOFRatio float64
	// Path is hls_path, where SRS writes the files; the HTTP server serves
	// the same dir.
	Path string
	// M3U8File and TSFile are the templates of the playlist and segment
	// files under Path.
	M3U8File, TSFile string
	// EntryPrefix is hls_entry_prefix, put before each segment URI.
	EntryPrefix string
	// TSFloor names segments by the wall time over Fragment, [timestamp].
	TSFloor bool
	// FMP4 is hls_use_fmp4: fragmented MP4 segments with an init file,
	// instead of TS.
	FMP4 bool
	// FMP4File and InitFile are the templates of the fmp4 segment and init
	// files under Path.
	FMP4File, InitFile string
	// Keys is hls_keys: segments encrypted, AES-128 for TS and SAMPLE-AES
	// for fmp4, with a new key every FragmentsPerKey segments.
	Keys            bool
	FragmentsPerKey int
	// KeyURL is hls_key_url, the base of the key URIs in the playlist; its
	// query, if any, goes after each key file.
	KeyURL string
	// NoCtx turns hls_ctx off: no master playlist and no session per player.
	// NoTSCtx turns hls_ts_ctx off: a session's segment URIs get no hls_ctx.
	NoCtx, NoTSCtx bool
	// MasterRelative is hls_master_m3u8_path_relative: the master playlist
	// links its media playlist by a relative path.
	MasterRelative bool
	// Dispose is hls_dispose, in whole seconds: SRS removes the files of a
	// stream that has sent no media that long. NoCleanup turns hls_cleanup
	// off: segments that leave the window stay on disk.
	Dispose   time.Duration
	NoCleanup bool
}

// HLSConfig is the config of a variant with HLS on and the HLS settings h,
// RTMP to publish, the HTTP API and server, and Common's lowest play latency.
func HLSConfig(h HLS) Config {
	return WithHLS(Config{
		Listeners: []Listener{RTMP, HTTPAPI, HTTPServer},
		Env: map[string]string{
			"SRS_VHOST_MIN_LATENCY":     "on",
			"SRS_VHOST_PLAY_MW_LATENCY": "0",
		},
	}, h)
}

// WithHLS returns a copy of cfg with HLS on and the HLS settings h, so a
// variant of another feature can serve HLS too. It turns the HTTP server on
// to serve the files.
func WithHLS(cfg Config, h HLS) Config {
	env := map[string]string{}
	for k, v := range cfg.Env {
		env[k] = v
	}
	env["SRS_VHOST_HLS_ENABLED"] = "on"
	seconds := func(d time.Duration) string { return strconv.FormatFloat(d.Seconds(), 'f', -1, 64) }
	ratio := func(f float64) string { return strconv.FormatFloat(f, 'f', -1, 64) }
	if h.Fragment > 0 {
		env["SRS_VHOST_HLS_HLS_FRAGMENT"] = seconds(h.Fragment)
	}
	if h.Window > 0 {
		env["SRS_VHOST_HLS_HLS_WINDOW"] = seconds(h.Window)
	}
	if h.TDRatio > 0 {
		env["SRS_VHOST_HLS_HLS_TD_RATIO"] = ratio(h.TDRatio)
	}
	if h.AOFRatio > 0 {
		env["SRS_VHOST_HLS_HLS_AOF_RATIO"] = ratio(h.AOFRatio)
	}
	if h.Path != "" {
		env["SRS_VHOST_HLS_HLS_PATH"] = h.Path
		env["SRS_HTTP_SERVER_DIR"] = h.Path
	}
	if h.M3U8File != "" {
		env["SRS_VHOST_HLS_HLS_M3U8_FILE"] = h.M3U8File
	}
	if h.TSFile != "" {
		env["SRS_VHOST_HLS_HLS_TS_FILE"] = h.TSFile
	}
	if h.EntryPrefix != "" {
		env["SRS_VHOST_HLS_HLS_ENTRY_PREFIX"] = h.EntryPrefix
	}
	if h.TSFloor {
		env["SRS_VHOST_HLS_HLS_TS_FLOOR"] = "on"
	}
	if h.FMP4 {
		env["SRS_VHOST_HLS_HLS_USE_FMP4"] = "on"
	}
	if h.FMP4File != "" {
		env["SRS_VHOST_HLS_HLS_FMP4_FILE"] = h.FMP4File
	}
	if h.InitFile != "" {
		env["SRS_VHOST_HLS_HLS_INIT_FILE"] = h.InitFile
	}
	if h.Keys {
		env["SRS_VHOST_HLS_HLS_KEYS"] = "on"
	}
	if h.FragmentsPerKey > 0 {
		env["SRS_VHOST_HLS_HLS_FRAGMENTS_PER_KEY"] = strconv.Itoa(h.FragmentsPerKey)
	}
	if h.KeyURL != "" {
		env["SRS_VHOST_HLS_HLS_KEY_URL"] = h.KeyURL
	}
	if h.NoCtx {
		env["SRS_VHOST_HLS_HLS_CTX"] = "off"
	}
	if h.NoTSCtx {
		env["SRS_VHOST_HLS_HLS_TS_CTX"] = "off"
	}
	if h.MasterRelative {
		env["SRS_VHOST_HLS_HLS_MASTER_M3U8_PATH_RELATIVE"] = "on"
	}
	if h.Dispose > 0 {
		env["SRS_VHOST_HLS_HLS_DISPOSE"] = strconv.Itoa(int(h.Dispose / time.Second))
	}
	if h.NoCleanup {
		env["SRS_VHOST_HLS_HLS_CLEANUP"] = "off"
	}
	cfg.Env = env
	if !slices.Contains(cfg.Listeners, HTTPServer) {
		cfg.Listeners = append(slices.Clone(cfg.Listeners), HTTPServer)
	}
	return cfg
}

// DASH is a DASH variant's settings of its vhost; the zero value keeps SRS's
// defaults.
type DASH struct {
	// Fragment is dash_fragment, in seconds with a fraction.
	Fragment time.Duration
	// WindowSize is dash_window_size, the number of segments in the MPD.
	WindowSize int
	// Path is dash_path, where SRS writes the files. The HTTP server keeps
	// its dir, so Path must be that dir or under it for the files to be
	// served.
	Path string
	// MPDFile is dash_mpd_file, the template of the MPD under Path.
	MPDFile string
	// Dispose is dash_dispose, in whole seconds, and NoCleanup turns
	// dash_cleanup off, like the HLS settings.
	Dispose   time.Duration
	NoCleanup bool
}

// WithDASH returns a copy of cfg with DASH on and the DASH settings d, so a
// variant of another feature can serve DASH too. cfg must have the HTTP
// server on.
func WithDASH(cfg Config, d DASH) Config {
	env := map[string]string{}
	for k, v := range cfg.Env {
		env[k] = v
	}
	env["SRS_VHOST_DASH_ENABLED"] = "on"
	if d.Fragment > 0 {
		env["SRS_VHOST_DASH_DASH_FRAGMENT"] = strconv.FormatFloat(d.Fragment.Seconds(), 'f', -1, 64)
	}
	if d.WindowSize > 0 {
		env["SRS_VHOST_DASH_DASH_WINDOW_SIZE"] = strconv.Itoa(d.WindowSize)
	}
	if d.Path != "" {
		env["SRS_VHOST_DASH_DASH_PATH"] = d.Path
	}
	if d.MPDFile != "" {
		env["SRS_VHOST_DASH_DASH_MPD_FILE"] = d.MPDFile
	}
	if d.Dispose > 0 {
		env["SRS_VHOST_DASH_DASH_DISPOSE"] = strconv.Itoa(int(d.Dispose / time.Second))
	}
	if d.NoCleanup {
		env["SRS_VHOST_DASH_DASH_CLEANUP"] = "off"
	}
	cfg.Env = env
	return cfg
}

// RTC is a WebRTC variant's settings; the zero value keeps SRS's defaults.
type RTC struct {
	// DTLSRole is the vhost's dtls_role, active or passive: the role SRS
	// takes when an offer's setup is actpass.
	DTLSRole string
	// StunStrictCheck is the vhost's stun_strict_check.
	StunStrictCheck bool
	// NoAPICandidates turns api_as_candidates off.
	NoAPICandidates bool
	// NackPreferRTX is the vhost's nack_prefer_rtx: SRS sends a packet a
	// player asks for again on the RTX stream, when the player offers RTX.
	NackPreferRTX bool
	// NoNack turns the vhost's nack off: SRS asks no publisher for a lost
	// packet, and sends no player one again.
	NoNack bool
	// NoTWCC turns the vhost's twcc off: SRS sends publishers no
	// transport-wide congestion control feedback.
	NoTWCC bool
	// RTMPToRTC turns the vhost's rtmp_to_rtc on: SRS converts each RTMP
	// stream to WebRTC, with its AAC transcoded to Opus, so WHEP plays it.
	RTMPToRTC bool
	// RTCToRTMP turns the vhost's rtc_to_rtmp on: SRS converts each WebRTC
	// stream to RTMP, with its Opus transcoded to AAC, so the RTMP, HTTP and
	// segment players play it.
	RTCToRTMP bool
}

// WithRTC returns a copy of cfg with WebRTC on, over UDP only, with
// 127.0.0.1 as its candidate like Common, and the WebRTC settings r, so a
// variant of another feature can take WebRTC sessions too.
func WithRTC(cfg Config, r RTC) Config {
	env := map[string]string{}
	for k, v := range cfg.Env {
		env[k] = v
	}
	env["SRS_VHOST_RTC_ENABLED"] = "on"
	env["SRS_RTC_SERVER_CANDIDATE"] = "127.0.0.1"
	if r.DTLSRole != "" {
		env["SRS_VHOST_RTC_DTLS_ROLE"] = r.DTLSRole
	}
	if r.StunStrictCheck {
		env["SRS_VHOST_RTC_STUN_STRICT_CHECK"] = "on"
	}
	if r.NoAPICandidates {
		env["SRS_RTC_SERVER_API_AS_CANDIDATES"] = "off"
	}
	if r.NackPreferRTX {
		env["SRS_VHOST_RTC_NACK_PREFER_RTX"] = "on"
	}
	if r.NoNack {
		env["SRS_VHOST_RTC_NACK"] = "off"
	}
	if r.NoTWCC {
		env["SRS_VHOST_RTC_TWCC"] = "off"
	}
	if r.RTMPToRTC {
		env["SRS_VHOST_RTC_RTMP_TO_RTC"] = "on"
	}
	if r.RTCToRTMP {
		env["SRS_VHOST_RTC_RTC_TO_RTMP"] = "on"
	}
	cfg.Env = env
	if !slices.Contains(cfg.Listeners, RTCUDP) {
		cfg.Listeners = append(slices.Clone(cfg.Listeners), RTCUDP)
	}
	return cfg
}

// SRTOptions are an SRT variant's server settings; the zero value keeps
// SRS's defaults.
type SRTOptions struct {
	// DefaultApp is the app of a short streamid that names only a stream.
	DefaultApp string
	// DefaultMode is what a short streamid, without #!::, does: publish or
	// request.
	DefaultMode string
	// DefaultStreamID is the streamid of a caller that sends none.
	DefaultStreamID string
	// Passphrase is the secret every caller must have, and PBKeyLen its key
	// length.
	Passphrase string
	PBKeyLen   int
	// RecvLatency is recvlatency, SRS's receive latency, 0 like Common's;
	// KeepRecvLatency leaves it at SRS's default instead.
	RecvLatency     time.Duration
	KeepRecvLatency bool
	// Latency and PeerLatency set latency and peerlatency, in whole
	// milliseconds, when not zero.
	Latency, PeerLatency time.Duration
	// PeerIdleTimeout sets peer_idle_timeout, in whole milliseconds, when
	// not zero.
	PeerIdleTimeout time.Duration
}

// WithSRT returns a copy of cfg with SRT on, like Common: the listener, SRT
// and its conversion to RTMP on the vhost, and no receive latency; and with
// the settings s.
func WithSRT(cfg Config, s SRTOptions) Config {
	env := map[string]string{}
	for k, v := range cfg.Env {
		env[k] = v
	}
	for k, v := range srtEnv {
		env[k] = v
	}
	for k, v := range map[string]string{
		"SRS_SRT_SERVER_DEFAULT_APP":      s.DefaultApp,
		"SRS_SRT_SERVER_DEFAULT_MODE":     s.DefaultMode,
		"SRS_SRT_SERVER_DEFAULT_STREAMID": s.DefaultStreamID,
		"SRS_SRT_SERVER_PASSPHRASE":       s.Passphrase,
	} {
		if v != "" {
			env[k] = v
		}
	}
	if s.PBKeyLen != 0 {
		env["SRS_SRT_SERVER_PBKEYLEN"] = strconv.Itoa(s.PBKeyLen)
	}
	ms := func(d time.Duration) string { return strconv.FormatInt(d.Milliseconds(), 10) }
	if s.KeepRecvLatency {
		delete(env, "SRS_SRT_SERVER_RECVLATENCY")
	} else if s.RecvLatency != 0 {
		env["SRS_SRT_SERVER_RECVLATENCY"] = ms(s.RecvLatency)
	}
	for k, d := range map[string]time.Duration{
		"SRS_SRT_SERVER_LATENCY":           s.Latency,
		"SRS_SRT_SERVER_PEERLATENCY":       s.PeerLatency,
		"SRS_SRT_SERVER_PEER_IDLE_TIMEOUT": s.PeerIdleTimeout,
	} {
		if d != 0 {
			env[k] = ms(d)
		}
	}
	cfg.Env = env
	if !slices.Contains(cfg.Listeners, SRT) {
		cfg.Listeners = append(slices.Clone(cfg.Listeners), SRT)
	}
	return cfg
}

// srtEnv turns SRT on for the vhost, with its streams converted to RTMP, so
// they play over every protocol, and with no receive latency: SRS delivers
// what a publisher sends at once, not 120ms later, which is SRT's default.
// recvlatency, not latency, sets it: SRS applies latency first, then
// recvlatency, whose default 120 overrides it.
var srtEnv = map[string]string{
	"SRS_VHOST_SRT_ENABLED":      "on",
	"SRS_VHOST_SRT_TO_RTMP":      "on",
	"SRS_SRT_SERVER_RECVLATENCY": "0",
}

// RTSPOptions are an RTSP variant's settings; the zero value is RTSP on, like
// Common.
type RTSPOptions struct {
	// NoRTMPToRTSP turns rtmp_to_rtsp off: RTMP streams are not played over
	// RTSP.
	NoRTMPToRTSP bool
	// VhostOff leaves RTSP off on the vhost, with the listener on.
	VhostOff bool
}

// WithRTSP returns a copy of cfg with the RTSP listener and the settings r.
func WithRTSP(cfg Config, r RTSPOptions) Config {
	env := map[string]string{}
	for k, v := range cfg.Env {
		env[k] = v
	}
	env["SRS_VHOST_RTSP_ENABLED"] = "on"
	if r.VhostOff {
		env["SRS_VHOST_RTSP_ENABLED"] = "off"
	}
	if r.NoRTMPToRTSP {
		env["SRS_VHOST_RTSP_RTMP_TO_RTSP"] = "off"
	}
	cfg.Env = env
	if !slices.Contains(cfg.Listeners, RTSP) {
		cfg.Listeners = append(slices.Clone(cfg.Listeners), RTSP)
	}
	return cfg
}

// GBOptions are a GB28181 variant's settings; the zero value keeps SRS's
// defaults.
type GBOptions struct {
	// MediaConnectTimeout is media_connect_timeout, in seconds with a
	// fraction: how long a session from the publish API waits for its media.
	MediaConnectTimeout time.Duration
}

// WithGB returns a copy of cfg with the GB28181 caster, whose streams go to
// the server's own RTMP, and the settings g.
func WithGB(cfg Config, g GBOptions) Config {
	env := map[string]string{}
	for k, v := range cfg.Env {
		env[k] = v
	}
	if g.MediaConnectTimeout > 0 {
		env["SRS_STREAM_CASTER_MEDIA_CONNECT_TIMEOUT"] = strconv.FormatFloat(g.MediaConnectTimeout.Seconds(), 'f', -1, 64)
	}
	cfg.Env = env
	if !slices.Contains(cfg.Listeners, GB) {
		cfg.Listeners = append(slices.Clone(cfg.Listeners), GB)
	}
	return cfg
}
