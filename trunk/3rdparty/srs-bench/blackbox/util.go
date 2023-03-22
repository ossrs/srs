// The MIT License (MIT)
//
// # Copyright (c) 2023 Winlin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
package blackbox

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	ohttp "github.com/ossrs/go-oryx-lib/http"
	"github.com/ossrs/go-oryx-lib/logger"
	"io/ioutil"
	"math/rand"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

var srsLog *bool
var srsStdout *bool
var srsFFmpegStderr *bool
var srsDVRStderr *bool
var srsFFprobeStdout *bool

var srsTimeout *int
var srsFFprobeDuration *int
var srsFFprobeTimeout *int
var srsFFprobeHEVCTimeout *int

var srsBinary *string
var srsFFmpeg *string
var srsFFprobe *string

var srsPublishAvatar *string

func prepareTest() (err error) {
	srsLog = flag.Bool("srs-log", false, "Whether enable the detail log")
	srsStdout = flag.Bool("srs-stdout", false, "Whether enable the SRS stdout log")
	srsFFmpegStderr = flag.Bool("srs-ffmpeg-stderr", false, "Whether enable the FFmpeg stderr log")
	srsDVRStderr = flag.Bool("srs-dvr-stderr", false, "Whether enable the DVR stderr log")
	srsFFprobeStdout = flag.Bool("srs-ffprobe-stdout", false, "Whether enable the FFprobe stdout log")
	srsTimeout = flag.Int("srs-timeout", 64000, "For each case, the timeout in ms")
	srsFFprobeDuration = flag.Int("srs-ffprobe-duration", 16000, "For each case, the duration for ffprobe in ms")
	srsFFprobeTimeout = flag.Int("srs-ffprobe-timeout", 21000, "For each case, the timeout for ffprobe in ms")
	srsBinary = flag.String("srs-binary", "../../objs/srs", "The binary to start SRS server")
	srsFFmpeg = flag.String("srs-ffmpeg", "ffmpeg", "The FFmpeg tool")
	srsFFprobe = flag.String("srs-ffprobe", "ffprobe", "The FFprobe tool")
	srsPublishAvatar = flag.String("srs-publish-avatar", "avatar.flv", "The avatar file for publisher.")
	srsFFprobeHEVCTimeout = flag.Int("srs-ffprobe-hevc-timeout", 30000, "For each case, the timeout for ffprobe in ms")

	// Parse user options.
	flag.Parse()

	// Try to locate file.
	tryOpenFile := func(filename string) (string, error) {
		// Match if file exists.
		if _, err := os.Stat(filename); err == nil {
			return filename, nil
		}

		// If we run in GoLand, the current directory is in blackbox, so we use parent directory.
		nFilename := path.Join("../", filename)
		if _, err := os.Stat(nFilename); err == nil {
			return nFilename, nil
		}

		// Try to find file by which if it's a command like ffmpeg.
		cmd := exec.Command("which", filename)
		cmd.Env = []string{"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"}
		if v, err := cmd.Output(); err == nil {
			return strings.TrimSpace(string(v)), nil
		}

		return filename, errors.Errorf("file %v not found", filename)
	}

	// Check and relocate path of tools.
	if *srsBinary, err = tryOpenFile(*srsBinary); err != nil {
		return err
	}
	if *srsFFmpeg, err = tryOpenFile(*srsFFmpeg); err != nil {
		return err
	}
	if *srsFFprobe, err = tryOpenFile(*srsFFprobe); err != nil {
		return err
	}
	if *srsPublishAvatar, err = tryOpenFile(*srsPublishAvatar); err != nil {
		return err
	}

	return nil
}

// Filter the test error, ignore context.Canceled
func filterTestError(errs ...error) error {
	var filteredErrors []error

	for _, err := range errs {
		if err == nil || errors.Cause(err) == context.Canceled {
			continue
		}

		// If url error, server maybe error, do not print the detail log.
		if r0 := errors.Cause(err); r0 != nil {
			if r1, ok := r0.(*url.Error); ok {
				err = r1
			}
		}

		filteredErrors = append(filteredErrors, err)
	}

	if len(filteredErrors) == 0 {
		return nil
	}
	if len(filteredErrors) == 1 {
		return filteredErrors[0]
	}

	var descs []string
	for i, err := range filteredErrors[1:] {
		descs = append(descs, fmt.Sprintf("err #%d, %+v", i, err))
	}
	return errors.Wrapf(filteredErrors[0], "with %v", strings.Join(descs, ","))
}

// The SRSPortAllocator is SRS port manager.
type SRSPortAllocator struct {
	ports sync.Map
}

func NewSRSPortAllocator() *SRSPortAllocator {
	return &SRSPortAllocator{}
}

func (v *SRSPortAllocator) Allocate() int {
	for i := 0; i < 1024; i++ {
		port := 10000 + rand.Int()%50000
		if _, ok := v.ports.LoadOrStore(port, true); !ok {
			return port
		}

		time.Sleep(time.Duration(rand.Int()%1000) * time.Microsecond)
	}

	panic("Allocate port failed")
}

