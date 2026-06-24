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
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	stdSync "sync"
	"sync/atomic"
	"testing"
	"time"

	"srsx/internal/env/envfakes"
	"srsx/internal/lb"
	"srsx/internal/lb/lbfakes"
)

// httptestHostPort splits an httptest.Server URL into host and port strings.
func httptestHostPort(t *testing.T, ts *httptest.Server) (string, string) {
	t.Helper()
	u, err := url.Parse(ts.URL)
	if err != nil {
		t.Fatalf("parse httptest URL %q: %v", ts.URL, err)
	}
	return u.Hostname(), u.Port()
}

// reservedClosedPort binds and immediately closes a TCP port, returning an
// address that is reliably refused for the lifetime of the test.
func reservedClosedPort(t *testing.T) (string, string) {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("reserve port: %v", err)
	}
	addr := l.Addr().(*net.TCPAddr)
	if err := l.Close(); err != nil {
		t.Fatalf("close listener: %v", err)
	}
	return addr.IP.String(), strconv.Itoa(addr.Port)
}

// =============================================================================
// newHLSPlayStream
// =============================================================================

func TestHLSPlayStream_New_DefaultsBuildBackendURL(t *testing.T) {
	v := newHLSPlayStream()
	if v.buildBackendURL == nil {
		t.Fatal("buildBackendURL should default to non-nil")
	}
	if got := v.buildBackendURL("1.2.3.4", 8080, "/live.ts"); got != "http://1.2.3.4:8080/live.ts" {
		t.Fatalf("default buildBackendURL produced %q", got)
	}
}

func TestHLSPlayStream_New_AppliesOpts(t *testing.T) {
	ctx := context.Background()
	lbStub := &lbfakes.FakeOriginLoadBalancer{}
	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.ctx = ctx
		s.loadBalancer = lbStub
		s.SRSProxyBackendHLSID = "spb-id"
		s.StreamURL = "vhost/app/stream"
		s.FullURL = "http://example.com/live.m3u8"
	})
	if v.ctx != ctx {
		t.Error("ctx not applied")
	}
	if v.loadBalancer != lbStub {
		t.Error("loadBalancer not applied")
	}
	if v.SRSProxyBackendHLSID != "spb-id" {
		t.Errorf("SRSProxyBackendHLSID = %q", v.SRSProxyBackendHLSID)
	}
	if v.StreamURL != "vhost/app/stream" {
		t.Errorf("StreamURL = %q", v.StreamURL)
	}
	if v.FullURL != "http://example.com/live.m3u8" {
		t.Errorf("FullURL = %q", v.FullURL)
	}
}

func TestHLSPlayStream_New_OptCanOverrideBuildBackendURL(t *testing.T) {
	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.buildBackendURL = func(string, int, string) string { return "custom" }
	})
	if got := v.buildBackendURL("", 0, ""); got != "custom" {
		t.Fatalf("override not applied: got %q", got)
	}
}

// =============================================================================
// Initialize
// =============================================================================

func TestHLSPlayStream_Initialize_SetsCtxWhenNil(t *testing.T) {
	v := newHLSPlayStream()
	ret := v.Initialize(context.Background())
	if v.ctx == nil {
		t.Fatal("Initialize should set v.ctx when nil")
	}
	if ret != lb.HLSPlayStream(v) {
		t.Fatal("Initialize should return v")
	}
}

func TestHLSPlayStream_Initialize_PreservesExistingCtx(t *testing.T) {
	type ctxKey struct{}
	existing := context.WithValue(context.Background(), ctxKey{}, "sentinel")
	v := newHLSPlayStream(func(s *hlsPlayStream) { s.ctx = existing })
	v.Initialize(context.Background())
	if got, _ := v.ctx.Value(ctxKey{}).(string); got != "sentinel" {
		t.Fatalf("Initialize should not replace existing ctx, value=%q", got)
	}
}

// =============================================================================
// GetSPBHID
// =============================================================================

func TestHLSPlayStream_GetSPBHID(t *testing.T) {
	v := newHLSPlayStream(func(s *hlsPlayStream) { s.SRSProxyBackendHLSID = "spb-xyz" })
	if v.GetSPBHID() != "spb-xyz" {
		t.Fatalf("GetSPBHID = %q", v.GetSPBHID())
	}
}

// =============================================================================
// ServeHTTP / serve / CORS
// =============================================================================

