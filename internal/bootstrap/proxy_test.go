// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package bootstrap

import (
	"context"
	"errors"
	"sync/atomic"
	"testing"
	"time"

	"srsx/internal/env"
	"srsx/internal/env/envfakes"
	"srsx/internal/lb"
	"srsx/internal/lb/lbfakes"
	"srsx/internal/proxy"
	"srsx/internal/proxy/proxyfakes"
)

// =============================================================================
// Local fakes
// =============================================================================

// fakeSignalHandler implements signalHandler without touching real OS signals.
// InstallSignalsCancels, when true, cancels the supplied cancel func immediately
// so callers can drive the run/Start "ctx already cancelled" branch.
type fakeSignalHandler struct {
	installSignalsCalls     atomic.Int32
	installForceQuitCalls   atomic.Int32
	installForceQuitReturn  error
	installSignalsCancels   bool
	lastInstallSignalsCtx   context.Context
	lastInstallForceQuitCtx context.Context
}

func (f *fakeSignalHandler) InstallSignals(ctx context.Context, cancel context.CancelFunc) {
	f.installSignalsCalls.Add(1)
	f.lastInstallSignalsCtx = ctx
	if f.installSignalsCancels {
		cancel()
	}
}

func (f *fakeSignalHandler) InstallForceQuit(ctx context.Context, environment env.ProxyEnvironment) error {
	f.installForceQuitCalls.Add(1)
	f.lastInstallForceQuitCtx = ctx
	return f.installForceQuitReturn
}

// fakeProxyServer implements the local proxyServer interface for the SRT proxy
// and system API seams.
type fakeProxyServer struct {
	runCalls    atomic.Int32
	closeCalls  atomic.Int32
	runReturn   error
	closeReturn error
	lastRunCtx  context.Context
}

func (f *fakeProxyServer) Run(ctx context.Context) error {
	f.runCalls.Add(1)
	f.lastRunCtx = ctx
	return f.runReturn
}

func (f *fakeProxyServer) Close() error {
	f.closeCalls.Add(1)
	return f.closeReturn
}

// =============================================================================
// Helpers
// =============================================================================

// fakeEnvWithDefaults returns a FakeProxyEnvironment with reasonable defaults
// so run() can reach all stages without being short-circuited by a parse error.
func fakeEnvWithDefaults() *envfakes.FakeProxyEnvironment {
	e := &envfakes.FakeProxyEnvironment{}
	e.LoadBalancerTypeReturns("memory")
	e.GraceQuitTimeoutReturns("1s")
	e.ForceQuitTimeoutReturns("1s")
	return e
}

// bootstrapFakes bundles the fakes installed by withAllFakes for assertions.
type bootstrapFakes struct {
	env          *envfakes.FakeProxyEnvironment
	signal       *fakeSignalHandler
	lbMemory     *lbfakes.FakeOriginLoadBalancer
	lbRedis      *lbfakes.FakeOriginLoadBalancer
	rtmp         *proxyfakes.FakeRTMPProxyServer
	webrtc       *proxyfakes.FakeWebRTCProxyServer
	httpAPI      *proxyfakes.FakeHTTPAPIProxyServer
	srt          *fakeProxyServer
	systemAPI    *fakeProxyServer
	httpStream   *proxyfakes.FakeHTTPStreamProxyServer
	memoryCalls  atomic.Int32
	redisCalls   atomic.Int32
	rtcInHTTPAPI atomic.Value // proxy.WebRTCProxyServer instance passed to newHTTPAPIProxyServer
}