func (v *SRSPortAllocator) Free(port int) {
	v.ports.Delete(port)
}

var allocator *SRSPortAllocator

func init() {
	allocator = NewSRSPortAllocator()
}

type backendService struct {
	// The context for case.
	caseCtx       context.Context
	caseCtxCancel context.CancelFunc

	// When SRS process started.
	readyCtx       context.Context
	readyCtxCancel context.CancelFunc

	// Whether already closed.
	closedCtx       context.Context
	closedCtxCancel context.CancelFunc

	// All goroutines
	wg sync.WaitGroup

	// The name, args and env for cmd.
	name string
	args []string
	env  []string
	// If timeout, kill the process.
	duration time.Duration

	// The process stdout and stderr.
	stdout bytes.Buffer
	stderr bytes.Buffer
	// The process error.
	r0 error
	// The process pid.
	pid int
	// Whether ignore process exit status error.
	ignoreExitStatusError bool

	// Hooks for owner.
	// Before start the process.
	onBeforeStart func(ctx context.Context, bs *backendService, cmd *exec.Cmd) error
	// After started the process.
	onAfterStart func(ctx context.Context, bs *backendService, cmd *exec.Cmd) error
	// Before kill the process, when case is done.
	onBeforeKill func(ctx context.Context, bs *backendService, cmd *exec.Cmd) error
	// After stopped the process. Always callback when run is called.
	onStop func(ctx context.Context, bs *backendService, cmd *exec.Cmd, r0 error, stdout, stderr *bytes.Buffer) error
	// When dispose the process. Always callback when run is called.
	onDispose func(ctx context.Context, bs *backendService) error
}

func newBackendService(opts ...func(v *backendService)) *backendService {
	v := &backendService{}

	v.readyCtx, v.readyCtxCancel = context.WithCancel(context.Background())
	v.closedCtx, v.closedCtxCancel = context.WithCancel(context.Background())

	for _, opt := range opts {
		opt(v)
	}

	return v
}

func (v *backendService) Close() error {
	if v.closedCtx.Err() != nil {
		return v.r0
	}
	v.closedCtxCancel()

	if v.caseCtxCancel != nil {
		v.caseCtxCancel()
	}
	if v.readyCtxCancel != nil {
		v.readyCtxCancel()
	}

	v.wg.Wait()

	if v.onDispose != nil {
		v.onDispose(v.caseCtx, v)
	}

	logger.Tf(v.caseCtx, "Process is closed, pid=%v, r0=%v", v.pid, v.r0)
	return nil
}

func (v *backendService) ReadyCtx() context.Context {
	return v.readyCtx
}

func (v *backendService) Run(ctx context.Context, cancel context.CancelFunc) error {
	// Always dispose resource of process.
	defer v.Close()

	// Start SRS with -e, which only use environment variables.
	cmd := exec.Command(v.name, v.args...)

	// If not started, we also need to callback the onStop.
	var processStarted bool
	defer func() {
		if v.onStop != nil && !processStarted {
			v.onStop(ctx, v, cmd, v.r0, &v.stdout, &v.stderr)
		}
	}()

	// Ignore if already error.
	if ctx.Err() != nil {
		return ctx.Err()
	}

	// Save the context of case.
	v.caseCtx, v.caseCtxCancel = ctx, cancel

	// Setup stdout and stderr.
	cmd.Stdout = &v.stdout
	cmd.Stderr = &v.stderr
	cmd.Env = v.env
	if v.onBeforeStart != nil {
		if err := v.onBeforeStart(ctx, v, cmd); err != nil {
			return errors.Wrapf(err, "onBeforeStart failed")
		}
	}

	// Try to start the SRS server.
	if err := cmd.Start(); err != nil {
		return err
	}

	// Now process started, query the pid.
	v.pid = cmd.Process.Pid
	v.readyCtxCancel()
	processStarted = true
	if v.onAfterStart != nil {
		if err := v.onAfterStart(ctx, v, cmd); err != nil {
			return errors.Wrapf(err, "onAfterStart failed")
		}
	}

	// The context for SRS process.
	processDone, processDoneCancel := context.WithCancel(context.Background())

	// If exceed timeout, kill the process.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()
		if v.duration <= 0 {
			return
		}

		select {
		case <-ctx.Done():
		case <-time.After(v.duration):
			logger.Tf(ctx, "Process killed duration=%v, pid=%v, name=%v, args=%v", v.duration, v.pid, v.name, v.args)
			cmd.Process.Kill()
		}
	}()

	// If SRS process terminated, notify case to stop.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		// When SRS quit, also terminate the case.
		defer cancel()

		// Notify other goroutine, SRS already done.
		defer processDoneCancel()

		if err := cmd.Wait(); err != nil && !v.ignoreExitStatusError {
			v.r0 = errors.Wrapf(err, "Process wait err, pid=%v, name=%v, args=%v", v.pid, v.name, v.args)
		}
		if v.onStop != nil {
			if err := v.onStop(ctx, v, cmd, v.r0, &v.stdout, &v.stderr); err != nil {
				if v.r0 == nil {
					v.r0 = errors.Wrapf(err, "Process onStop err, pid=%v, name=%v, args=%v", v.pid, v.name, v.args)
				} else {
					logger.Ef(ctx, "Process onStop err %v", err)
				}
			}
		}
	}()

	// If case terminated, notify SRS process to stop.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		select {
		case <-ctx.Done():
			// Notify owner that we're going to kill the process.
			if v.onBeforeKill != nil {
				v.onBeforeKill(ctx, v, cmd)
			}

			// When case terminated, also terminate the SRS process.
			cmd.Process.Signal(syscall.SIGINT)
		case <-processDone.Done():
			// Ignore if already done.
			return
		}

		// Start a goroutine to ensure process killed.
		go func() {
			time.Sleep(3 * time.Second)
			if processDone.Err() == nil { // Ignore if already done.
				cmd.Process.Signal(syscall.SIGKILL)
			}
		}()
	}()

	// Wait for SRS or case done.
	select {
	case <-ctx.Done():
	case <-processDone.Done():
	}

	return v.r0
}

