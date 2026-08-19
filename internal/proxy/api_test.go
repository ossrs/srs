// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"

	"srsx/internal/env"
	"srsx/internal/env/envfakes"
	"srsx/internal/lb/lbfakes"
)

// fakeWebRTCProxyServer is a minimal in-package WebRTCProxyServer used by
// httpAPIProxyServer tests. Only the WHIP/WHEP handler methods are exercised.
// Run/Close are inert stubs so the type satisfies the interface.
type fakeWebRTCProxyServer struct {
	whipCalls        atomic.Int32
	whepCalls        atomic.Int32
	whipReturn       error
	whepReturn       error
	whipResponseBody string
	whepResponseBody string
}

func (f *fakeWebRTCProxyServer) Run(ctx context.Context) error { return nil }
func (f *fakeWebRTCProxyServer) Close() error                  { return nil }
func (f *fakeWebRTCProxyServer) HandleApiForWHIP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	f.whipCalls.Add(1)
	if f.whipResponseBody != "" {
		w.WriteHeader(http.StatusOK)
		io.WriteString(w, f.whipResponseBody)
	}
	return f.whipReturn
}
func (f *fakeWebRTCProxyServer) HandleApiForWHEP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	f.whepCalls.Add(1)
	if f.whepResponseBody != "" {
		w.WriteHeader(http.StatusOK)
		io.WriteString(w, f.whepResponseBody)
	}
	return f.whepReturn
}

// captureMuxFromHTTPAPIRun drives NewHTTPAPIProxyServer.Run with a fake server
// that captures the registered mux. Caller is responsible for cancelling ctx
// to trigger shutdown.
func captureMuxFromHTTPAPIRun(t *testing.T, environment env.ProxyEnvironment,
	rtc WebRTCProxyServer, ctx context.Context,
	opts ...func(*httpAPIProxyServer)) (*http.ServeMux, *fakeHTTPProxyServer, *httpAPIProxyServer) {
	t.Helper()

	fakeSrv := newFakeHTTPProxyServer()
	var capturedMux *http.ServeMux

	baseOpts := []func(*httpAPIProxyServer){
		func(s *httpAPIProxyServer) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				mux := http.NewServeMux()
				capturedMux = mux
				return fakeSrv, mux
			}
		},
	}
	srvIface := NewHTTPAPIProxyServer(environment, 50*time.Millisecond, rtc, append(baseOpts, opts...)...)
	srv := srvIface.(*httpAPIProxyServer)

	if err := srv.Run(ctx); err != nil {
		t.Fatalf("Run: %v", err)
	}
	if capturedMux == nil {
		t.Fatal("newServer was not called by Run")
	}
	return capturedMux, fakeSrv, srv
}

// captureMuxFromSystemAPIRun drives NewSystemAPI.Run with a fake server that
// captures the registered mux. Caller cancels ctx to trigger shutdown.
func captureMuxFromSystemAPIRun(t *testing.T, environment env.ProxyEnvironment,
	lbFake *lbfakes.FakeOriginLoadBalancer, ctx context.Context,
	opts ...func(*systemAPI)) (*http.ServeMux, *fakeHTTPProxyServer, *systemAPI) {
	t.Helper()

	fakeSrv := newFakeHTTPProxyServer()
	var capturedMux *http.ServeMux

	baseOpts := []func(*systemAPI){
		func(s *systemAPI) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				mux := http.NewServeMux()
				capturedMux = mux
				return fakeSrv, mux
			}
		},
	}
	srv := NewSystemAPI(environment, lbFake, 50*time.Millisecond, append(baseOpts, opts...)...)

	if err := srv.Run(ctx); err != nil {
		t.Fatalf("Run: %v", err)
	}
	if capturedMux == nil {
		t.Fatal("newServer was not called by Run")
	}
	return capturedMux, fakeSrv, srv
}

// =============================================================================
// NewHTTPAPIProxyServer
// =============================================================================