// withAllFakes returns a functional option that swaps every seam for a fake.
// The returned bootstrapFakes lets tests inspect calls and arguments.
func withAllFakes(e *envfakes.FakeProxyEnvironment) (func(*proxyBootstrap), *bootstrapFakes) {
	f := &bootstrapFakes{
		env:        e,
		signal:     &fakeSignalHandler{},
		lbMemory:   &lbfakes.FakeOriginLoadBalancer{},
		lbRedis:    &lbfakes.FakeOriginLoadBalancer{},
		rtmp:       &proxyfakes.FakeRTMPProxyServer{},
		webrtc:     &proxyfakes.FakeWebRTCProxyServer{},
		httpAPI:    &proxyfakes.FakeHTTPAPIProxyServer{},
		srt:        &fakeProxyServer{},
		systemAPI:  &fakeProxyServer{},
		httpStream: &proxyfakes.FakeHTTPStreamProxyServer{},
	}
	opt := func(b *proxyBootstrap) {
		b.newEnvironment = func(context.Context) (env.ProxyEnvironment, error) { return f.env, nil }
		b.newSignalHandler = func() signalHandler { return f.signal }
		b.newRedisLoadBalancer = func(env.ProxyEnvironment) lb.OriginLoadBalancer {
			f.redisCalls.Add(1)
			return f.lbRedis
		}
		b.newMemoryLoadBalancer = func(env.ProxyEnvironment) lb.OriginLoadBalancer {
			f.memoryCalls.Add(1)
			return f.lbMemory
		}
		b.newRTMPProxyServer = func(env.ProxyEnvironment, lb.OriginLoadBalancer) proxy.RTMPProxyServer { return f.rtmp }
		b.newWebRTCProxyServer = func(env.ProxyEnvironment, lb.OriginLoadBalancer) proxy.WebRTCProxyServer { return f.webrtc }
		b.newHTTPAPIProxyServer = func(_ env.ProxyEnvironment, _ time.Duration, rtc proxy.WebRTCProxyServer) proxy.HTTPAPIProxyServer {
			f.rtcInHTTPAPI.Store(rtc)
			return f.httpAPI
		}
		b.newSRSSRTProxyServer = func(env.ProxyEnvironment, lb.OriginLoadBalancer) proxyServer { return f.srt }
		b.newSystemAPI = func(env.ProxyEnvironment, lb.OriginLoadBalancer, time.Duration) proxyServer { return f.systemAPI }
		b.newHTTPStreamProxyServer = func(env.ProxyEnvironment, lb.OriginLoadBalancer, time.Duration) proxy.HTTPStreamProxyServer {
			return f.httpStream
		}
	}
	return opt, f
}

// =============================================================================
// NewProxyBootstrap
// =============================================================================

func TestNewProxyBootstrap_DefaultsAllSeams(t *testing.T) {
	b := NewProxyBootstrap().(*proxyBootstrap)

	if b.newEnvironment == nil {
		t.Error("newEnvironment seam should default to non-nil")
	}
	if b.newSignalHandler == nil {
		t.Error("newSignalHandler seam should default to non-nil")
	}
	if b.newRedisLoadBalancer == nil {
		t.Error("newRedisLoadBalancer seam should default to non-nil")
	}
	if b.newMemoryLoadBalancer == nil {
		t.Error("newMemoryLoadBalancer seam should default to non-nil")
	}
	if b.newRTMPProxyServer == nil {
		t.Error("newRTMPProxyServer seam should default to non-nil")
	}
	if b.newWebRTCProxyServer == nil {
		t.Error("newWebRTCProxyServer seam should default to non-nil")
	}
	if b.newHTTPAPIProxyServer == nil {
		t.Error("newHTTPAPIProxyServer seam should default to non-nil")
	}
	if b.newSRSSRTProxyServer == nil {
		t.Error("newSRSSRTProxyServer seam should default to non-nil")
	}
	if b.newSystemAPI == nil {
		t.Error("newSystemAPI seam should default to non-nil")
	}
	if b.newHTTPStreamProxyServer == nil {
		t.Error("newHTTPStreamProxyServer seam should default to non-nil")
	}
}

func TestNewProxyBootstrap_AppliesOpts(t *testing.T) {
	var called bool
	NewProxyBootstrap(func(b *proxyBootstrap) { called = true })
	if !called {
		t.Fatal("opt was not invoked")
	}
}