// ServiceRunner is an interface to run backend service.
type ServiceRunner interface {
	Run(ctx context.Context, cancel context.CancelFunc) error
}

// ServiceReadyQuerier is an interface to detect whether service is ready.
type ServiceReadyQuerier interface {
	ReadyCtx() context.Context
}

// SRSServer is the interface for SRS server.
type SRSServer interface {
	ServiceRunner
	ServiceReadyQuerier
	// WorkDir is the current working directory for SRS.
	WorkDir() string
	// RTMPPort is the RTMP stream port.
	RTMPPort() int
	// HTTPPort is the HTTP stream port.
	HTTPPort() int
	// APIPort is the HTTP API port.
	APIPort() int
	// SRTPort is the SRT UDP port.
	SRTPort() int
}

// srsServer is a SRS server instance.
type srsServer struct {
	// The backend service process.
	process *backendService

	// When SRS process started.
	readyCtx       context.Context
	readyCtxCancel context.CancelFunc

	// SRS server ID.
	srsID string
	// SRS workdir.
	workDir string
	// SRS PID file, relative to the workdir.
	srsRelativePidFile string
	// SRS server ID cache file, relative to the workdir.
	srsRelativeIDFile string

	// SRS RTMP server listen port.
	rtmpListen int
	// HTTP API listen port.
	apiListen int
	// HTTP server listen port.
	httpListen int
	// SRT UDP server listen port.
	srtListen int

	// The envs from user.
	envs []string
}

func NewSRSServer(opts ...func(v *srsServer)) SRSServer {
	rid := fmt.Sprintf("%v-%v", os.Getpid(), rand.Int())
	v := &srsServer{
		workDir: path.Join("objs", fmt.Sprintf("%v", rand.Int())),
		srsID:   fmt.Sprintf("srs-id-%v", rid),
		process: newBackendService(),
	}
	v.readyCtx, v.readyCtxCancel = context.WithCancel(context.Background())

	// If we run in GoLand, the current directory is in blackbox, so we use parent directory.
	if _, err := os.Stat("objs"); err != nil {
		v.workDir = path.Join("..", "objs", fmt.Sprintf("%v", rand.Int()))
	}

	// Do allocate resource.
	v.srsRelativePidFile = path.Join("objs", fmt.Sprintf("srs-%v.pid", rid))
	v.srsRelativeIDFile = path.Join("objs", fmt.Sprintf("srs-%v.id", rid))
	v.rtmpListen = allocator.Allocate()
	v.apiListen = allocator.Allocate()
	v.httpListen = allocator.Allocate()
	v.srtListen = allocator.Allocate()

	// Do cleanup.
	v.process.onDispose = func(ctx context.Context, bs *backendService) error {
		allocator.Free(v.rtmpListen)
		allocator.Free(v.apiListen)
		allocator.Free(v.httpListen)
		allocator.Free(v.srtListen)

		if _, err := os.Stat(v.workDir); err == nil {
			os.RemoveAll(v.workDir)
		}

		logger.Tf(ctx, "SRS server is closed, id=%v, pid=%v, cleanup=%v r0=%v",
			v.srsID, bs.pid, v.workDir, bs.r0)
		return nil
	}

	for _, opt := range opts {
		opt(v)
	}

	return v
}

func (v *srsServer) ReadyCtx() context.Context {
	return v.readyCtx
}

func (v *srsServer) RTMPPort() int {
	return v.rtmpListen
}

func (v *srsServer) HTTPPort() int {
	return v.httpListen
}

func (v *srsServer) APIPort() int {
	return v.apiListen
}

func (v *srsServer) SRTPort() int {
	return v.srtListen
}

func (v *srsServer) WorkDir() string {
	return v.workDir
}

