// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"srsx/internal/lb"
)

func cachedRTC(t *testing.T, f *webRTCFixture, name string) (*rtcConnection, *fakeBackendUDP) {
	t.Helper()
	backend := newFakeBackendUDP()
	c := newRTCConnection(func(c *rtcConnection) {
		c.Ufrag, c.StreamURL = name, "vhost/live/shared"
		c.loadBalancer = f.lb
		c.origin = &lb.OriginServer{IP: "127.0.0.1", RTC: []string{"8000"}}
		c.dialBackendUDP = func(context.Context, string, int) (io.ReadWriteCloser, error) { return backend, nil }
	})
	c.Initialize(context.Background(), f.listener)
	f.server.usernames.Store(name, c)
	t.Cleanup(c.Close)
	return c, backend
}
func waitReader(t *testing.T, c *rtcConnection) {
	t.Helper()
	c.mu.Lock()
	done := c.readerDone
	c.mu.Unlock()
	if done == nil {
		return
	}
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("backend reader retained after close")
	}
}
func assertEmptyRTC(t *testing.T, f *webRTCFixture) {
	t.Helper()
	f.server.usernames.Range(func(k string, c *rtcConnection) bool { t.Errorf("retained username %s", k); return true })
	f.server.addresses.Range(func(k string, c *rtcConnection) bool { t.Errorf("retained address %s", k); return true })
	if len(f.server.resources) != 0 {
		t.Fatal("retained resource mapping")
	}
}
func TestRTCSessionIdleCleanup(t *testing.T) {
	f := newWebRTCFixture()
	idle, b := cachedRTC(t, f, "idle")
	active, _ := cachedRTC(t, f, "active")
	unused, _ := cachedRTC(t, f, "unused")
	addr := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 9000}
	if err := f.server.handleClientUDP(context.Background(), addr, newStunBindingRequest(idle.Ufrag)); err != nil {
		t.Fatal(err)
	}
	<-b.writes
	// Server-to-client video is not proof that the client is still alive.
	b.reads <- []byte("video")
	<-f.listener.writes
	now := time.Now()
	idle.mu.Lock()
	idle.lastClientPacket = now.Add(-lb.RTCAliveDuration)
	idle.mu.Unlock()
	unused.mu.Lock()
	unused.lastClientPacket = now.Add(-lb.RTCAliveDuration)
	unused.mu.Unlock()
	f.server.sweep(now)
	if !idle.isClosed() || !unused.isClosed() || active.isClosed() {
		t.Fatal("incorrect idle/active selection")
	}
	if !b.closed.Load() {
		t.Fatal("socket leaked")
	}
	waitReader(t, idle)
	if _, ok := f.server.addresses.Load(addr.String()); ok {
		t.Fatal("address retained")
	}
	active.Close()
	f.server.sweep(now)
	assertEmptyRTC(t, f)
}
func TestRTCSessionAddressRebindAndReuse(t *testing.T) {
	f := newWebRTCFixture()
	old, ob := cachedRTC(t, f, "old")
	fresh, fb := cachedRTC(t, f, "fresh")
	a := &net.UDPAddr{Port: 9000}
	b := &net.UDPAddr{Port: 9001}
	for _, addr := range []*net.UDPAddr{a, b} {
		if err := f.server.handleClientUDP(context.Background(), addr, newStunBindingRequest(old.Ufrag)); err != nil {
			t.Fatal(err)
		}
		<-ob.writes
	}
	if _, ok := f.server.addresses.Load(a.String()); ok {
		t.Fatal("old NAT address retained")
	}
	if err := f.server.handleClientUDP(context.Background(), b, newStunBindingRequest(fresh.Ufrag)); err != nil {
		t.Fatal(err)
	}
	<-fb.writes
	old.Close()
	f.server.sweep(time.Now())
	waitReader(t, old)
	if got, ok := f.server.addresses.Load(b.String()); !ok || got != fresh {
		t.Fatal("old cleanup deleted replacement")
	}
	if err := f.server.handleClientUDP(context.Background(), b, fakeRTPPacket()); err != nil {
		t.Fatal(err)
	}
	<-fb.writes
	fresh.Close()
	f.server.sweep(time.Now())
	waitReader(t, fresh)
	assertEmptyRTC(t, f)
}
func TestRTCSessionDelete204ExactAndPinned(t *testing.T) {
	f := newWebRTCFixture()
	defer f.server.Close()
	old, _ := cachedRTC(t, f, "old")
	other, _ := cachedRTC(t, f, "other")
	target, _ := url.Parse("http://example.com/rtc/v1/whep/?session=first&token=secret&app=live&stream=shared")
	otherURL, _ := url.Parse("http://example.com/rtc/v1/whep/?session=second&token=secret&app=live&stream=shared")
	f.server.resources[rtcResourceKey(target)] = old
	f.server.resources[rtcResourceKey(otherURL)] = other
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "DELETE" {
			t.Error("wrong method")
		}
		w.WriteHeader(204)
	}))
	defer backend.Close()
	f.server.backendURL = func(origin *lb.OriginServer, r *http.Request) (string, error) {
		if origin != old.origin {
			t.Error("DELETE used replacement Origin")
		}
		return backend.URL, nil
	}
	// Query order must not change resource identity.
	req := httptest.NewRequest("DELETE", "http://example.com/rtc/v1/whep/?app=live&stream=shared&token=secret&session=first", nil)
	w := httptest.NewRecorder()
	if err := f.server.proxyApiToBackend(context.Background(), w, req, &lb.OriginServer{}, "", "vhost/live/shared"); err != nil {
		t.Fatal(err)
	}
	if w.Code != 204 || !old.isClosed() || other.isClosed() {
		t.Fatal("DELETE must close only exact session")
	}
	if _, ok := f.server.resources[rtcResourceKey(target)]; ok {
		t.Fatal("deleted resource retained")
	}
}
func TestRTCSessionHTTPRequestCancellation(t *testing.T) {
	f := newWebRTCFixture()
	defer f.server.Close()
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprint(w, "v=0\r\na=ice-ufrag:local\r\na=ice-pwd:local-password\r\n")
	}))
	defer backend.Close()
	f.server.backendURL = func(*lb.OriginServer, *http.Request) (string, error) { return backend.URL, nil }
	ctx, cancel := context.WithCancel(context.Background())
	req := httptest.NewRequest("POST", "http://example.com/rtc/v1/whep/?app=live&stream=shared", nil)
	if err := f.server.proxyApiToBackend(ctx, httptest.NewRecorder(), req, &lb.OriginServer{}, "v=0\r\na=ice-ufrag:remote\r\na=ice-pwd:remote-password\r\n", "shared"); err != nil {
		t.Fatal(err)
	}
	cancel()
	c, ok := f.server.usernames.Load("local:remote")
	if !ok {
		t.Fatal("missing negotiated session")
	}
	if c.ctx.Err() != nil {
		t.Fatal("HTTP cancellation canceled media lifetime")
	}
}
func TestRTCSessionChurnRealSockets(t *testing.T) {
	f := newWebRTCFixture()
	defer f.server.Close()
	origin, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer origin.Close()
	baseline := runtime.NumGoroutine()
	for batch := 0; batch < 10; batch++ {
		var connections []*rtcConnection
		for i := 0; i < 50; i++ {
			c := newRTCConnection(func(c *rtcConnection) {
				c.Ufrag = fmt.Sprintf("%d-%d", batch, i)
				c.StreamURL = "shared"
				c.loadBalancer = f.lb
				c.origin = &lb.OriginServer{IP: "127.0.0.1", RTC: []string{strconv.Itoa(origin.LocalAddr().(*net.UDPAddr).Port)}}
			})
			c.Initialize(context.Background(), f.listener)
			f.server.usernames.Store(c.Ufrag, c)
			if err := f.server.handleClientUDP(context.Background(), &net.UDPAddr{Port: 10000 + i}, newStunBindingRequest(c.Ufrag)); err != nil {
				t.Fatal(err)
			}
			connections = append(connections, c)
		}
		f.server.sweep(time.Now().Add(lb.RTCAliveDuration + time.Second))
		for _, c := range connections {
			waitReader(t, c)
			if !c.isClosed() {
				t.Fatal("session survived churn")
			}
		}
		assertEmptyRTC(t, f)
	}
	runtime.Gosched()
	if n := runtime.NumGoroutine(); n > baseline+3 {
		t.Fatalf("goroutines grew from %d to %d", baseline, n)
	}
}
func TestRTCSessionConcurrentExpiry(t *testing.T) {
	f := newWebRTCFixture()
	c, b := cachedRTC(t, f, "race")
	go func() {
		for range b.writes {
		}
	}() // continuously drain fake writes
	var wg sync.WaitGroup
	for i := 0; i < 3; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				_ = f.server.handleClientUDP(context.Background(), &net.UDPAddr{Port: 9000 + j}, newStunBindingRequest(c.Ufrag))
			}
		}()
	}
	wg.Add(1)
	go func() {
		defer wg.Done()
		for j := 0; j < 100; j++ {
			f.server.sweep(time.Now().Add(3 * time.Minute))
		}
	}()
	wg.Wait()
	f.server.Close()
	waitReader(t, c)
	close(b.writes)
	assertEmptyRTC(t, f)
}
func TestRTCSessionReadErrorClosesSocket(t *testing.T) {
	f := newWebRTCFixture()
	c, b := cachedRTC(t, f, "failed")
	b.readErr = io.EOF
	if err := c.HandlePacket(&net.UDPAddr{Port: 9000}, []byte("start")); err != nil && !strings.Contains(err.Error(), "closed") {
		t.Fatal(err)
	}
	waitReader(t, c)
	if !b.closed.Load() {
		t.Fatal("read failure leaked socket")
	}
	f.server.sweep(time.Now())
	assertEmptyRTC(t, f)
}