// TestNewProxyBootstrap_DefaultsConstructRealInstances exercises every default
// closure that is safe to call in a unit test (i.e. does not touch real
// network/filesystem state). newEnvironment is excluded because env.NewProxyEnvironment
// loads a .env file and mutates process env vars.
func TestNewProxyBootstrap_DefaultsConstructRealInstances(t *testing.T) {
	b := NewProxyBootstrap().(*proxyBootstrap)
	e := fakeEnvWithDefaults()
	loadBalancer := &lbfakes.FakeOriginLoadBalancer{}

	if got := b.newSignalHandler(); got == nil {
		t.Error("newSignalHandler default returned nil")
	}
	if got := b.newRedisLoadBalancer(e); got == nil {
		t.Error("newRedisLoadBalancer default returned nil")
	}
	if got := b.newMemoryLoadBalancer(e); got == nil {
		t.Error("newMemoryLoadBalancer default returned nil")
	}
	if got := b.newRTMPProxyServer(e, loadBalancer); got == nil {
		t.Error("newRTMPProxyServer default returned nil")
	}
	rtc := b.newWebRTCProxyServer(e, loadBalancer)
	if rtc == nil {
		t.Error("newWebRTCProxyServer default returned nil")
	}
	if got := b.newHTTPAPIProxyServer(e, time.Second, rtc); got == nil {
		t.Error("newHTTPAPIProxyServer default returned nil")
	}
	if got := b.newSRSSRTProxyServer(e, loadBalancer); got == nil {
		t.Error("newSRSSRTProxyServer default returned nil")
	}
	if got := b.newSystemAPI(e, loadBalancer, time.Second); got == nil {
		t.Error("newSystemAPI default returned nil")
	}
	if got := b.newHTTPStreamProxyServer(e, loadBalancer, time.Second); got == nil {
		t.Error("newHTTPStreamProxyServer default returned nil")
	}
}

func TestNewProxyBootstrap_OptCanOverrideSeam(t *testing.T) {
	customErr := errors.New("custom")
	b := NewProxyBootstrap(func(b *proxyBootstrap) {
		b.newEnvironment = func(context.Context) (env.ProxyEnvironment, error) { return nil, customErr }
	}).(*proxyBootstrap)

	_, err := b.newEnvironment(context.Background())
	if !errors.Is(err, customErr) {
		t.Errorf("custom newEnvironment not applied: %v", err)
	}
}

// =============================================================================
// initializeLoadBalancer
// =============================================================================

func TestInitializeLoadBalancer_Redis(t *testing.T) {
	e := fakeEnvWithDefaults()
	e.LoadBalancerTypeReturns("redis")
	opt, f := withAllFakes(e)
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	got, err := b.initializeLoadBalancer(context.Background(), f.env)
	if err != nil {
		t.Fatalf("initializeLoadBalancer: %v", err)
	}
	if got != f.lbRedis {
		t.Error("expected the redis load balancer")
	}
	if f.redisCalls.Load() != 1 {
		t.Errorf("newRedisLoadBalancer calls = %d, want 1", f.redisCalls.Load())
	}
	if f.memoryCalls.Load() != 0 {
		t.Errorf("newMemoryLoadBalancer calls = %d, want 0", f.memoryCalls.Load())
	}
	if f.lbRedis.InitializeCallCount() != 1 {
		t.Errorf("Initialize calls = %d, want 1", f.lbRedis.InitializeCallCount())
	}
}

func TestInitializeLoadBalancer_Memory(t *testing.T) {
	e := fakeEnvWithDefaults()
	e.LoadBalancerTypeReturns("memory")
	opt, f := withAllFakes(e)
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	got, err := b.initializeLoadBalancer(context.Background(), f.env)
	if err != nil {
		t.Fatalf("initializeLoadBalancer: %v", err)
	}
	if got != f.lbMemory {
		t.Error("expected the memory load balancer")
	}
	if f.memoryCalls.Load() != 1 {
		t.Errorf("newMemoryLoadBalancer calls = %d, want 1", f.memoryCalls.Load())
	}
	if f.redisCalls.Load() != 0 {
		t.Errorf("newRedisLoadBalancer calls = %d, want 0", f.redisCalls.Load())
	}
}

func TestInitializeLoadBalancer_DefaultBranchUsesMemory(t *testing.T) {
	e := fakeEnvWithDefaults()
	e.LoadBalancerTypeReturns("anything-else")
	opt, f := withAllFakes(e)
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	if _, err := b.initializeLoadBalancer(context.Background(), f.env); err != nil {
		t.Fatalf("initializeLoadBalancer: %v", err)
	}
	if f.memoryCalls.Load() != 1 {
		t.Error("unknown LoadBalancerType should fall through to memory")
	}
}