func (v *srsServer) Run(ctx context.Context, cancel context.CancelFunc) error {
	logger.Tf(ctx, "Starting SRS server, dir=%v, binary=%v, id=%v, pid=%v, rtmp=%v",
		v.workDir, *srsBinary, v.srsID, v.srsRelativePidFile, v.rtmpListen,
	)

	// Create directories.
	if err := os.MkdirAll(path.Join(v.workDir, "./objs/nginx/html"), os.FileMode(0755)|os.ModeDir); err != nil {
		return errors.Wrapf(err, "SRS create directory %v", path.Join(v.workDir, "./objs/nginx/html"))
	}

	// Setup the name and args of process.
	v.process.name = *srsBinary
	v.process.args = []string{"-e"}

	// Setup the constant values.
	v.process.env = []string{
		// Run in frontend.
		"SRS_DAEMON=off",
		// Write logs to stdout and stderr.
		"SRS_SRS_LOG_FILE=console",
		// Disable warning for asan.
		"MallocNanoZone=0",
		// Avoid error for macOS, which ulimit to 256.
		"SRS_MAX_CONNECTIONS=100",
	}
	// For directories.
	v.process.env = append(v.process.env, []string{
		// SRS working directory.
		fmt.Sprintf("SRS_WORK_DIR=%v", v.workDir),
		// Setup the default directory for HTTP server.
		"SRS_HTTP_SERVER_DIR=./objs/nginx/html",
		// Setup the default directory for HLS stream.
		"SRS_VHOST_HLS_HLS_PATH=./objs/nginx/html",
		"SRS_VHOST_HLS_HLS_M3U8_FILE=[app]/[stream].m3u8",
		"SRS_VHOST_HLS_HLS_TS_FILE=[app]/[stream]-[seq].ts",
	}...)
	// For variables.
	v.process.env = append(v.process.env, []string{
		// SRS PID file.
		fmt.Sprintf("SRS_PID=%v", v.srsRelativePidFile),
		// SRS ID file.
		fmt.Sprintf("SRS_SERVER_ID=%v", v.srsID),
		// HTTP API to detect the service.
		fmt.Sprintf("SRS_HTTP_API_ENABLED=on"),
		fmt.Sprintf("SRS_HTTP_API_LISTEN=%v", v.apiListen),
		// Setup the RTMP listen port.
		fmt.Sprintf("SRS_LISTEN=%v", v.rtmpListen),
		// Setup the HTTP sever listen port.
		fmt.Sprintf("SRS_HTTP_SERVER_LISTEN=%v", v.httpListen),
		// Setup the SRT server listen port.
		fmt.Sprintf("SRS_SRT_SERVER_LISTEN=%v", v.srtListen),
	}...)
	// Rewrite envs by case.
	if v.envs != nil {
		v.process.env = append(v.process.env, v.envs...)
	}
	// Allow user to rewrite them.
	for _, env := range os.Environ() {
		if strings.HasPrefix(env, "SRS") || strings.HasPrefix(env, "PATH") {
			v.process.env = append(v.process.env, env)
		}
	}

	// Wait for all goroutine to done.
	var wg sync.WaitGroup
	defer wg.Wait()

	// Start a task to detect the HTTP API.
	wg.Add(1)
	go func() {
		defer wg.Done()
		for ctx.Err() == nil {
			time.Sleep(100 * time.Millisecond)

			r := fmt.Sprintf("http://localhost:%v/api/v1/versions", v.apiListen)
			res, err := http.Get(r)
			if err != nil {
				continue
			}
			defer res.Body.Close()

			b, err := ioutil.ReadAll(res.Body)
			if err != nil {
				continue
			}

			logger.Tf(ctx, "SRS API is ready, %v %v", r, string(b))
			v.readyCtxCancel()
			return
		}
	}()

	// Hooks for process.
	v.process.onBeforeStart = func(ctx context.Context, bs *backendService, cmd *exec.Cmd) error {
		logger.Tf(ctx, "SRS id=%v, env %v %v %v",
			v.srsID, strings.Join(cmd.Env, " "), bs.name, strings.Join(bs.args, " "))
		return nil
	}
	v.process.onAfterStart = func(ctx context.Context, bs *backendService, cmd *exec.Cmd) error {
		logger.Tf(ctx, "SRS id=%v, pid=%v", v.srsID, bs.pid)
		return nil
	}
	v.process.onStop = func(ctx context.Context, bs *backendService, cmd *exec.Cmd, r0 error, stdout, stderr *bytes.Buffer) error {
		// Should be ready when process stop.
		defer v.readyCtxCancel()

		logger.Tf(ctx, "SRS process pid=%v exit, r0=%v", bs.pid, r0)
		if *srsStdout == true {
			logger.Tf(ctx, "SRS process pid=%v, stdout is \n%v", bs.pid, stdout.String())
		}
		if stderr.Len() > 0 {
			logger.Tf(ctx, "SRS process pid=%v, stderr is \n%v", bs.pid, stderr.String())
		}
		return nil
	}

	// Run the process util quit.
	return v.process.Run(ctx, cancel)
}

type FFmpegClient interface {
	ServiceRunner
	ServiceReadyQuerier
}

type ffmpegClient struct {
	// The backend service process.
	process *backendService

	// FFmpeg cli args, without ffmpeg binary.
	args []string
	// Let the process quit, do not cancel the case.
	cancelCaseWhenQuit bool
	// When timeout, stop FFmpeg, sometimes the '-t' does not work.
	ffmpegDuration time.Duration
}