func TestHLSPlayStream_ServeHTTP_CORSPreflightShortCircuits(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.ctx = context.Background()
		s.loadBalancer = lbFake
	})
	req := httptest.NewRequest(http.MethodOptions, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	v.ServeHTTP(rec, req)
	if lbFake.PickCallCount() != 0 {
		t.Fatalf("Pick should not be called on CORS preflight, calls=%d", lbFake.PickCallCount())
	}
}

func TestHLSPlayStream_ServeHTTP_ErrorBranchInvokesApiError(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(nil, errors.New("pick-fail"))
	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.ctx = context.Background()
		s.loadBalancer = lbFake
		s.StreamURL = "vhost/app/stream"
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	v.ServeHTTP(rec, req)
	// ApiError writes a JSON error response. Verify the body is non-empty
	// and the status is not the default 200 (or that some response was made).
	if rec.Body.Len() == 0 {
		t.Fatal("ServeHTTP error branch should produce a response body")
	}
}

func TestHLSPlayStream_Serve_PickError(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(nil, errors.New("pick-fail"))
	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.ctx = context.Background()
		s.loadBalancer = lbFake
		s.StreamURL = "vhost/app/stream"
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	err := v.serve(v.ctx, rec, req)
	if err == nil || !strings.Contains(err.Error(), "pick backend for vhost/app/stream") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHLSPlayStream_Serve_WrapsServeByBackendError(t *testing.T) {
	// Backend with empty HTTP slice triggers serveByBackend's "no http server"
	// error, which serve() then wraps with "serve %v with %v by backend %+v".
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(&lb.OriginServer{IP: "127.0.0.1"}, nil)
	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.ctx = context.Background()
		s.loadBalancer = lbFake
		s.StreamURL = "vhost/app/stream"
		s.FullURL = "http://example.com/live.m3u8"
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	err := v.serve(v.ctx, rec, req)
	if err == nil || !strings.Contains(err.Error(), "serve http://example.com/live.m3u8 with vhost/app/stream by backend") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHLSPlayStream_Serve_HappyPathRewritesM3U8(t *testing.T) {
	m3u8 := "#EXTM3U\n#EXT-X-VERSION:3\nlive-0.ts\n"
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.WriteString(w, m3u8)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(&lb.OriginServer{IP: host, HTTP: []string{port}}, nil)

	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.ctx = context.Background()
		s.loadBalancer = lbFake
		s.StreamURL = "vhost/app/stream"
		s.FullURL = "http://example.com/live.m3u8"
		s.SRSProxyBackendHLSID = "spb-1"
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	if err := v.serve(v.ctx, rec, req); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if !strings.Contains(rec.Body.String(), "live-0.ts?spbhid=spb-1") {
		t.Fatalf("body missing spbhid rewrite: %q", rec.Body.String())
	}
}

// =============================================================================
// serveByBackend — error paths (no HTTP round-trip needed)
// =============================================================================

func TestHLSPlayStream_ServeByBackend_NoHTTPEndpoint(t *testing.T) {
	v := newHLSPlayStream()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req, &lb.OriginServer{IP: "127.0.0.1"})
	if err == nil || !strings.Contains(err.Error(), "no http server") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHLSPlayStream_ServeByBackend_BadPort(t *testing.T) {
	v := newHLSPlayStream()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: "127.0.0.1", HTTP: []string{"not-a-port"}})
	if err == nil || !strings.Contains(err.Error(), "parse http port not-a-port") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHLSPlayStream_ServeByBackend_RequestBuildError(t *testing.T) {
	v := newHLSPlayStream(func(s *hlsPlayStream) {
		s.buildBackendURL = func(string, int, string) string { return "://invalid-url" }
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: "127.0.0.1", HTTP: []string{"8080"}})
	if err == nil || !strings.Contains(err.Error(), "create request to ://invalid-url") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHLSPlayStream_ServeByBackend_DialError(t *testing.T) {
	host, port := reservedClosedPort(t)
	v := newHLSPlayStream()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}})
	if err == nil || !strings.HasSuffix(err.Error(), "EOF") {
		t.Fatalf("expected error suffixed with 'EOF', got: %v", err)
	}
}

// =============================================================================
// serveByBackend — HTTP round-trip via httptest.Server
// =============================================================================

func TestHLSPlayStream_ServeByBackend_NonOKStatus(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNotFound)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHLSPlayStream()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}})
	if err == nil || !strings.Contains(err.Error(), "status=404") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHLSPlayStream_ServeByBackend_TSPassthrough(t *testing.T) {
	payload := []byte{0x47, 0x00, 0x01, 0x02, 0x03, 0x04}
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "video/mp2t")
		_, _ = w.Write(payload)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHLSPlayStream()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.ts", nil)
	rec := httptest.NewRecorder()
	if err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}}); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if got := rec.Body.Bytes(); !bytes.Equal(got, payload) {
		t.Fatalf("body mismatch: got=%v want=%v", got, payload)
	}
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
}