func TestInitializeLoadBalancer_InitializeErrorIsWrapped(t *testing.T) {
	initErr := errors.New("boom")
	e := fakeEnvWithDefaults()
	opt, f := withAllFakes(e)
	f.lbMemory.InitializeReturns(initErr)
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	_, err := b.initializeLoadBalancer(context.Background(), f.env)
	if err == nil {
		t.Fatal("expected an error")
	}
	if !errors.Is(err, initErr) {
		t.Errorf("error chain missing initErr: %v", err)
	}
}

// =============================================================================
// startServers
// =============================================================================

// runStartServersUntilCancel runs startServers in a goroutine, cancels the ctx
// once the test has observed all servers running, and returns the result.
func runStartServersUntilCancel(t *testing.T, b *proxyBootstrap, env env.ProxyEnvironment, lb lb.OriginLoadBalancer) error {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- b.startServers(ctx, env, lb, 50*time.Millisecond) }()
	// Give startServers time to invoke all six constructors and block on <-ctx.Done().
	time.Sleep(20 * time.Millisecond)
	cancel()
	select {
	case err := <-done:
		return err
	case <-time.After(2 * time.Second):
		t.Fatal("startServers did not return after ctx cancel")
		return nil
	}
}

func TestStartServers_HappyPath_StartsAndClosesAllSix(t *testing.T) {
	opt, f := withAllFakes(fakeEnvWithDefaults())
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	if err := runStartServersUntilCancel(t, b, f.env, f.lbMemory); err != nil {
		t.Fatalf("startServers: %v", err)
	}

	if got := f.rtmp.RunCallCount(); got != 1 {
		t.Errorf("rtmp Run = %d, want 1", got)
	}
	if got := f.webrtc.RunCallCount(); got != 1 {
		t.Errorf("webrtc Run = %d, want 1", got)
	}
	if got := f.httpAPI.RunCallCount(); got != 1 {
		t.Errorf("httpAPI Run = %d, want 1", got)
	}
	if got := f.srt.runCalls.Load(); got != 1 {
		t.Errorf("srt Run = %d, want 1", got)
	}
	if got := f.systemAPI.runCalls.Load(); got != 1 {
		t.Errorf("systemAPI Run = %d, want 1", got)
	}
	if got := f.httpStream.RunCallCount(); got != 1 {
		t.Errorf("httpStream Run = %d, want 1", got)
	}

	if got := f.rtmp.CloseCallCount(); got != 1 {
		t.Errorf("rtmp Close = %d, want 1", got)
	}
	if got := f.webrtc.CloseCallCount(); got != 1 {
		t.Errorf("webrtc Close = %d, want 1", got)
	}
	if got := f.httpAPI.CloseCallCount(); got != 1 {
		t.Errorf("httpAPI Close = %d, want 1", got)
	}
	if got := f.srt.closeCalls.Load(); got != 1 {
		t.Errorf("srt Close = %d, want 1", got)
	}
	if got := f.systemAPI.closeCalls.Load(); got != 1 {
		t.Errorf("systemAPI Close = %d, want 1", got)
	}
	if got := f.httpStream.CloseCallCount(); got != 1 {
		t.Errorf("httpStream Close = %d, want 1", got)
	}
}

func TestStartServers_HTTPAPIReceivesWebRTCInstance(t *testing.T) {
	opt, f := withAllFakes(fakeEnvWithDefaults())
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	if err := runStartServersUntilCancel(t, b, f.env, f.lbMemory); err != nil {
		t.Fatalf("startServers: %v", err)
	}

	rtc := f.rtcInHTTPAPI.Load()
	if rtc == nil {
		t.Fatal("newHTTPAPIProxyServer was not invoked with a WebRTC instance")
	}
	if rtc.(proxy.WebRTCProxyServer) != f.webrtc {
		t.Error("HTTPAPI received a different WebRTC instance than newWebRTCProxyServer returned")
	}
}