func NewFFmpeg(opts ...func(v *ffmpegClient)) FFmpegClient {
	v := &ffmpegClient{
		process:            newBackendService(),
		cancelCaseWhenQuit: true,
	}

	// Do cleanup.
	v.process.onDispose = func(ctx context.Context, bs *backendService) error {
		return nil
	}

	// We ignore any exit error, because FFmpeg might exit with error even publish ok.
	v.process.ignoreExitStatusError = true

	for _, opt := range opts {
		opt(v)
	}

	return v
}

func (v *ffmpegClient) ReadyCtx() context.Context {
	return v.process.ReadyCtx()
}

func (v *ffmpegClient) Run(ctx context.Context, cancel context.CancelFunc) error {
	logger.Tf(ctx, "Starting FFmpeg by %v", strings.Join(v.args, " "))

	v.process.name = *srsFFmpeg
	v.process.args = v.args
	v.process.env = os.Environ()
	v.process.duration = v.ffmpegDuration

	v.process.onStop = func(ctx context.Context, bs *backendService, cmd *exec.Cmd, r0 error, stdout, stderr *bytes.Buffer) error {
		logger.Tf(ctx, "FFmpeg process pid=%v exit, r0=%v, stdout=%v", bs.pid, r0, stdout.String())
		if *srsFFmpegStderr && stderr.Len() > 0 {
			logger.Tf(ctx, "FFmpeg process pid=%v, stderr is \n%v", bs.pid, stderr.String())
		}
		return nil
	}

	// We might not want to cancel the case, for example, when check DVR by session, we just let the FFmpeg process to
	// quit and we should check the callback and DVR file.
	ffCtx, ffCancel := context.WithCancel(ctx)
	go func() {
		select {
		case <-ctx.Done():
		case <-ffCtx.Done():
			if v.cancelCaseWhenQuit {
				cancel()
			}
		}
	}()

	return v.process.Run(ffCtx, ffCancel)
}

type FFprobeClient interface {
	ServiceRunner
	// ProbeDoneCtx indicates the probe is done.
	ProbeDoneCtx() context.Context
	// Result return the raw string and metadata.
	Result() (string, *ffprobeObject)
}

type ffprobeClient struct {
	// The DVR file for ffprobe. Stream should be DVR to file, then use ffprobe to detect it. If DVR by FFmpeg, we will
	// start a FFmpeg process to do the DVR, or the DVR should be done by other tools.
	dvrFile string
	// The timeout to wait for task to done.
	timeout time.Duration

	// Whether do DVR by FFmpeg, if using SRS DVR, please set to false.
	dvrByFFmpeg bool
	// The stream to DVR for probing. Ignore if not DVR by ffmpeg
	streamURL string
	// The duration of video file for DVR and probing.
	duration time.Duration

	// When probe stream metadata object.
	doneCtx    context.Context
	doneCancel context.CancelFunc
	// The metadata object.
	metadata *ffprobeObject
	// The raw string of ffprobe.
	rawString string
}

func NewFFprobe(opts ...func(v *ffprobeClient)) FFprobeClient {
	v := &ffprobeClient{
		metadata:    &ffprobeObject{},
		dvrByFFmpeg: true,
	}
	v.doneCtx, v.doneCancel = context.WithCancel(context.Background())

	for _, opt := range opts {
		opt(v)
	}

	return v
}

func (v *ffprobeClient) ProbeDoneCtx() context.Context {
	return v.doneCtx
}

func (v *ffprobeClient) Result() (string, *ffprobeObject) {
	return v.rawString, v.metadata
}

func (v *ffprobeClient) Run(ctxCase context.Context, cancelCase context.CancelFunc) error {
	if true {
		ctx, cancel := context.WithTimeout(ctxCase, v.timeout)
		defer cancel()

		logger.Tf(ctx, "Starting FFprobe for stream=%v, dvr=%v, duration=%v, timeout=%v",
			v.streamURL, v.dvrFile, v.duration, v.timeout)

		// Try to start a DVR process.
		for ctx.Err() == nil {
			// If not DVR by FFmpeg, we just wait the DVR file to be ready, and it should be done by SRS or other tools.
			if v.dvrByFFmpeg {
				// If error, just ignore and retry, because the stream might not be ready. For example, for HLS, the DVR process
				// might need to wait for a duration of segment, 10s as such.
				_ = v.doDVR(ctx)
			}

			// Check whether DVR file is ok.
			if fs, err := os.Stat(v.dvrFile); err == nil && fs.Size() > 1024 {
				logger.Tf(ctx, "DVR FFprobe file is ok, file=%v, size=%v", v.dvrFile, fs.Size())
				break
			}

			// If not DVR by FFmpeg, must be by other tools, only need to wait.
			if !v.dvrByFFmpeg {
				logger.Tf(ctx, "Waiting stream=%v to be DVR", v.streamURL)
			}

			// Wait for a while and retry. Use larger timeout for HLS.
			retryTimeout := 1 * time.Second
			if strings.Contains(v.streamURL, ".m3u8") || v.dvrFile == "" {
				retryTimeout = 3 * time.Second
			}

			select {
			case <-ctx.Done():
			case <-time.After(retryTimeout):
			}
		}
	}

	// Ignore if case terminated.
	if ctxCase.Err() != nil {
		return nil
	}

	// Start a probe process for the DVR file.
	return v.doProbe(ctxCase, cancelCase)
}