func TestHTTPAPIProxyServer_New_StoresFieldsAndDefaultsSeams(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	rtc := &fakeWebRTCProxyServer{}
	timeout := 2 * time.Second
	srv := NewHTTPAPIProxyServer(env, timeout, rtc).(*httpAPIProxyServer)

	if srv.environment != env {
		t.Error("environment not stored")
	}
	if srv.rtc != rtc {
		t.Error("rtc not stored")
	}
	if srv.gracefulQuitTimeout != timeout {
		t.Errorf("gracefulQuitTimeout = %v, want %v", srv.gracefulQuitTimeout, timeout)
	}
	if srv.shutdown == nil {
		t.Error("shutdown seam should default to non-nil")
	}
	if srv.newServer == nil {
		t.Error("newServer seam should default to non-nil")
	}
}

func TestHTTPAPIProxyServer_New_AppliesOpts(t *testing.T) {
	var called bool
	srv := NewHTTPAPIProxyServer(&envfakes.FakeProxyEnvironment{}, time.Second,
		&fakeWebRTCProxyServer{},
		func(s *httpAPIProxyServer) { called = true }).(*httpAPIProxyServer)
	if !called {
		t.Fatal("opt was not invoked")
	}
	if srv.shutdown == nil {
		t.Error("default seams should still be set when opt doesn't override them")
	}
}

func TestHTTPAPIProxyServer_New_OptCanOverrideAllSeams(t *testing.T) {
	customShutdown := func(context.Context) error { return errors.New("custom") }
	customNewServer := func(string) (httpServer, *http.ServeMux) { return nil, nil }

	srv := NewHTTPAPIProxyServer(&envfakes.FakeProxyEnvironment{}, time.Second,
		&fakeWebRTCProxyServer{},
		func(s *httpAPIProxyServer) {
			s.shutdown = customShutdown
			s.newServer = customNewServer
		}).(*httpAPIProxyServer)

	if err := srv.shutdown(context.Background()); err == nil || err.Error() != "custom" {
		t.Errorf("custom shutdown not applied: %v", err)
	}
	// Pointer comparison on func values isn't supported by ==; call the value
	// and observe the override via behavior.
	if got, _ := srv.newServer(""); got != nil {
		t.Error("custom newServer not applied")
	}
}

// =============================================================================
// httpAPIProxyServer — default factory behavior
// =============================================================================

func TestHTTPAPIProxyServer_DefaultNewServer_BuildsRealServerAndMux(t *testing.T) {
	srv := NewHTTPAPIProxyServer(&envfakes.FakeProxyEnvironment{}, time.Second,
		&fakeWebRTCProxyServer{}).(*httpAPIProxyServer)

	got, mux := srv.newServer(":12321")
	if mux == nil {
		t.Fatal("mux is nil")
	}
	real, ok := got.(*http.Server)
	if !ok {
		t.Fatalf("expected *http.Server, got %T", got)
	}
	if real.Addr != ":12321" {
		t.Errorf("Addr = %q, want :12321", real.Addr)
	}
	if real.Handler != mux {
		t.Error("Handler should be the returned mux")
	}
}

func TestHTTPAPIProxyServer_DefaultShutdown_DelegatesToServer(t *testing.T) {
	fakeSrv := newFakeHTTPProxyServer()
	srv := NewHTTPAPIProxyServer(&envfakes.FakeProxyEnvironment{}, time.Second,
		&fakeWebRTCProxyServer{}).(*httpAPIProxyServer)
	srv.server = fakeSrv // simulate what Run() would assign

	if err := srv.shutdown(context.Background()); err != nil {
		t.Fatalf("shutdown: %v", err)
	}
	if fakeSrv.shutdownCalls.Load() != 1 {
		t.Fatalf("shutdown was not delegated to server, calls=%d", fakeSrv.shutdownCalls.Load())
	}
}

// =============================================================================
// httpAPIProxyServer — Close
// =============================================================================