func TestStartServers_RunErrorsAreWrappedAndShortCircuit(t *testing.T) {
	tests := []struct {
		name           string
		install        func(f *bootstrapFakes, err error)
		wantWrap       string
		earlierStarted func(f *bootstrapFakes) bool
	}{
		{
			name:     "rtmp",
			install:  func(f *bootstrapFakes, err error) { f.rtmp.RunReturns(err) },
			wantWrap: "rtmp server",
			earlierStarted: func(f *bootstrapFakes) bool {
				return f.webrtc.RunCallCount() == 0 && f.httpAPI.RunCallCount() == 0
			},
		},
		{
			name:     "webrtc",
			install:  func(f *bootstrapFakes, err error) { f.webrtc.RunReturns(err) },
			wantWrap: "rtc server",
			earlierStarted: func(f *bootstrapFakes) bool {
				return f.rtmp.RunCallCount() == 1 && f.httpAPI.RunCallCount() == 0
			},
		},
		{
			name:     "httpAPI",
			install:  func(f *bootstrapFakes, err error) { f.httpAPI.RunReturns(err) },
			wantWrap: "http api server",
			earlierStarted: func(f *bootstrapFakes) bool {
				return f.webrtc.RunCallCount() == 1 && f.srt.runCalls.Load() == 0
			},
		},
		{
			name:     "srt",
			install:  func(f *bootstrapFakes, err error) { f.srt.runReturn = err },
			wantWrap: "srt server",
			earlierStarted: func(f *bootstrapFakes) bool {
				return f.httpAPI.RunCallCount() == 1 && f.systemAPI.runCalls.Load() == 0
			},
		},
		{
			name:     "systemAPI",
			install:  func(f *bootstrapFakes, err error) { f.systemAPI.runReturn = err },
			wantWrap: "system api server",
			earlierStarted: func(f *bootstrapFakes) bool {
				return f.srt.runCalls.Load() == 1 && f.httpStream.RunCallCount() == 0
			},
		},
		{
			name:     "httpStream",
			install:  func(f *bootstrapFakes, err error) { f.httpStream.RunReturns(err) },
			wantWrap: "http server",
			earlierStarted: func(f *bootstrapFakes) bool {
				return f.systemAPI.runCalls.Load() == 1
			},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			runErr := errors.New("boom-" + tc.name)
			opt, f := withAllFakes(fakeEnvWithDefaults())
			tc.install(f, runErr)
			b := NewProxyBootstrap(opt).(*proxyBootstrap)

			err := b.startServers(context.Background(), f.env, f.lbMemory, 50*time.Millisecond)
			if err == nil {
				t.Fatalf("%s: expected error", tc.name)
			}
			if !errors.Is(err, runErr) {
				t.Errorf("%s: error chain missing runErr: %v", tc.name, err)
			}
			if !contains(err.Error(), tc.wantWrap) {
				t.Errorf("%s: error %q does not contain wrap %q", tc.name, err.Error(), tc.wantWrap)
			}
			if !tc.earlierStarted(f) {
				t.Errorf("%s: short-circuit invariant violated", tc.name)
			}
		})
	}
}

// contains is a tiny helper so the table-driven test doesn't pull in strings
// just for substring matching.
func contains(haystack, needle string) bool {
	for i := 0; i+len(needle) <= len(haystack); i++ {
		if haystack[i:i+len(needle)] == needle {
			return true
		}
	}
	return false
}

// =============================================================================
// run
// =============================================================================

func TestRun_NewEnvironmentErrorIsWrapped(t *testing.T) {
	envErr := errors.New("env-boom")
	opt, _ := withAllFakes(fakeEnvWithDefaults())
	b := NewProxyBootstrap(opt, func(b *proxyBootstrap) {
		b.newEnvironment = func(context.Context) (env.ProxyEnvironment, error) { return nil, envErr }
	}).(*proxyBootstrap)

	err := b.run(context.Background())
	if err == nil {
		t.Fatal("expected error")
	}
	if !errors.Is(err, envErr) {
		t.Errorf("error chain missing envErr: %v", err)
	}
	if !contains(err.Error(), "create environment") {
		t.Errorf("expected wrap %q, got %q", "create environment", err.Error())
	}
}