func (v *ffprobeClient) doDVR(ctx context.Context) error {
	ctx, cancel := context.WithCancel(ctx)

	if !v.dvrByFFmpeg {
		return nil
	}

	process := newBackendService()
	process.name = *srsFFmpeg
	process.args = []string{
		"-t", fmt.Sprintf("%v", int64(v.duration/time.Second)),
		"-i", v.streamURL, "-c", "copy", "-y", v.dvrFile,
	}
	process.env = os.Environ()

	process.onDispose = func(ctx context.Context, bs *backendService) error {
		return nil
	}
	process.onBeforeStart = func(ctx context.Context, bs *backendService, cmd *exec.Cmd) error {
		logger.Tf(ctx, "DVR start %v %v", bs.name, strings.Join(bs.args, " "))
		return nil
	}
	process.onStop = func(ctx context.Context, bs *backendService, cmd *exec.Cmd, r0 error, stdout, stderr *bytes.Buffer) error {
		logger.Tf(ctx, "DVR process pid=%v exit, r0=%v, stdout=%v", bs.pid, r0, stdout.String())
		if *srsDVRStderr && stderr.Len() > 0 {
			logger.Tf(ctx, "DVR process pid=%v, stderr is \n%v", bs.pid, stderr.String())
		}
		return nil
	}

	return process.Run(ctx, cancel)
}

func (v *ffprobeClient) doProbe(ctx context.Context, cancel context.CancelFunc) error {
	process := newBackendService()
	process.name = *srsFFprobe
	process.args = []string{
		"-show_error", "-show_private_data", "-v", "quiet", "-find_stream_info",
		"-analyzeduration", fmt.Sprintf("%v", int64(v.duration/time.Microsecond)),
		"-print_format", "json", "-show_format", "-show_streams", v.dvrFile,
	}
	process.env = os.Environ()

	process.onDispose = func(ctx context.Context, bs *backendService) error {
		if _, err := os.Stat(v.dvrFile); !os.IsNotExist(err) {
			os.Remove(v.dvrFile)
		}
		return nil
	}
	process.onBeforeStart = func(ctx context.Context, bs *backendService, cmd *exec.Cmd) error {
		logger.Tf(ctx, "FFprobe start %v %v", bs.name, strings.Join(bs.args, " "))
		return nil
	}
	process.onStop = func(ctx context.Context, bs *backendService, cmd *exec.Cmd, r0 error, stdout, stderr *bytes.Buffer) error {
		logger.Tf(ctx, "FFprobe process pid=%v exit, r0=%v, stderr=%v", bs.pid, r0, stderr.String())
		if *srsFFprobeStdout && stdout.Len() > 0 {
			logger.Tf(ctx, "FFprobe process pid=%v, stdout is \n%v", bs.pid, stdout.String())
		}

		str := stdout.String()
		v.rawString = str

		if err := json.Unmarshal([]byte(str), v.metadata); err != nil {
			return err
		}

		m := v.metadata
		logger.Tf(ctx, "FFprobe done pid=%v, %v", bs.pid, m.String())

		v.doneCancel()
		return nil
	}

	return process.Run(ctx, cancel)
}