func TestRTCSessionServerShutdown(t *testing.T) {
	for _, cancelOnly := range []bool{false, true} {
		t.Run(fmt.Sprint(cancelOnly), func(t *testing.T) {
			f := newWebRTCFixture()
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			if err := f.server.Run(ctx); err != nil {
				t.Fatal(err)
			}
			c, b := cachedRTC(t, f, "shutdown")
			if err := f.server.handleClientUDP(ctx, &net.UDPAddr{Port: 9000}, newStunBindingRequest(c.Ufrag)); err != nil {
				t.Fatal(err)
			}
			<-b.writes
			if cancelOnly {
				cancel()
			} else {
				f.server.Close()
			}
			waitReader(t, c)
			if !b.closed.Load() {
				t.Fatal("shutdown retained socket")
			}
			f.server.Close()
			assertEmptyRTC(t, f)
		})
	}
}
func TestRTCSessionStoreFailureNotCached(t *testing.T) {
	f := newWebRTCFixture()
	f.lb.StoreWebRTCReturns(fmt.Errorf("store failure"))
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprint(w, "v=0\r\na=ice-ufrag:local\r\na=ice-pwd:local-password\r\n")
	}))
	defer backend.Close()
	f.server.backendURL = func(*lb.OriginServer, *http.Request) (string, error) { return backend.URL, nil }
	req := httptest.NewRequest("POST", "http://example.com/rtc/v1/whep/?app=live&stream=shared", nil)
	if err := f.server.proxyApiToBackend(context.Background(), httptest.NewRecorder(), req, &lb.OriginServer{}, "v=0\r\na=ice-ufrag:remote\r\na=ice-pwd:remote-password\r\n", "shared"); err == nil {
		t.Fatal("expected store error")
	}
	assertEmptyRTC(t, f)
}