func TestHTTPAPIProxyServer_Close_InvokesShutdownWithDeadline(t *testing.T) {
	var gotCtx context.Context
	var calls int
	srv := NewHTTPAPIProxyServer(&envfakes.FakeProxyEnvironment{}, 50*time.Millisecond,
		&fakeWebRTCProxyServer{},
		func(s *httpAPIProxyServer) {
			s.shutdown = func(ctx context.Context) error {
				gotCtx = ctx
				calls++
				return nil
			}
		}).(*httpAPIProxyServer)

	if err := srv.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	if calls != 1 {
		t.Fatalf("shutdown calls = %d, want 1", calls)
	}
	if _, ok := gotCtx.Deadline(); !ok {
		t.Error("Close should pass a deadline-bearing ctx to shutdown")
	}
}

// =============================================================================
// httpAPIProxyServer — Run lifecycle
// =============================================================================

func TestHTTPAPIProxyServer_Run_AddrWithoutColonPrependsIt(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns("11985")

	var capturedAddr string
	fakeSrv := newFakeHTTPProxyServer()
	srvIface := NewHTTPAPIProxyServer(env, 50*time.Millisecond, &fakeWebRTCProxyServer{},
		func(s *httpAPIProxyServer) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				capturedAddr = addr
				return fakeSrv, http.NewServeMux()
			}
		})
	defer srvIface.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if err := srvIface.Run(ctx); err != nil {
		t.Fatalf("Run: %v", err)
	}
	if capturedAddr != ":11985" {
		t.Fatalf("newServer addr = %q, want :11985", capturedAddr)
	}
}

func TestHTTPAPIProxyServer_Run_AddrWithColonUnchanged(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns("127.0.0.1:9999")

	var capturedAddr string
	fakeSrv := newFakeHTTPProxyServer()
	srvIface := NewHTTPAPIProxyServer(env, 50*time.Millisecond, &fakeWebRTCProxyServer{},
		func(s *httpAPIProxyServer) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				capturedAddr = addr
				return fakeSrv, http.NewServeMux()
			}
		})
	defer srvIface.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if err := srvIface.Run(ctx); err != nil {
		t.Fatalf("Run: %v", err)
	}
	if capturedAddr != "127.0.0.1:9999" {
		t.Fatalf("newServer addr = %q", capturedAddr)
	}
}

func TestHTTPAPIProxyServer_Run_CtxCancelTriggersShutdown(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	_, fakeSrv, _ := captureMuxFromHTTPAPIRun(t, env, &fakeWebRTCProxyServer{}, ctx)

	deadline := time.Now().Add(time.Second)
	for fakeSrv.listenCalls.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if fakeSrv.listenCalls.Load() == 0 {
		t.Fatal("ListenAndServe goroutine did not start")
	}

	cancel()

	deadline = time.Now().Add(time.Second)
	for fakeSrv.shutdownCalls.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if fakeSrv.shutdownCalls.Load() == 0 {
		t.Fatal("Shutdown was not invoked after ctx cancel")
	}
}

// =============================================================================
// httpAPIProxyServer — handler dispatch
// =============================================================================

func TestHTTPAPIProxyServer_Run_HandlerVersionsReturnsJSON(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromHTTPAPIRun(t, env, &fakeWebRTCProxyServer{}, ctx)

	req := httptest.NewRequest(http.MethodGet, "/api/v1/versions", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	var body map[string]string
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("json: %v\nbody=%s", err, rec.Body.String())
	}
	if body["signature"] == "" {
		t.Error("signature should be populated")
	}
	if body["version"] == "" {
		t.Error("version should be populated")
	}
}