func TestRun_InstallForceQuitErrorIsWrapped(t *testing.T) {
	fqErr := errors.New("force-quit-boom")
	opt, f := withAllFakes(fakeEnvWithDefaults())
	f.signal.installForceQuitReturn = fqErr
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	err := b.run(context.Background())
	if err == nil {
		t.Fatal("expected error")
	}
	if !errors.Is(err, fqErr) {
		t.Errorf("error chain missing fqErr: %v", err)
	}
	if !contains(err.Error(), "install force quit") {
		t.Errorf("expected wrap %q, got %q", "install force quit", err.Error())
	}
}

func TestRun_BadGraceQuitDurationIsWrapped(t *testing.T) {
	e := fakeEnvWithDefaults()
	e.GraceQuitTimeoutReturns("not-a-duration")
	opt, _ := withAllFakes(e)
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	err := b.run(context.Background())
	if err == nil {
		t.Fatal("expected error")
	}
	if !contains(err.Error(), "parse gracefully quit timeout") {
		t.Errorf("expected wrap %q, got %q", "parse gracefully quit timeout", err.Error())
	}
}

func TestRun_LoadBalancerInitializeErrorIsWrapped(t *testing.T) {
	initErr := errors.New("init-boom")
	opt, f := withAllFakes(fakeEnvWithDefaults())
	f.lbMemory.InitializeReturns(initErr)
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	err := b.run(context.Background())
	if err == nil {
		t.Fatal("expected error")
	}
	if !errors.Is(err, initErr) {
		t.Errorf("error chain missing initErr: %v", err)
	}
	if !contains(err.Error(), "initialize srs load balancer") {
		t.Errorf("expected wrap %q, got %q", "initialize srs load balancer", err.Error())
	}
}

func TestRun_HappyPath_BlocksUntilCancelThenReturnsNil(t *testing.T) {
	opt, _ := withAllFakes(fakeEnvWithDefaults())
	b := NewProxyBootstrap(opt).(*proxyBootstrap)

	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- b.run(ctx) }()
	time.Sleep(20 * time.Millisecond)
	cancel()
	select {
	case err := <-done:
		if err != nil {
			t.Errorf("run: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("run did not return after ctx cancel")
	}
}

// =============================================================================
// Start
// =============================================================================

func TestStart_HappyPath_InstallsSignalsAndReturnsNil(t *testing.T) {
	opt, f := withAllFakes(fakeEnvWithDefaults())
	f.signal.installSignalsCancels = true // cancel the inner ctx immediately
	b := NewProxyBootstrap(opt)

	err := b.Start(context.Background())
	if err != nil {
		t.Fatalf("Start: %v", err)
	}
	if f.signal.installSignalsCalls.Load() != 1 {
		t.Errorf("InstallSignals calls = %d, want 1", f.signal.installSignalsCalls.Load())
	}
	if f.signal.installForceQuitCalls.Load() != 1 {
		t.Errorf("InstallForceQuit calls = %d, want 1", f.signal.installForceQuitCalls.Load())
	}
}

func TestStart_PropagatesNonCancelError(t *testing.T) {
	envErr := errors.New("env-boom")
	opt, _ := withAllFakes(fakeEnvWithDefaults())
	b := NewProxyBootstrap(opt, func(b *proxyBootstrap) {
		b.newEnvironment = func(context.Context) (env.ProxyEnvironment, error) { return nil, envErr }
	})

	err := b.Start(context.Background())
	if err == nil {
		t.Fatal("expected error")
	}
	if !errors.Is(err, envErr) {
		t.Errorf("error chain missing envErr: %v", err)
	}
}

func TestStart_AbsorbsErrorWhenContextCancelled(t *testing.T) {
	// When InstallSignals cancels the inner ctx and run returns an error, Start
	// should swallow the error (treating it as a graceful shutdown).
	envErr := errors.New("post-cancel-boom")
	opt, f := withAllFakes(fakeEnvWithDefaults())
	f.signal.installSignalsCancels = true
	b := NewProxyBootstrap(opt, func(b *proxyBootstrap) {
		b.newEnvironment = func(context.Context) (env.ProxyEnvironment, error) { return nil, envErr }
	})

	err := b.Start(context.Background())
	if err != nil {
		t.Errorf("Start should swallow error after ctx cancel, got: %v", err)
	}
}