func TestHLSPlayStream_ServeByBackend_M3U8RewritesTSWithoutQuery(t *testing.T) {
	m3u8 := "#EXTM3U\n#EXT-X-VERSION:3\nlive-0.ts\nlive-1.ts\n"
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.WriteString(w, m3u8)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHLSPlayStream(func(s *hlsPlayStream) { s.SRSProxyBackendHLSID = "ABC" })
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	if err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}}); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	body := rec.Body.String()
	for _, want := range []string{"live-0.ts?spbhid=ABC", "live-1.ts?spbhid=ABC"} {
		if !strings.Contains(body, want) {
			t.Fatalf("missing %q in body: %q", want, body)
		}
	}
}

func TestHLSPlayStream_ServeByBackend_M3U8RewritesTSWithQuery(t *testing.T) {
	m3u8 := "#EXTM3U\nlive-0.ts?token=foo\n"
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.WriteString(w, m3u8)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHLSPlayStream(func(s *hlsPlayStream) { s.SRSProxyBackendHLSID = "ABC" })
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	if err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}}); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if want := "live-0.ts?spbhid=ABC&&token=foo"; !strings.Contains(rec.Body.String(), want) {
		t.Fatalf("missing %q in body: %q", want, rec.Body.String())
	}
}

func TestHLSPlayStream_ServeByBackend_AppendsRawQueryOnTS(t *testing.T) {
	var seenURL string
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		seenURL = r.URL.String()
		w.WriteHeader(http.StatusOK)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHLSPlayStream()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.ts?token=foo", nil)
	rec := httptest.NewRecorder()
	if err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}}); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if !strings.Contains(seenURL, "token=foo") {
		t.Fatalf("backend should see raw query, got %q", seenURL)
	}
}

// =============================================================================
// httpFlvTsConnection
// =============================================================================

// =============================================================================
// newHTTPFlvTsConnection
// =============================================================================

func TestHTTPFlvTsConn_New_DefaultsBuildBackendURL(t *testing.T) {
	v := newHTTPFlvTsConnection()
	if v.buildBackendURL == nil {
		t.Fatal("buildBackendURL should default to non-nil")
	}
	if got := v.buildBackendURL("1.2.3.4", 8080, "/live.flv"); got != "http://1.2.3.4:8080/live.flv" {
		t.Fatalf("default buildBackendURL produced %q", got)
	}
}

func TestHTTPFlvTsConn_New_AppliesOpts(t *testing.T) {
	ctx := context.Background()
	lbStub := &lbfakes.FakeOriginLoadBalancer{}
	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.ctx = ctx
		c.loadBalancer = lbStub
	})
	if v.ctx != ctx {
		t.Error("ctx not applied")
	}
	if v.loadBalancer != lbStub {
		t.Error("loadBalancer not applied")
	}
}

func TestHTTPFlvTsConn_New_OptCanOverrideBuildBackendURL(t *testing.T) {
	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.buildBackendURL = func(string, int, string) string { return "custom" }
	})
	if got := v.buildBackendURL("", 0, ""); got != "custom" {
		t.Fatalf("override not applied: got %q", got)
	}
}

// =============================================================================
// ServeHTTP / serve / CORS
// =============================================================================

func TestHTTPFlvTsConn_ServeHTTP_CORSPreflightShortCircuits(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.ctx = context.Background()
		c.loadBalancer = lbFake
	})
	req := httptest.NewRequest(http.MethodOptions, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	v.ServeHTTP(rec, req)
	if lbFake.PickCallCount() != 0 {
		t.Fatalf("Pick should not be called on CORS preflight, calls=%d", lbFake.PickCallCount())
	}
}

func TestHTTPFlvTsConn_ServeHTTP_ErrorBranchInvokesApiError(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(nil, errors.New("pick-fail"))
	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.ctx = context.Background()
		c.loadBalancer = lbFake
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	v.ServeHTTP(rec, req)
	if rec.Body.Len() == 0 {
		t.Fatal("ServeHTTP error branch should produce a response body")
	}
}