func TestHTTPAPIProxyServer_Run_HandlerWHIPDelegatesToRTC(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")
	rtc := &fakeWebRTCProxyServer{whipResponseBody: "ok-whip"}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromHTTPAPIRun(t, env, rtc, ctx)

	req := httptest.NewRequest(http.MethodPost, "/rtc/v1/whip/?app=live&stream=s", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rtc.whipCalls.Load() != 1 {
		t.Fatalf("HandleApiForWHIP calls = %d, want 1", rtc.whipCalls.Load())
	}
	if rtc.whepCalls.Load() != 0 {
		t.Errorf("HandleApiForWHEP should not be invoked")
	}
	if !bytes.Equal(rec.Body.Bytes(), []byte("ok-whip")) {
		t.Errorf("body = %q, want ok-whip", rec.Body.String())
	}
}

func TestHTTPAPIProxyServer_Run_HandlerLegacyPublishRoutesToWHIP(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")
	rtc := &fakeWebRTCProxyServer{}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromHTTPAPIRun(t, env, rtc, ctx)

	req := httptest.NewRequest(http.MethodPost, "/rtc/v1/publish/", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rtc.whipCalls.Load() != 1 {
		t.Fatalf("HandleApiForWHIP via /rtc/v1/publish/ calls = %d, want 1", rtc.whipCalls.Load())
	}
	if rtc.whepCalls.Load() != 0 {
		t.Errorf("HandleApiForWHEP should not be invoked via /rtc/v1/publish/")
	}
}

func TestHTTPAPIProxyServer_Run_HandlerWHIPErrorInvokesApiError(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")
	rtc := &fakeWebRTCProxyServer{whipReturn: errors.New("boom-whip")}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromHTTPAPIRun(t, env, rtc, ctx)

	req := httptest.NewRequest(http.MethodPost, "/rtc/v1/whip/", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusInternalServerError {
		t.Fatalf("status = %d, want 500", rec.Code)
	}
	if !bytes.Contains(rec.Body.Bytes(), []byte("boom-whip")) {
		t.Errorf("body = %q, expected to contain error message", rec.Body.String())
	}
}

func TestHTTPAPIProxyServer_Run_HandlerWHEPDelegatesToRTC(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")
	rtc := &fakeWebRTCProxyServer{whepResponseBody: "ok-whep"}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromHTTPAPIRun(t, env, rtc, ctx)

	req := httptest.NewRequest(http.MethodPost, "/rtc/v1/whep/", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rtc.whepCalls.Load() != 1 {
		t.Fatalf("HandleApiForWHEP calls = %d, want 1", rtc.whepCalls.Load())
	}
	if rtc.whipCalls.Load() != 0 {
		t.Errorf("HandleApiForWHIP should not be invoked")
	}
	if !bytes.Equal(rec.Body.Bytes(), []byte("ok-whep")) {
		t.Errorf("body = %q, want ok-whep", rec.Body.String())
	}
}

func TestHTTPAPIProxyServer_Run_HandlerLegacyPlayRoutesToWHEP(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")
	rtc := &fakeWebRTCProxyServer{}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromHTTPAPIRun(t, env, rtc, ctx)

	req := httptest.NewRequest(http.MethodPost, "/rtc/v1/play/", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rtc.whepCalls.Load() != 1 {
		t.Fatalf("HandleApiForWHEP via /rtc/v1/play/ calls = %d, want 1", rtc.whepCalls.Load())
	}
	if rtc.whipCalls.Load() != 0 {
		t.Errorf("HandleApiForWHIP should not be invoked via /rtc/v1/play/")
	}
}

func TestHTTPAPIProxyServer_Run_HandlerWHEPErrorInvokesApiError(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpAPIReturns(":0")
	rtc := &fakeWebRTCProxyServer{whepReturn: errors.New("boom-whep")}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromHTTPAPIRun(t, env, rtc, ctx)

	req := httptest.NewRequest(http.MethodPost, "/rtc/v1/whep/", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusInternalServerError {
		t.Fatalf("status = %d, want 500", rec.Code)
	}
	if !bytes.Contains(rec.Body.Bytes(), []byte("boom-whep")) {
		t.Errorf("body = %q", rec.Body.String())
	}
}

// =============================================================================
// NewSystemAPI
// =============================================================================

func TestSystemAPI_New_StoresFieldsAndDefaultsSeams(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	timeout := 2 * time.Second
	srv := NewSystemAPI(env, lbFake, timeout)

	if srv.environment != env {
		t.Error("environment not stored")
	}
	if srv.loadBalancer != lbFake {
		t.Error("loadBalancer not stored")
	}
	if srv.gracefulQuitTimeout != timeout {
		t.Errorf("gracefulQuitTimeout = %v, want %v", srv.gracefulQuitTimeout, timeout)
	}
	if srv.shutdown == nil {
		t.Error("shutdown seam should default to non-nil")
	}
	if srv.newServer == nil {
		t.Error("newServer seam should default to non-nil")
	}
}

func TestSystemAPI_New_AppliesOpts(t *testing.T) {
	var called bool
	srv := NewSystemAPI(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{},
		time.Second, func(s *systemAPI) { called = true })
	if !called {
		t.Fatal("opt was not invoked")
	}
	if srv.shutdown == nil {
		t.Error("default seams should still be set when opt doesn't override them")
	}
}

func TestSystemAPI_New_OptCanOverrideAllSeams(t *testing.T) {
	customShutdown := func(context.Context) error { return errors.New("custom") }
	customNewServer := func(string) (httpServer, *http.ServeMux) { return nil, nil }

	srv := NewSystemAPI(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{},
		time.Second, func(s *systemAPI) {
			s.shutdown = customShutdown
			s.newServer = customNewServer
		})

	if err := srv.shutdown(context.Background()); err == nil || err.Error() != "custom" {
		t.Errorf("custom shutdown not applied: %v", err)
	}
	if got, _ := srv.newServer(""); got != nil {
		t.Error("custom newServer not applied")
	}
}

func TestSystemAPI_WithHTTPAPIAuth(t *testing.T) {
	tests := []struct {
		name             string
		enabled          string
		authorization    string
		wantStatus       int
		wantNextCalls    int
		wantAuthenticate string
	}{
		{
			name:          "disabled allows request",
			enabled:       "off",
			wantStatus:    http.StatusNoContent,
			wantNextCalls: 1,
		},
		{
			name:             "missing authorization",
			enabled:          "on",
			wantStatus:       http.StatusUnauthorized,
			wantAuthenticate: "Bearer",
		},
		{
			name:             "wrong authentication scheme",
			enabled:          "on",
			authorization:    "Basic secret-token",
			wantStatus:       http.StatusUnauthorized,
			wantAuthenticate: "Bearer",
		},
		{
			name:             "empty bearer token",
			enabled:          "on",
			authorization:    "Bearer ",
			wantStatus:       http.StatusUnauthorized,
			wantAuthenticate: "Bearer",
		},
		{
			name:             "wrong bearer token",
			enabled:          "on",
			authorization:    "Bearer wrong-token",
			wantStatus:       http.StatusUnauthorized,
			wantAuthenticate: "Bearer",
		},
		{
			name:          "correct bearer token",
			enabled:       "on",
			authorization: "Bearer secret-token",
			wantStatus:    http.StatusNoContent,
			wantNextCalls: 1,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			environment := &envfakes.FakeProxyEnvironment{}
			environment.HttpAPIAuthEnabledReturns(tc.enabled)
			environment.HttpAPIAuthTokenReturns("secret-token")
			server := &systemAPI{environment: environment}

			nextCalls := 0
			handler := server.withHTTPAPIAuth(func(w http.ResponseWriter, r *http.Request) {
				nextCalls++
				w.WriteHeader(http.StatusNoContent)
			})
			req := httptest.NewRequest(http.MethodPost, "/api/v1/srs/register", nil)
			if tc.authorization != "" {
				req.Header.Set("Authorization", tc.authorization)
			}
			rec := httptest.NewRecorder()

			handler(rec, req)

			if rec.Code != tc.wantStatus {
				t.Errorf("status = %d, want %d", rec.Code, tc.wantStatus)
			}
			if nextCalls != tc.wantNextCalls {
				t.Errorf("next calls = %d, want %d", nextCalls, tc.wantNextCalls)
			}
			if got := rec.Header().Get("WWW-Authenticate"); got != tc.wantAuthenticate {
				t.Errorf("WWW-Authenticate = %q, want %q", got, tc.wantAuthenticate)
			}
		})
	}
}

// =============================================================================
// systemAPI — default factory behavior
// =============================================================================

func TestSystemAPI_DefaultNewServer_BuildsRealServerAndMux(t *testing.T) {
	srv := NewSystemAPI(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{}, time.Second)

	got, mux := srv.newServer(":12321")
	if mux == nil {
		t.Fatal("mux is nil")
	}
	real, ok := got.(*http.Server)
	if !ok {
		t.Fatalf("expected *http.Server, got %T", got)
	}
	if real.Addr != ":12321" {
		t.Errorf("Addr = %q, want :12321", real.Addr)
	}
	if real.Handler != mux {
		t.Error("Handler should be the returned mux")
	}
}

func TestSystemAPI_DefaultShutdown_DelegatesToServer(t *testing.T) {
	fakeSrv := newFakeHTTPProxyServer()
	srv := NewSystemAPI(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{}, time.Second)
	srv.server = fakeSrv

	if err := srv.shutdown(context.Background()); err != nil {
		t.Fatalf("shutdown: %v", err)
	}
	if fakeSrv.shutdownCalls.Load() != 1 {
		t.Fatalf("shutdown was not delegated, calls=%d", fakeSrv.shutdownCalls.Load())
	}
}

// =============================================================================
// systemAPI — Close
// =============================================================================

func TestSystemAPI_Close_InvokesShutdownWithDeadline(t *testing.T) {
	var gotCtx context.Context
	var calls int
	srv := NewSystemAPI(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{},
		50*time.Millisecond, func(s *systemAPI) {
			s.shutdown = func(ctx context.Context) error {
				gotCtx = ctx
				calls++
				return nil
			}
		})

	if err := srv.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	if calls != 1 {
		t.Fatalf("shutdown calls = %d, want 1", calls)
	}
	if _, ok := gotCtx.Deadline(); !ok {
		t.Error("Close should pass a deadline-bearing ctx to shutdown")
	}
}

// =============================================================================
// systemAPI — Run lifecycle
// =============================================================================

func TestSystemAPI_Run_AddrWithoutColonPrependsIt(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.SystemAPIReturns("12025")

	var capturedAddr string
	fakeSrv := newFakeHTTPProxyServer()
	srv := NewSystemAPI(env, &lbfakes.FakeOriginLoadBalancer{}, 50*time.Millisecond,
		func(s *systemAPI) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				capturedAddr = addr
				return fakeSrv, http.NewServeMux()
			}
		})
	defer srv.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if err := srv.Run(ctx); err != nil {
		t.Fatalf("Run: %v", err)
	}
	if capturedAddr != ":12025" {
		t.Fatalf("newServer addr = %q, want :12025", capturedAddr)
	}
}

func TestSystemAPI_Run_AddrWithColonUnchanged(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.SystemAPIReturns("127.0.0.1:9999")

	var capturedAddr string
	fakeSrv := newFakeHTTPProxyServer()
	srv := NewSystemAPI(env, &lbfakes.FakeOriginLoadBalancer{}, 50*time.Millisecond,
		func(s *systemAPI) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				capturedAddr = addr
				return fakeSrv, http.NewServeMux()
			}
		})
	defer srv.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if err := srv.Run(ctx); err != nil {
		t.Fatalf("Run: %v", err)
	}
	if capturedAddr != "127.0.0.1:9999" {
		t.Fatalf("newServer addr = %q", capturedAddr)
	}
}

func TestSystemAPI_Run_CtxCancelTriggersShutdown(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.SystemAPIReturns(":0")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	_, fakeSrv, _ := captureMuxFromSystemAPIRun(t, env, &lbfakes.FakeOriginLoadBalancer{}, ctx)

	deadline := time.Now().Add(time.Second)
	for fakeSrv.listenCalls.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if fakeSrv.listenCalls.Load() == 0 {
		t.Fatal("ListenAndServe goroutine did not start")
	}

	cancel()

	deadline = time.Now().Add(time.Second)
	for fakeSrv.shutdownCalls.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if fakeSrv.shutdownCalls.Load() == 0 {
		t.Fatal("Shutdown was not invoked after ctx cancel")
	}
}

// =============================================================================
// systemAPI — handler dispatch
// =============================================================================

func TestSystemAPI_Run_HandlerVersionsReturnsJSON(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.SystemAPIReturns(":0")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromSystemAPIRun(t, env, &lbfakes.FakeOriginLoadBalancer{}, ctx)

	req := httptest.NewRequest(http.MethodGet, "/api/v1/versions", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	var body map[string]string
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("json: %v\nbody=%s", err, rec.Body.String())
	}
	if body["signature"] == "" {
		t.Error("signature should be populated")
	}
	if body["version"] == "" {
		t.Error("version should be populated")
	}
}

func TestSystemAPI_Run_HandlerRegisterRequiresBearerToken(t *testing.T) {
	tests := []struct {
		name             string
		authorization    string
		wantStatus       int
		wantUpdateCalls  int
		wantAuthenticate string
	}{
		{
			name:             "missing authorization",
			wantStatus:       http.StatusUnauthorized,
			wantAuthenticate: "Bearer",
		},
		{
			name:             "wrong bearer token",
			authorization:    "Bearer wrong-token",
			wantStatus:       http.StatusUnauthorized,
			wantAuthenticate: "Bearer",
		},
		{
			name:            "correct bearer token",
			authorization:   "Bearer secret-token",
			wantStatus:      http.StatusOK,
			wantUpdateCalls: 1,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			environment := &envfakes.FakeProxyEnvironment{}
			environment.SystemAPIReturns(":0")
			environment.HttpAPIAuthEnabledReturns("on")
			environment.HttpAPIAuthTypeReturns("bearer")
			environment.HttpAPIAuthTokenReturns("secret-token")
			lbFake := &lbfakes.FakeOriginLoadBalancer{}

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			mux, _, _ := captureMuxFromSystemAPIRun(t, environment, lbFake, ctx)

			req := httptest.NewRequest(http.MethodPost, "/api/v1/srs/register", validRegisterBody(t))
			if tc.authorization != "" {
				req.Header.Set("Authorization", tc.authorization)
			}
			rec := httptest.NewRecorder()
			mux.ServeHTTP(rec, req)

			if rec.Code != tc.wantStatus {
				t.Errorf("status = %d, want %d", rec.Code, tc.wantStatus)
			}
			if got := lbFake.UpdateCallCount(); got != tc.wantUpdateCalls {
				t.Errorf("Update calls = %d, want %d", got, tc.wantUpdateCalls)
			}
			if got := rec.Header().Get("WWW-Authenticate"); got != tc.wantAuthenticate {
				t.Errorf("WWW-Authenticate = %q, want %q", got, tc.wantAuthenticate)
			}
		})
	}
}