/*
   "index": 0,
   "codec_name": "h264",
   "codec_long_name": "H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10",
   "profile": "High",
   "codec_type": "video",
   "codec_tag_string": "avc1",
   "codec_tag": "0x31637661",
   "width": 768,
   "height": 320,
   "coded_width": 768,
   "coded_height": 320,
   "closed_captions": 0,
   "film_grain": 0,
   "has_b_frames": 2,
   "sample_aspect_ratio": "1:1",
   "display_aspect_ratio": "12:5",
   "pix_fmt": "yuv420p",
   "level": 32,
   "chroma_location": "left",
   "field_order": "progressive",
   "refs": 1,
   "is_avc": "true",
   "nal_length_size": "4",
   "id": "0x1",
   "r_frame_rate": "25/1",
   "avg_frame_rate": "25/1",
   "time_base": "1/16000",
   "start_pts": 1280,
   "start_time": "0.080000",
   "duration_ts": 160000,
   "duration": "10.000000",
   "bit_rate": "196916",
   "bits_per_raw_sample": "8",
   "nb_frames": "250",
   "extradata_size": 41,
   "disposition": {
       "default": 1,
       "dub": 0,
       "original": 0,
       "comment": 0,
       "lyrics": 0,
       "karaoke": 0,
       "forced": 0,
       "hearing_impaired": 0,
       "visual_impaired": 0,
       "clean_effects": 0,
       "attached_pic": 0,
       "timed_thumbnails": 0,
       "captions": 0,
       "descriptions": 0,
       "metadata": 0,
       "dependent": 0,
       "still_image": 0
   },
   "tags": {
       "language": "und",
       "handler_name": "VideoHandler",
       "vendor_id": "[0][0][0][0]"
   }
*/
/*
   "index": 1,
   "codec_name": "aac",
   "codec_long_name": "AAC (Advanced Audio Coding)",
   "profile": "LC",
   "codec_type": "audio",
   "codec_tag_string": "mp4a",
   "codec_tag": "0x6134706d",
   "sample_fmt": "fltp",
   "sample_rate": "44100",
   "channels": 2,
   "channel_layout": "stereo",
   "bits_per_sample": 0,
   "id": "0x2",
   "r_frame_rate": "0/0",
   "avg_frame_rate": "0/0",
   "time_base": "1/44100",
   "start_pts": 132,
   "start_time": "0.002993",
   "duration_ts": 441314,
   "duration": "10.007120",
   "bit_rate": "29827",
   "nb_frames": "431",
   "extradata_size": 2,
   "disposition": {
       "default": 1,
       "dub": 0,
       "original": 0,
       "comment": 0,
       "lyrics": 0,
       "karaoke": 0,
       "forced": 0,
       "hearing_impaired": 0,
       "visual_impaired": 0,
       "clean_effects": 0,
       "attached_pic": 0,
       "timed_thumbnails": 0,
       "captions": 0,
       "descriptions": 0,
       "metadata": 0,
       "dependent": 0,
       "still_image": 0
   },
   "tags": {
       "language": "und",
       "handler_name": "SoundHandler",
       "vendor_id": "[0][0][0][0]"
   }
*/
type ffprobeObjectMedia struct {
	Index          int    `json:"index"`
	CodecName      string `json:"codec_name"`
	CodecType      string `json:"codec_type"`
	Timebase       string `json:"time_base"`
	Bitrate        string `json:"bit_rate"`
	Profile        string `json:"profile"`
	Duration       string `json:"duration"`
	CodecTagString string `json:"codec_tag_string"`

	// For video codec.
	Width        int    `json:"width"`
	Height       int    `json:"height"`
	CodedWidth   int    `json:"coded_width"`
	CodedHeight  int    `json:"coded_height"`
	RFramerate   string `json:"r_frame_rate"`
	AvgFramerate string `json:"avg_frame_rate"`
	PixFmt       string `json:"pix_fmt"`
	Level        int    `json:"level"`

	// For audio codec.
	Channels      int    `json:"channels"`
	ChannelLayout string `json:"channel_layout"`
	SampleFmt     string `json:"sample_fmt"`
	SampleRate    string `json:"sample_rate"`
}

func (v *ffprobeObjectMedia) String() string {
	sb := strings.Builder{}

	sb.WriteString(fmt.Sprintf("index=%v, codec=%v, type=%v, tb=%v, bitrate=%v, profile=%v, duration=%v",
		v.Index, v.CodecName, v.CodecType, v.Timebase, v.Bitrate, v.Profile, v.Duration))
	sb.WriteString(fmt.Sprintf(", codects=%v", v.CodecTagString))

	if v.CodecType == "video" {
		sb.WriteString(fmt.Sprintf(", size=%vx%v, csize=%vx%v, rfr=%v, afr=%v, pix=%v, level=%v",
			v.Width, v.Height, v.CodedWidth, v.CodedHeight, v.RFramerate, v.AvgFramerate, v.PixFmt, v.Level))
	} else if v.CodecType == "audio" {
		sb.WriteString(fmt.Sprintf(", channels=%v, layout=%v, fmt=%v, srate=%v",
			v.Channels, v.ChannelLayout, v.SampleFmt, v.SampleRate))
	}

	return sb.String()
}

/*
"filename": "../objs/srs-ffprobe-stream-84487-8369019999559815097.mp4",
"nb_streams": 2,
"nb_programs": 0,
"format_name": "mov,mp4,m4a,3gp,3g2,mj2",
"format_long_name": "QuickTime / MOV",
"start_time": "0.002993",
"duration": "10.080000",
"size": "292725",
"bit_rate": "232321",
"probe_score": 100,

	"tags": {
	    "major_brand": "isom",
	    "minor_version": "512",
	    "compatible_brands": "isomiso2avc1mp41",
	    "encoder": "Lavf59.27.100"
	}
*/
type ffprobeObjectFormat struct {
	Filename   string `json:"filename"`
	Duration   string `json:"duration"`
	NBStream   int16  `json:"nb_streams"`
	Size       string `json:"size"`
	Bitrate    string `json:"bit_rate"`
	ProbeScore int    `json:"probe_score"`
}

func (v *ffprobeObjectFormat) String() string {
	return fmt.Sprintf("file=%v, duration=%v, score=%v, size=%v, bitrate=%v, streams=%v",
		v.Filename, v.Duration, v.ProbeScore, v.Size, v.Bitrate, v.NBStream)
}

/*
	{
	    "streams": [{ffprobeObjectMedia}, {ffprobeObjectMedia}],
	    "format": {ffprobeObjectFormat}
	}
*/
type ffprobeObject struct {
	Format  ffprobeObjectFormat  `json:"format"`
	Streams []ffprobeObjectMedia `json:"streams"`
}