func TestHTTPFlvTsConn_Serve_PickError(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(nil, errors.New("pick-fail"))
	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.ctx = context.Background()
		c.loadBalancer = lbFake
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live/stream.flv", nil)
	rec := httptest.NewRecorder()
	err := v.serve(context.Background(), rec, req)
	if err == nil || !strings.Contains(err.Error(), "pick backend for") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHTTPFlvTsConn_Serve_WrapsServeByBackendError(t *testing.T) {
	// Empty HTTP slice on backend triggers serveByBackend's "no http stream
	// server" error, which serve() wraps with "serve <fullURL> with <streamURL>".
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(&lb.OriginServer{IP: "127.0.0.1"}, nil)
	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.ctx = context.Background()
		c.loadBalancer = lbFake
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live/stream.flv", nil)
	rec := httptest.NewRecorder()
	err := v.serve(context.Background(), rec, req)
	if err == nil || !strings.Contains(err.Error(), "serve ") || !strings.Contains(err.Error(), " by backend ") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHTTPFlvTsConn_Serve_HappyPath(t *testing.T) {
	payload := []byte{0x46, 0x4c, 0x56, 0x01} // "FLV\x01"
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write(payload)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.PickReturns(&lb.OriginServer{IP: host, HTTP: []string{port}}, nil)

	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.ctx = context.Background()
		c.loadBalancer = lbFake
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live/stream.flv", nil)
	rec := httptest.NewRecorder()
	if err := v.serve(context.Background(), rec, req); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if !bytes.Equal(rec.Body.Bytes(), payload) {
		t.Fatalf("body mismatch: got=%v want=%v", rec.Body.Bytes(), payload)
	}
}

// =============================================================================
// serveByBackend — error paths
// =============================================================================

func TestHTTPFlvTsConn_ServeByBackend_NoHTTPEndpoint(t *testing.T) {
	v := newHTTPFlvTsConnection()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req, &lb.OriginServer{IP: "127.0.0.1"})
	if err == nil || !strings.Contains(err.Error(), "no http stream server") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHTTPFlvTsConn_ServeByBackend_BadPort(t *testing.T) {
	v := newHTTPFlvTsConnection()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: "127.0.0.1", HTTP: []string{"not-a-port"}})
	if err == nil || !strings.Contains(err.Error(), "parse http port not-a-port") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHTTPFlvTsConn_ServeByBackend_RequestBuildError(t *testing.T) {
	v := newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
		c.buildBackendURL = func(string, int, string) string { return "://invalid-url" }
	})
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: "127.0.0.1", HTTP: []string{"8080"}})
	if err == nil || !strings.Contains(err.Error(), "create request to ://invalid-url") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHTTPFlvTsConn_ServeByBackend_DialError(t *testing.T) {
	host, port := reservedClosedPort(t)
	v := newHTTPFlvTsConnection()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}})
	if err == nil || !strings.Contains(err.Error(), "do request to") {
		t.Fatalf("unexpected err: %v", err)
	}
}

// =============================================================================
// serveByBackend — HTTP round-trip
// =============================================================================

func TestHTTPFlvTsConn_ServeByBackend_NonOKStatus(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNotFound)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHTTPFlvTsConnection()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}})
	if err == nil || !strings.Contains(err.Error(), "status=404") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHTTPFlvTsConn_ServeByBackend_BodyPassthrough(t *testing.T) {
	payload := []byte{0x46, 0x4c, 0x56, 0x01, 0x05, 0x00, 0x00, 0x00}
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write(payload)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHTTPFlvTsConnection()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	if err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}}); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if !bytes.Equal(rec.Body.Bytes(), payload) {
		t.Fatalf("body mismatch: got=%v want=%v", rec.Body.Bytes(), payload)
	}
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
}

func TestHTTPFlvTsConn_ServeByBackend_DropsRawQuery(t *testing.T) {
	// Unlike hlsPlayStream.serveByBackend, the FLV/TS path forwards only
	// r.URL.Path — it does NOT append RawQuery to the backend request.
	var seenRawQuery string
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		seenRawQuery = r.URL.RawQuery
		w.WriteHeader(http.StatusOK)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHTTPFlvTsConnection()
	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.flv?token=foo", nil)
	rec := httptest.NewRecorder()
	if err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}}); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if seenRawQuery != "" {
		t.Fatalf("backend should NOT see raw query, got %q", seenRawQuery)
	}
}

func TestHTTPFlvTsConn_ServeByBackend_PreservesMethod(t *testing.T) {
	var seenMethod string
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		seenMethod = r.Method
		w.WriteHeader(http.StatusOK)
	}))
	defer ts.Close()
	host, port := httptestHostPort(t, ts)

	v := newHTTPFlvTsConnection()
	req := httptest.NewRequest(http.MethodHead, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	if err := v.serveByBackend(context.Background(), rec, req,
		&lb.OriginServer{IP: host, HTTP: []string{port}}); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if seenMethod != http.MethodHead {
		t.Fatalf("backend method = %q, want HEAD", seenMethod)
	}
}