// validRegisterBody returns the JSON body for a happy-path /api/v1/srs/register call.
func validRegisterBody(t *testing.T) io.Reader {
	t.Helper()
	b, err := json.Marshal(map[string]any{
		"ip":        "1.2.3.4",
		"server":    "srv-abc",
		"service":   "svc-1",
		"pid":       "12345",
		"rtmp":      []string{"1935"},
		"http":      []string{"8080"},
		"api":       []string{"1985"},
		"srt":       []string{"10080"},
		"rtc":       []string{"8000"},
		"device_id": "dev-x",
	})
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	return bytes.NewReader(b)
}

func TestSystemAPI_Run_HandlerRegisterHappyPathCallsUpdate(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.SystemAPIReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromSystemAPIRun(t, env, lbFake, ctx)

	req := httptest.NewRequest(http.MethodPost, "/api/v1/srs/register", validRegisterBody(t))
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if lbFake.UpdateCallCount() != 1 {
		t.Fatalf("Update calls = %d, want 1", lbFake.UpdateCallCount())
	}
	_, server := lbFake.UpdateArgsForCall(0)
	if server.IP != "1.2.3.4" {
		t.Errorf("IP = %q", server.IP)
	}
	if server.ServerID != "srv-abc" {
		t.Errorf("ServerID = %q", server.ServerID)
	}
	if server.ServiceID != "svc-1" {
		t.Errorf("ServiceID = %q", server.ServiceID)
	}
	if server.PID != "12345" {
		t.Errorf("PID = %q", server.PID)
	}
	if got := server.RTMP; len(got) != 1 || got[0] != "1935" {
		t.Errorf("RTMP = %v", got)
	}
	if got := server.HTTP; len(got) != 1 || got[0] != "8080" {
		t.Errorf("HTTP = %v", got)
	}
	if got := server.API; len(got) != 1 || got[0] != "1985" {
		t.Errorf("API = %v", got)
	}
	if got := server.SRT; len(got) != 1 || got[0] != "10080" {
		t.Errorf("SRT = %v", got)
	}
	if got := server.RTC; len(got) != 1 || got[0] != "8000" {
		t.Errorf("RTC = %v", got)
	}
	if server.DeviceID != "dev-x" {
		t.Errorf("DeviceID = %q", server.DeviceID)
	}
	if server.UpdatedAt.IsZero() {
		t.Error("UpdatedAt should be set")
	}
}