func (v *ffprobeObject) String() string {
	sb := strings.Builder{}
	sb.WriteString(v.Format.String())
	sb.WriteString(", [")
	for _, stream := range v.Streams {
		sb.WriteString("{")
		sb.WriteString(stream.String())
		sb.WriteString("}")
	}
	sb.WriteString("]")
	return sb.String()
}

func (v *ffprobeObject) Duration() time.Duration {
	dv, err := strconv.ParseFloat(v.Format.Duration, 10)
	if err != nil {
		return time.Duration(0)
	}

	return time.Duration(dv*1000) * time.Millisecond
}

func (v *ffprobeObject) Video() *ffprobeObjectMedia {
	for _, media := range v.Streams {
		if media.CodecType == "video" {
			return &media
		}
	}
	return nil
}

func (v *ffprobeObject) Audio() *ffprobeObjectMedia {
	for _, media := range v.Streams {
		if media.CodecType == "audio" {
			return &media
		}
	}
	return nil
}

type HooksEvent interface {
	HookAction() string
}

type HooksEventBase struct {
	Action string `json:"action"`
}

func (v *HooksEventBase) HookAction() string {
	return v.Action
}

type HooksEventOnDvr struct {
	HooksEventBase
	Stream    string `json:"stream"`
	StreamUrl string `json:"stream_url"`
	StreamID  string `json:"stream_id"`
	CWD       string `json:"cwd"`
	File      string `json:"file"`
	TcUrl     string `json:"tcUrl"`
	App       string `json:"app"`
	Vhost     string `json:"vhost"`
	IP        string `json:"ip"`
	ClientIP  string `json:"client_id"`
	ServerID  string `json:"server_id"`
}

type HooksService interface {
	ServiceRunner
	ServiceReadyQuerier
	HooksAPI() int
	HooksEvents() <-chan HooksEvent
}

type hooksService struct {
	readyCtx    context.Context
	readyCancel context.CancelFunc

	httpPort int
	dispose  func()

	r0         error
	hooksOnDvr chan HooksEvent
}

func NewHooksService(opts ...func(v *hooksService)) HooksService {
	v := &hooksService{}

	v.httpPort = allocator.Allocate()
	v.dispose = func() {
		allocator.Free(v.httpPort)
		close(v.hooksOnDvr)
	}
	v.hooksOnDvr = make(chan HooksEvent, 64)
	v.readyCtx, v.readyCancel = context.WithCancel(context.Background())

	for _, opt := range opts {
		opt(v)
	}

	return v
}

func (v *hooksService) ReadyCtx() context.Context {
	return v.readyCtx
}

func (v *hooksService) HooksAPI() int {
	return v.httpPort
}

func (v *hooksService) HooksEvents() <-chan HooksEvent {
	return v.hooksOnDvr
}

func (v *hooksService) Run(ctx context.Context, cancel context.CancelFunc) error {
	defer func() {
		v.readyCancel()
		v.dispose()
	}()

	handler := http.ServeMux{}
	handler.HandleFunc("/api/v1/ping", func(w http.ResponseWriter, r *http.Request) {
		ohttp.WriteData(ctx, w, r, "pong")
	})

	handler.HandleFunc("/api/v1/dvrs", func(w http.ResponseWriter, r *http.Request) {
		b, err := ioutil.ReadAll(r.Body)
		if err != nil {
			ohttp.WriteError(ctx, w, r, err)
			return
		}

		evt := HooksEventOnDvr{}
		if err := json.Unmarshal(b, &evt); err != nil {
			ohttp.WriteError(ctx, w, r, err)
			return
		}

		select {
		case <-ctx.Done():
		case v.hooksOnDvr <- &evt:
		}

		logger.Tf(ctx, "Callback: Got on_dvr request %v", string(b))
		ohttp.WriteData(ctx, w, r, nil)
	})

	server := &http.Server{Addr: fmt.Sprintf(":%v", v.httpPort), Handler: &handler}

	var wg sync.WaitGroup
	defer wg.Wait()

	wg.Add(1)
	go func() {
		defer wg.Done()
		logger.Tf(ctx, "Callback: Start hooks server, listen=%v", v.httpPort)

		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.Wf(ctx, "Callback: Service listen=%v, err %v", v.httpPort, err)
			v.r0 = errors.Wrapf(err, "server listen=%v", v.httpPort)
			cancel()
			return
		}
		logger.Tf(ctx, "Callback: Hooks done, listen=%v", v.httpPort)
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		<-ctx.Done()

		go server.Shutdown(context.Background())
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		for ctx.Err() == nil {
			time.Sleep(100 * time.Millisecond)

			r := fmt.Sprintf("http://localhost:%v/api/v1/ping", v.httpPort)
			res, err := http.Get(r)
			if err != nil {
				continue
			}
			defer res.Body.Close()

			b, err := ioutil.ReadAll(res.Body)
			if err != nil {
				continue
			}

			logger.Tf(ctx, "Callback: API is ready, %v %v", r, string(b))
			v.readyCancel()
			return
		}
	}()

	wg.Wait()
	return v.r0
}