// =============================================================================
// httpStreamProxyServer
// =============================================================================

// fakeHTTPProxyServer is an httpServer that blocks in ListenAndServe until
// Shutdown is called. Used to drive Run()'s lifecycle without binding a port.
type fakeHTTPProxyServer struct {
	listenCalls    atomic.Int32
	shutdownCalls  atomic.Int32
	listenReturn   error
	shutdownReturn error
	block          chan struct{}
	once           stdSync.Once
}

func newFakeHTTPProxyServer() *fakeHTTPProxyServer {
	return &fakeHTTPProxyServer{
		listenReturn: http.ErrServerClosed,
		block:        make(chan struct{}),
	}
}

func (f *fakeHTTPProxyServer) ListenAndServe() error {
	f.listenCalls.Add(1)
	<-f.block
	return f.listenReturn
}

func (f *fakeHTTPProxyServer) Shutdown(ctx context.Context) error {
	f.shutdownCalls.Add(1)
	f.once.Do(func() { close(f.block) })
	return f.shutdownReturn
}

// captureMuxFromRun calls Run with a fake server that captures the registered
// mux. Returns the mux and the fake server for further assertions. Caller is
// responsible for cancelling ctx to trigger shutdown.
func captureMuxFromRun(t *testing.T, env *envfakes.FakeProxyEnvironment,
	lbFake *lbfakes.FakeOriginLoadBalancer, ctx context.Context,
	opts ...func(*httpStreamProxyServer)) (*http.ServeMux, *fakeHTTPProxyServer, *httpStreamProxyServer) {
	t.Helper()

	fakeSrv := newFakeHTTPProxyServer()
	var capturedMux *http.ServeMux

	baseOpts := []func(*httpStreamProxyServer){
		func(s *httpStreamProxyServer) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				mux := http.NewServeMux()
				capturedMux = mux
				return fakeSrv, mux
			}
		},
	}
	srvIface := NewHTTPStreamProxyServer(env, lbFake, 50*time.Millisecond, append(baseOpts, opts...)...)
	srv := srvIface.(*httpStreamProxyServer)

	if err := srv.Run(ctx); err != nil {
		t.Fatalf("Run: %v", err)
	}
	if capturedMux == nil {
		t.Fatal("newServer was not called by Run")
	}
	return capturedMux, fakeSrv, srv
}

// =============================================================================
// NewHTTPStreamProxyServer
// =============================================================================

func TestHTTPStreamProxyServer_New_StoresFieldsAndDefaultsSeams(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	timeout := 2 * time.Second
	srv := NewHTTPStreamProxyServer(env, lbFake, timeout).(*httpStreamProxyServer)

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
	if srv.newHLSStream == nil {
		t.Error("newHLSStream seam should default to non-nil")
	}
	if srv.newFlvTsConn == nil {
		t.Error("newFlvTsConn seam should default to non-nil")
	}
}

func TestHTTPStreamProxyServer_New_AppliesOpts(t *testing.T) {
	var optCalled bool
	srv := NewHTTPStreamProxyServer(
		&envfakes.FakeProxyEnvironment{},
		&lbfakes.FakeOriginLoadBalancer{},
		time.Second,
		func(s *httpStreamProxyServer) { optCalled = true },
	).(*httpStreamProxyServer)
	if !optCalled {
		t.Fatal("opt was not invoked")
	}
	if srv.shutdown == nil {
		t.Error("default seams should still be set when opts don't override them")
	}
}

func TestHTTPStreamProxyServer_New_OptCanOverrideAllSeams(t *testing.T) {
	customShutdown := func(context.Context) error { return errors.New("custom") }
	customNewServer := func(string) (httpServer, *http.ServeMux) { return nil, nil }
	customNewHLS := func(string, string) *hlsPlayStream { return nil }
	customNewFlv := func(context.Context) *httpFlvTsConnection { return nil }

	srv := NewHTTPStreamProxyServer(
		&envfakes.FakeProxyEnvironment{},
		&lbfakes.FakeOriginLoadBalancer{},
		time.Second,
		func(s *httpStreamProxyServer) {
			s.shutdown = customShutdown
			s.newServer = customNewServer
			s.newHLSStream = customNewHLS
			s.newFlvTsConn = customNewFlv
		},
	).(*httpStreamProxyServer)

	if err := srv.shutdown(context.Background()); err == nil || err.Error() != "custom" {
		t.Errorf("custom shutdown not applied: %v", err)
	}
	// Pointer comparison on func values isn't supported by ==; call them and
	// check the override took effect via observable behavior.
	if got, _ := srv.newServer(""); got != nil {
		t.Error("custom newServer not applied")
	}
	if srv.newHLSStream("", "") != nil {
		t.Error("custom newHLSStream not applied")
	}
	if srv.newFlvTsConn(context.Background()) != nil {
		t.Error("custom newFlvTsConn not applied")
	}
}