func TestSystemAPI_Run_HandlerRegisterParseBodyError(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.SystemAPIReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromSystemAPIRun(t, env, lbFake, ctx)

	req := httptest.NewRequest(http.MethodPost, "/api/v1/srs/register",
		bytes.NewReader([]byte("not json")))
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if lbFake.UpdateCallCount() != 0 {
		t.Fatalf("Update should not be called on parse body err, calls = %d", lbFake.UpdateCallCount())
	}
	if !bytes.Contains(rec.Body.Bytes(), []byte("parse body")) {
		t.Errorf("body = %q, expected parse body error", rec.Body.String())
	}
}

// registerWithField returns a body with one field replaced. Other mandatory
// fields default to valid values so only the field under test triggers an
// error.
func registerWithField(t *testing.T, field string, value any) io.Reader {
	t.Helper()
	m := map[string]any{
		"ip":      "1.2.3.4",
		"server":  "srv-abc",
		"service": "svc-1",
		"pid":     "12345",
		"rtmp":    []string{"1935"},
	}
	m[field] = value
	b, err := json.Marshal(m)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	return bytes.NewReader(b)
}

func TestSystemAPI_Run_HandlerRegisterValidationErrors(t *testing.T) {
	cases := []struct {
		name        string
		field       string
		value       any
		wantErrText string
	}{
		{"empty-ip", "ip", "", "empty ip"},
		{"empty-server", "server", "", "empty server"},
		{"empty-service", "service", "", "empty service"},
		{"empty-pid", "pid", "", "empty pid"},
		{"empty-rtmp", "rtmp", []string{}, "empty rtmp"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			env := &envfakes.FakeProxyEnvironment{}
			env.SystemAPIReturns(":0")
			lbFake := &lbfakes.FakeOriginLoadBalancer{}

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			mux, _, _ := captureMuxFromSystemAPIRun(t, env, lbFake, ctx)

			req := httptest.NewRequest(http.MethodPost, "/api/v1/srs/register",
				registerWithField(t, tc.field, tc.value))
			rec := httptest.NewRecorder()
			mux.ServeHTTP(rec, req)

			if lbFake.UpdateCallCount() != 0 {
				t.Errorf("Update should not be called when %s is invalid, calls = %d",
					tc.field, lbFake.UpdateCallCount())
			}
			if !bytes.Contains(rec.Body.Bytes(), []byte(tc.wantErrText)) {
				t.Errorf("body = %q, expected to contain %q", rec.Body.String(), tc.wantErrText)
			}
		})
	}
}

func TestSystemAPI_Run_HandlerRegisterLoadBalancerUpdateError(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.SystemAPIReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.UpdateReturns(errors.New("lb-update-fail"))

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromSystemAPIRun(t, env, lbFake, ctx)

	req := httptest.NewRequest(http.MethodPost, "/api/v1/srs/register", validRegisterBody(t))
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if lbFake.UpdateCallCount() != 1 {
		t.Fatalf("Update calls = %d, want 1", lbFake.UpdateCallCount())
	}
	if !bytes.Contains(rec.Body.Bytes(), []byte("lb-update-fail")) {
		t.Errorf("body = %q, expected lb error", rec.Body.String())
	}
}