// =============================================================================
// Default factory behavior
// =============================================================================

func TestHTTPStreamProxyServer_DefaultNewServer_BuildsRealServerAndMux(t *testing.T) {
	srv := NewHTTPStreamProxyServer(&envfakes.FakeProxyEnvironment{},
		&lbfakes.FakeOriginLoadBalancer{}, time.Second).(*httpStreamProxyServer)

	got, mux := srv.newServer(":12345")
	if mux == nil {
		t.Fatal("mux is nil")
	}
	real, ok := got.(*http.Server)
	if !ok {
		t.Fatalf("expected *http.Server, got %T", got)
	}
	if real.Addr != ":12345" {
		t.Errorf("Addr = %q, want :12345", real.Addr)
	}
	if real.Handler != mux {
		t.Error("Handler should be the returned mux")
	}
}

func TestHTTPStreamProxyServer_DefaultNewHLSStream_WiresFields(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	srv := NewHTTPStreamProxyServer(&envfakes.FakeProxyEnvironment{}, lbFake,
		time.Second).(*httpStreamProxyServer)

	got := srv.newHLSStream("vhost/app/stream", "http://example.com/live.m3u8")
	if got.loadBalancer != lbFake {
		t.Error("loadBalancer not wired")
	}
	if got.StreamURL != "vhost/app/stream" {
		t.Errorf("StreamURL = %q", got.StreamURL)
	}
	if got.FullURL != "http://example.com/live.m3u8" {
		t.Errorf("FullURL = %q", got.FullURL)
	}
	if got.SRSProxyBackendHLSID == "" {
		t.Error("SRSProxyBackendHLSID should be auto-generated")
	}
	if got.buildBackendURL == nil {
		t.Error("buildBackendURL default should be propagated")
	}
}

func TestHTTPStreamProxyServer_DefaultNewFlvTsConn_WiresFields(t *testing.T) {
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	srv := NewHTTPStreamProxyServer(&envfakes.FakeProxyEnvironment{}, lbFake,
		time.Second).(*httpStreamProxyServer)

	type ctxKey struct{}
	ctx := context.WithValue(context.Background(), ctxKey{}, "sentinel")
	got := srv.newFlvTsConn(ctx)
	if got.ctx != ctx {
		t.Error("ctx not wired")
	}
	if got.loadBalancer != lbFake {
		t.Error("loadBalancer not wired")
	}
}

func TestHTTPStreamProxyServer_DefaultShutdown_DelegatesToServer(t *testing.T) {
	fakeSrv := newFakeHTTPProxyServer()
	srv := NewHTTPStreamProxyServer(&envfakes.FakeProxyEnvironment{},
		&lbfakes.FakeOriginLoadBalancer{}, time.Second).(*httpStreamProxyServer)
	srv.server = fakeSrv // simulate what Run() would assign
	if err := srv.shutdown(context.Background()); err != nil {
		t.Fatalf("shutdown: %v", err)
	}
	if fakeSrv.shutdownCalls.Load() != 1 {
		t.Fatalf("shutdown was not delegated to server, calls=%d", fakeSrv.shutdownCalls.Load())
	}
}

// =============================================================================
// Close
// =============================================================================

func TestHTTPStreamProxyServer_Close_InvokesShutdownWithDeadline(t *testing.T) {
	var gotCtx context.Context
	var calls int
	srv := NewHTTPStreamProxyServer(
		&envfakes.FakeProxyEnvironment{},
		&lbfakes.FakeOriginLoadBalancer{},
		50*time.Millisecond,
		func(s *httpStreamProxyServer) {
			s.shutdown = func(ctx context.Context) error {
				gotCtx = ctx
				calls++
				return nil
			}
		},
	).(*httpStreamProxyServer)

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
// Run — lifecycle
// =============================================================================

func TestHTTPStreamProxyServer_Run_AddrWithoutColonPrependsIt(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns("8080")

	var capturedAddr string
	fakeSrv := newFakeHTTPProxyServer()
	srvIface := NewHTTPStreamProxyServer(env, &lbfakes.FakeOriginLoadBalancer{},
		50*time.Millisecond, func(s *httpStreamProxyServer) {
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
	if capturedAddr != ":8080" {
		t.Fatalf("newServer addr = %q, want :8080", capturedAddr)
	}
}

func TestHTTPStreamProxyServer_Run_AddrWithColonUnchanged(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns("127.0.0.1:9999")

	var capturedAddr string
	fakeSrv := newFakeHTTPProxyServer()
	srvIface := NewHTTPStreamProxyServer(env, &lbfakes.FakeOriginLoadBalancer{},
		50*time.Millisecond, func(s *httpStreamProxyServer) {
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

func TestHTTPStreamProxyServer_Run_StaticFilesInvalidPath(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	env.StaticFilesReturns("/no/such/path/exists/__srsbot_test__")

	fakeSrv := newFakeHTTPProxyServer()
	srvIface := NewHTTPStreamProxyServer(env, &lbfakes.FakeOriginLoadBalancer{},
		50*time.Millisecond, func(s *httpStreamProxyServer) {
			s.newServer = func(addr string) (httpServer, *http.ServeMux) {
				return fakeSrv, http.NewServeMux()
			}
		})

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	err := srvIface.Run(ctx)
	if err == nil || !strings.Contains(err.Error(), "invalid static files") {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestHTTPStreamProxyServer_Run_CtxCancelTriggersShutdown(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	_, fakeSrv, _ := captureMuxFromRun(t, env, &lbfakes.FakeOriginLoadBalancer{}, ctx)

	// Wait briefly for ListenAndServe goroutine to be running.
	deadline := time.Now().Add(time.Second)
	for fakeSrv.listenCalls.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if fakeSrv.listenCalls.Load() == 0 {
		t.Fatal("ListenAndServe goroutine did not start")
	}

	cancel()

	// Wait for Shutdown to be invoked by the watcher goroutine.
	deadline = time.Now().Add(time.Second)
	for fakeSrv.shutdownCalls.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if fakeSrv.shutdownCalls.Load() == 0 {
		t.Fatal("Shutdown was not invoked after ctx cancel")
	}
}

// =============================================================================
// Run — handler dispatch
// =============================================================================

func TestHTTPStreamProxyServer_Run_HandlerVersionsReturnsJSON(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, &lbfakes.FakeOriginLoadBalancer{}, ctx)

	req := httptest.NewRequest(http.MethodGet, "/api/v1/versions", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	var body struct {
		Code int    `json:"code"`
		PID  string `json:"pid"`
		Data struct {
			Major    int    `json:"major"`
			Minor    int    `json:"minor"`
			Revision int    `json:"revision"`
			Version  string `json:"version"`
		} `json:"data"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("json: %v\nbody=%s", err, rec.Body.String())
	}
	if body.Code != 0 {
		t.Errorf("Code = %d, want 0", body.Code)
	}
	if body.PID == "" {
		t.Error("PID should be populated")
	}
	if body.Data.Version == "" {
		t.Error("Version should be populated")
	}
}

func TestHTTPStreamProxyServer_Run_HandlerM3U8InvokesNewHLSStream(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}

	// Make LoadOrStoreHLS return whatever was passed in (the stream from newHLSStream).
	lbFake.LoadOrStoreHLSStub = func(_ context.Context, _ string, s lb.HLSPlayStream) (lb.HLSPlayStream, error) {
		return s, nil
	}

	var capturedStreamURL, capturedFullURL string
	var newHLSCalls int

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, lbFake, ctx, func(s *httpStreamProxyServer) {
		// Wrap default newHLSStream to capture args, but return a real
		// hlsPlayStream so the .(*hlsPlayStream) cast inside Run's handler works.
		// The returned stream has a fake loadBalancer; ServeHTTP will short-circuit
		// on the OPTIONS preflight we send below.
		s.newHLSStream = func(streamURL, fullURL string) *hlsPlayStream {
			newHLSCalls++
			capturedStreamURL, capturedFullURL = streamURL, fullURL
			return newHLSPlayStream(func(h *hlsPlayStream) {
				h.loadBalancer = lbFake
				h.StreamURL, h.FullURL = streamURL, fullURL
			})
		}
	})

	req := httptest.NewRequest(http.MethodOptions, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if newHLSCalls != 1 {
		t.Fatalf("newHLSStream calls = %d, want 1", newHLSCalls)
	}
	if !strings.HasSuffix(capturedStreamURL, "/live") {
		t.Errorf("captured streamURL %q should end with /live", capturedStreamURL)
	}
	if !strings.Contains(capturedFullURL, "live.m3u8") {
		t.Errorf("captured fullURL %q should contain live.m3u8", capturedFullURL)
	}
}

func TestHTTPStreamProxyServer_Run_HandlerM3U8LoadOrStoreErrorReturns400(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.LoadOrStoreHLSReturns(nil, errors.New("redis down"))

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, lbFake, ctx)

	req := httptest.NewRequest(http.MethodGet, "http://example.com/live.m3u8", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want 400; body=%s", rec.Code, rec.Body.String())
	}
	if !strings.Contains(rec.Body.String(), "load or store hls") {
		t.Errorf("body should mention 'load or store hls', got %q", rec.Body.String())
	}
}

func TestHTTPStreamProxyServer_Run_HandlerFlvInvokesNewFlvTsConn(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}

	var newFlvCalls int
	var capturedCtx context.Context

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, lbFake, ctx, func(s *httpStreamProxyServer) {
		s.newFlvTsConn = func(reqCtx context.Context) *httpFlvTsConnection {
			newFlvCalls++
			capturedCtx = reqCtx
			return newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
				c.ctx = reqCtx
				c.loadBalancer = lbFake
			})
		}
	})

	req := httptest.NewRequest(http.MethodOptions, "http://example.com/live.flv", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if newFlvCalls != 1 {
		t.Fatalf("newFlvTsConn calls = %d, want 1", newFlvCalls)
	}
	if capturedCtx == nil {
		t.Error("captured ctx should be non-nil")
	}
}

func TestHTTPStreamProxyServer_Run_HandlerTsInvokesNewFlvTsConn(t *testing.T) {
	// Same dispatch as .flv but for .ts (without spbhid).
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}

	var newFlvCalls int
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, lbFake, ctx, func(s *httpStreamProxyServer) {
		s.newFlvTsConn = func(reqCtx context.Context) *httpFlvTsConnection {
			newFlvCalls++
			return newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
				c.ctx = reqCtx
				c.loadBalancer = lbFake
			})
		}
	})

	req := httptest.NewRequest(http.MethodOptions, "http://example.com/live.ts", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if newFlvCalls != 1 {
		t.Fatalf("newFlvTsConn calls = %d, want 1", newFlvCalls)
	}
}

func TestHTTPStreamProxyServer_Run_HandlerTsWithSPBHIDLoadsByID(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}

	stub := newHLSPlayStream(func(h *hlsPlayStream) {
		h.loadBalancer = lbFake
		h.SRSProxyBackendHLSID = "ABC"
	})
	lbFake.LoadHLSBySPBHIDReturns(stub, nil)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, lbFake, ctx)

	req := httptest.NewRequest(http.MethodOptions, "http://example.com/live-0.ts?spbhid=ABC", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if lbFake.LoadHLSBySPBHIDCallCount() != 1 {
		t.Fatalf("LoadHLSBySPBHID calls = %d, want 1", lbFake.LoadHLSBySPBHIDCallCount())
	}
	_, gotID := lbFake.LoadHLSBySPBHIDArgsForCall(0)
	if gotID != "ABC" {
		t.Errorf("LoadHLSBySPBHID id = %q, want ABC", gotID)
	}
}

func TestHTTPStreamProxyServer_Run_HandlerTsWithSPBHIDErrorReturns400(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	lbFake := &lbfakes.FakeOriginLoadBalancer{}
	lbFake.LoadHLSBySPBHIDReturns(nil, errors.New("not found"))

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, lbFake, ctx)

	req := httptest.NewRequest(http.MethodGet, "http://example.com/live-0.ts?spbhid=missing", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want 400; body=%s", rec.Code, rec.Body.String())
	}
}

func TestHTTPStreamProxyServer_Run_HandlerUnmatchedReturns404(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	// StaticFiles unset, no .m3u8/.flv/.ts suffix → 404.

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, &lbfakes.FakeOriginLoadBalancer{}, ctx)

	req := httptest.NewRequest(http.MethodGet, "http://example.com/random/path", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Fatalf("status = %d, want 404", rec.Code)
	}
}

func TestHTTPStreamProxyServer_Run_HandlerServesStaticFiles(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "hello.txt"), []byte("hi"), 0644); err != nil {
		t.Fatalf("write: %v", err)
	}

	env := &envfakes.FakeProxyEnvironment{}
	env.HttpServerReturns(":0")
	env.StaticFilesReturns(dir)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	mux, _, _ := captureMuxFromRun(t, env, &lbfakes.FakeOriginLoadBalancer{}, ctx)

	req := httptest.NewRequest(http.MethodGet, "http://example.com/hello.txt", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	if rec.Body.String() != "hi" {
		t.Errorf("body = %q, want hi", rec.Body.String())
	}
}
