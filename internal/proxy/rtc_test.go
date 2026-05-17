// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"srsx/internal/env/envfakes"
	"srsx/internal/lb"
	"srsx/internal/lb/lbfakes"
)

func TestRtcICEPair_Ufrag(t *testing.T) {
	cases := []struct {
		name string
		pair rtcICEPair
		want string
	}{
		{
			name: "typical",
			pair: rtcICEPair{
				RemoteICEUfrag: "remote-ufrag",
				RemoteICEPwd:   "remote-pwd",
				LocalICEUfrag:  "local-ufrag",
				LocalICEPwd:    "local-pwd",
			},
			want: "local-ufrag:remote-ufrag",
		},
		{
			name: "both empty",
			pair: rtcICEPair{},
			want: ":",
		},
		{
			name: "only local",
			pair: rtcICEPair{LocalICEUfrag: "L"},
			want: "L:",
		},
		{
			name: "only remote",
			pair: rtcICEPair{RemoteICEUfrag: "R"},
			want: ":R",
		},
		{
			name: "pwd fields do not affect ufrag",
			pair: rtcICEPair{
				RemoteICEUfrag: "r",
				RemoteICEPwd:   "should-be-ignored",
				LocalICEUfrag:  "l",
				LocalICEPwd:    "should-be-ignored",
			},
			want: "l:r",
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := c.pair.Ufrag(); got != c.want {
				t.Fatalf("Ufrag()=%q, want %q", got, c.want)
			}
		})
	}
}

// fakeBackendUDP is an in-memory io.ReadWriteCloser standing in for the dialed
// UDP socket. Writes are captured on a channel; reads block until reads is fed
// or closed (in which case Read returns io.EOF).
type fakeBackendUDP struct {
	writes    chan []byte
	reads     chan []byte
	closed    atomic.Bool
	writeErr  error
	readErr   error
	readOnce  atomic.Bool // when set, second Read returns io.EOF to terminate the goroutine
	bytesRead atomic.Int64
}

func newFakeBackendUDP() *fakeBackendUDP {
	return &fakeBackendUDP{
		writes: make(chan []byte, 16),
		reads:  make(chan []byte, 16),
	}
}

func (f *fakeBackendUDP) Read(p []byte) (int, error) {
	if f.readErr != nil {
		return 0, f.readErr
	}
	data, ok := <-f.reads
	if !ok {
		return 0, io.EOF
	}
	n := copy(p, data)
	f.bytesRead.Add(int64(n))
	return n, nil
}

func (f *fakeBackendUDP) Write(p []byte) (int, error) {
	if f.writeErr != nil {
		return 0, f.writeErr
	}
	cp := make([]byte, len(p))
	copy(cp, p)
	f.writes <- cp
	return len(p), nil
}

func (f *fakeBackendUDP) Close() error {
	if f.closed.CompareAndSwap(false, true) {
		close(f.reads)
	}
	return nil
}

// fakePacketConn is an in-memory net.PacketConn standing in for the proxy's
// UDP listener. Only WriteTo is exercised; the other methods are no-ops.
type fakePacketConn struct {
	writes   chan packetWrite
	writeErr error
}

type packetWrite struct {
	data []byte
	addr net.Addr
}

func newFakePacketConn() *fakePacketConn {
	return &fakePacketConn{writes: make(chan packetWrite, 16)}
}

func (f *fakePacketConn) WriteTo(p []byte, addr net.Addr) (int, error) {
	if f.writeErr != nil {
		return 0, f.writeErr
	}
	cp := make([]byte, len(p))
	copy(cp, p)
	f.writes <- packetWrite{data: cp, addr: addr}
	return len(p), nil
}

func (f *fakePacketConn) ReadFrom(p []byte) (int, net.Addr, error) { return 0, nil, io.EOF }
func (f *fakePacketConn) Close() error                             { return nil }
func (f *fakePacketConn) LocalAddr() net.Addr                      { return nil }
func (f *fakePacketConn) SetDeadline(time.Time) error              { return nil }
func (f *fakePacketConn) SetReadDeadline(time.Time) error          { return nil }
func (f *fakePacketConn) SetWriteDeadline(time.Time) error         { return nil }

func TestNewRTCConnection(t *testing.T) {
	t.Run("defaults dialBackendUDP", func(t *testing.T) {
		c := newRTCConnection()
		if c.dialBackendUDP == nil {
			t.Fatal("expected dialBackendUDP to be defaulted")
		}
	})

	t.Run("applies functional options", func(t *testing.T) {
		c := newRTCConnection(func(c *rtcConnection) {
			c.StreamURL = "vhost/app/stream"
			c.Ufrag = "L:R"
		})
		if c.StreamURL != "vhost/app/stream" {
			t.Fatalf("StreamURL=%q", c.StreamURL)
		}
		if c.Ufrag != "L:R" {
			t.Fatalf("Ufrag=%q", c.Ufrag)
		}
	})

	t.Run("options override default dialBackendUDP", func(t *testing.T) {
		called := false
		dial := func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
			called = true
			return nil, nil
		}
		c := newRTCConnection(func(c *rtcConnection) { c.dialBackendUDP = dial })
		_, _ = c.dialBackendUDP(context.Background(), "", 0)
		if !called {
			t.Fatal("expected overridden dialBackendUDP to be invoked")
		}
	})
}

func TestRtcConnection_Initialize(t *testing.T) {
	t.Run("sets ctx when nil", func(t *testing.T) {
		c := newRTCConnection()
		listener := newFakePacketConn()
		ret := c.Initialize(context.Background(), listener)
		if c.ctx == nil {
			t.Fatal("expected ctx to be set")
		}
		if c.listenerUDP != listener {
			t.Fatal("expected listenerUDP to be set")
		}
		if ret != c {
			t.Fatal("expected Initialize to return receiver")
		}
	})

	t.Run("does not overwrite existing ctx", func(t *testing.T) {
		type ctxKey struct{}
		original := context.WithValue(context.Background(), ctxKey{}, "marker")
		c := newRTCConnection(func(c *rtcConnection) { c.ctx = original })
		c.Initialize(context.Background(), nil)
		if got := c.ctx.Value(ctxKey{}); got != "marker" {
			t.Fatalf("ctx was overwritten; got value=%v", got)
		}
	})

	t.Run("nil listener does not overwrite existing", func(t *testing.T) {
		existing := newFakePacketConn()
		c := newRTCConnection(func(c *rtcConnection) { c.listenerUDP = existing })
		c.Initialize(context.Background(), nil)
		if c.listenerUDP != existing {
			t.Fatal("nil listener overwrote existing listenerUDP")
		}
	})
}

func TestRtcConnection_GetUfrag(t *testing.T) {
	c := newRTCConnection(func(c *rtcConnection) { c.Ufrag = "abc:def" })
	if got := c.GetUfrag(); got != "abc:def" {
		t.Fatalf("GetUfrag()=%q", got)
	}
}

// rtcConnFixture wires an rtcConnection with fakes for the load balancer,
// listener, and backend dial seam.
type rtcConnFixture struct {
	conn     *rtcConnection
	lb       *lbfakes.FakeOriginLoadBalancer
	listener *fakePacketConn
	backend  *fakeBackendUDP
	dialErr  error
	dialIP   string
	dialPort int
}

func newRtcConnFixture() *rtcConnFixture {
	f := &rtcConnFixture{
		lb:       &lbfakes.FakeOriginLoadBalancer{},
		listener: newFakePacketConn(),
		backend:  newFakeBackendUDP(),
	}
	f.conn = newRTCConnection(func(c *rtcConnection) {
		c.loadBalancer = f.lb
		c.StreamURL = "vhost/app/stream"
		c.Ufrag = "L:R"
		c.listenerUDP = f.listener
		c.dialBackendUDP = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
			f.dialIP, f.dialPort = ip, port
			if f.dialErr != nil {
				return nil, f.dialErr
			}
			return f.backend, nil
		}
	})
	return f
}

func TestRtcConnection_ConnectBackend(t *testing.T) {
	t.Run("noop when already connected", func(t *testing.T) {
		f := newRtcConnFixture()
		f.conn.backendUDP = f.backend
		if err := f.conn.connectBackend(context.Background()); err != nil {
			t.Fatalf("unexpected err=%v", err)
		}
		if f.lb.PickCallCount() != 0 {
			t.Fatal("expected Pick not to be called when already connected")
		}
	})

	t.Run("propagates Pick error", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(nil, errors.New("boom"))
		err := f.conn.connectBackend(context.Background())
		if err == nil || !strings.Contains(err.Error(), "boom") {
			t.Fatalf("expected pick err, got %v", err)
		}
	})

	t.Run("errors when backend has no RTC endpoints", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1"}, nil)
		err := f.conn.connectBackend(context.Background())
		if err == nil || !strings.Contains(err.Error(), "no udp server") {
			t.Fatalf("expected no-udp-server err, got %v", err)
		}
	})

	t.Run("propagates dial error", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", RTC: []string{"18000"}}, nil)
		f.dialErr = errors.New("dial-failed")
		err := f.conn.connectBackend(context.Background())
		if err == nil || !strings.Contains(err.Error(), "dial-failed") {
			t.Fatalf("expected dial err, got %v", err)
		}
		if f.conn.backendUDP != nil {
			t.Fatal("backendUDP should remain nil on dial failure")
		}
	})

	t.Run("success sets backendUDP and forwards ip/port", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.5", RTC: []string{"18000"}}, nil)
		if err := f.conn.connectBackend(context.Background()); err != nil {
			t.Fatalf("unexpected err=%v", err)
		}
		if f.conn.backendUDP != f.backend {
			t.Fatal("backendUDP not set")
		}
		if f.dialIP != "10.0.0.5" || f.dialPort != 18000 {
			t.Fatalf("dial got ip=%q port=%d", f.dialIP, f.dialPort)
		}
	})
}

func TestRtcConnection_HandlePacket(t *testing.T) {
	t.Run("writes data to backend and stores client addr", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", RTC: []string{"18000"}}, nil)
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		f.conn.Initialize(ctx, f.listener)

		clientAddr := &net.UDPAddr{IP: net.ParseIP("192.168.1.5"), Port: 5000}
		payload := []byte("hello-backend")
		if err := f.conn.HandlePacket(clientAddr, payload); err != nil {
			t.Fatalf("HandlePacket err=%v", err)
		}

		if f.conn.clientUDP != clientAddr {
			t.Fatal("clientUDP not updated")
		}

		select {
		case got := <-f.backend.writes:
			if string(got) != string(payload) {
				t.Fatalf("backend got %q, want %q", got, payload)
			}
		case <-time.After(time.Second):
			t.Fatal("timeout waiting for backend write")
		}
	})

	t.Run("propagates connectBackend error", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(nil, errors.New("pick-fail"))
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		f.conn.Initialize(ctx, f.listener)

		err := f.conn.HandlePacket(&net.UDPAddr{}, []byte("x"))
		if err == nil || !strings.Contains(err.Error(), "pick-fail") {
			t.Fatalf("expected propagated pick err, got %v", err)
		}
	})

	t.Run("propagates backend write error", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", RTC: []string{"18000"}}, nil)
		f.backend.writeErr = errors.New("write-fail")
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		f.conn.Initialize(ctx, f.listener)

		err := f.conn.HandlePacket(&net.UDPAddr{}, []byte("x"))
		if err == nil || !strings.Contains(err.Error(), "write-fail") {
			t.Fatalf("expected propagated write err, got %v", err)
		}
	})

	t.Run("backend reads are forwarded to listener", func(t *testing.T) {
		f := newRtcConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", RTC: []string{"18000"}}, nil)
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		f.conn.Initialize(ctx, f.listener)

		clientAddr := &net.UDPAddr{IP: net.ParseIP("192.168.1.5"), Port: 5000}
		if err := f.conn.HandlePacket(clientAddr, []byte("trigger")); err != nil {
			t.Fatalf("HandlePacket err=%v", err)
		}
		// drain the trigger packet sent to backend
		<-f.backend.writes

		// Feed a packet from the backend; expect it forwarded to the listener.
		f.backend.reads <- []byte("from-backend")

		select {
		case got := <-f.listener.writes:
			if string(got.data) != "from-backend" {
				t.Fatalf("listener got %q, want %q", got.data, "from-backend")
			}
			if got.addr != clientAddr {
				t.Fatalf("listener addr=%v, want %v", got.addr, clientAddr)
			}
		case <-time.After(time.Second):
			t.Fatal("timeout waiting for listener write")
		}

		// Cleanly terminate the read loop.
		_ = f.backend.Close()
	})
}

// ---------------------------------------------------------------------------
// webRTCProxyServer: fakes, helpers, and fixtures
// ---------------------------------------------------------------------------

// blockingUDPListener stands in for the WebRTC UDP listener used by Run().
// ReadFrom blocks until packets are pushed via push(); Close unblocks the
// reader with a "use of closed network connection" error so the accept loop
// hits utils.IsClosedNetworkError and exits gracefully.
type blockingUDPListener struct {
	packets chan udpPacket
	writes  chan packetWrite
	closed  atomic.Bool
}

type udpPacket struct {
	data []byte
	addr net.Addr
}

func newBlockingUDPListener() *blockingUDPListener {
	return &blockingUDPListener{
		packets: make(chan udpPacket, 8),
		writes:  make(chan packetWrite, 16),
	}
}

func (l *blockingUDPListener) push(p udpPacket) { l.packets <- p }

func (l *blockingUDPListener) ReadFrom(buf []byte) (int, net.Addr, error) {
	p, ok := <-l.packets
	if !ok {
		return 0, nil, errors.New("use of closed network connection")
	}
	n := copy(buf, p.data)
	return n, p.addr, nil
}

func (l *blockingUDPListener) WriteTo(p []byte, addr net.Addr) (int, error) {
	cp := make([]byte, len(p))
	copy(cp, p)
	l.writes <- packetWrite{data: cp, addr: addr}
	return len(p), nil
}

func (l *blockingUDPListener) Close() error {
	if l.closed.CompareAndSwap(false, true) {
		close(l.packets)
	}
	return nil
}

func (l *blockingUDPListener) LocalAddr() net.Addr              { return fakeAddr{} }
func (l *blockingUDPListener) SetDeadline(time.Time) error      { return nil }
func (l *blockingUDPListener) SetReadDeadline(time.Time) error  { return nil }
func (l *blockingUDPListener) SetWriteDeadline(time.Time) error { return nil }

// newStunBindingRequest builds a minimal STUN binding request packet whose
// USERNAME attribute (type 0x0006) carries the given ufrag. The first byte is
// 0x00 so utils.RtcIsSTUN returns true; the header's message-length field
// matches the attribute body so rtcStunPacket.UnmarshalBinary succeeds.
func newStunBindingRequest(ufrag string) []byte {
	body := make([]byte, 0, 4+len(ufrag)+3)
	body = append(body, 0x00, 0x06)
	body = append(body, byte(len(ufrag)>>8), byte(len(ufrag)))
	body = append(body, []byte(ufrag)...)
	for len(body)%4 != 0 {
		body = append(body, 0)
	}

	hdr := make([]byte, 20)
	binary.BigEndian.PutUint16(hdr[0:2], 0x0001)
	binary.BigEndian.PutUint16(hdr[2:4], uint16(len(body)))
	return append(hdr, body...)
}

// fakeNonStunPacket builds a UDP payload whose first byte is neither 0/1 (so
// utils.RtcIsSTUN returns false) nor a valid RTP marker, so handleClientUDP
// treats it as "unknown" and skips parsing.
func fakeNonStunPacket() []byte { return []byte{0x42, 0x00, 0x00, 0x00} }

// fakeRTPPacket builds a minimal payload that satisfies utils.RtcIsRTPOrRTCP
// (len >= 12, first byte 0x80) so handleClientUDP's STUN parser is skipped.
func fakeRTPPacket() []byte {
	p := make([]byte, 12)
	p[0] = 0x80
	return p
}

// webRTCFixture bundles fakes plus a webRTCProxyServer wired against them.
// The default listenUDP returns the fixture's blocking listener; tests can
// either drive Run() through it or call handler methods directly without
// starting Run() at all.
type webRTCFixture struct {
	env      *envfakes.FakeProxyEnvironment
	lb       *lbfakes.FakeOriginLoadBalancer
	listener *blockingUDPListener
	server   *webRTCProxyServer
}

func newWebRTCFixture() *webRTCFixture {
	f := &webRTCFixture{
		env:      &envfakes.FakeProxyEnvironment{},
		lb:       &lbfakes.FakeOriginLoadBalancer{},
		listener: newBlockingUDPListener(),
	}
	f.env.WebRTCServerReturns("18000")

	srv := NewWebRTCProxyServer(f.env, f.lb, func(v *webRTCProxyServer) {
		v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			return f.listener, nil
		}
	})
	f.server = srv.(*webRTCProxyServer)
	return f
}

// sampleSDPOffer is a minimal valid SDP offer with the ICE attributes
// ParseIceUfragPwd looks for. Used as the WHIP/WHEP request body.
const sampleSDPOffer = "v=0\r\n" +
	"a=ice-ufrag:remote-ufrag\r\n" +
	"a=ice-pwd:remote-pwd-very-long-value-32xx\r\n"

// sampleSDPAnswer returns an SDP answer where the backend's RTC port appears
// in a candidate line so the proxy's port-rewrite path can be exercised.
func sampleSDPAnswer(port string) string {
	return "v=0\r\n" +
		"a=ice-ufrag:local-ufrag\r\n" +
		"a=ice-pwd:local-pwd-very-long-value-32xxxx\r\n" +
		"a=candidate:1 1 udp 1 1.2.3.4 " + port + " typ host\r\n"
}

// ---------------------------------------------------------------------------
// NewWebRTCProxyServer: constructor & defaults
// ---------------------------------------------------------------------------

func TestNewWebRTCProxyServer_SetsDefaults(t *testing.T) {
	srv := NewWebRTCProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	v := srv.(*webRTCProxyServer)
	if v.listenUDP == nil {
		t.Fatal("listenUDP should default to a non-nil factory")
	}
	if v.backendURL == nil {
		t.Fatal("backendURL should default to a non-nil factory")
	}
}

func TestNewWebRTCProxyServer_DefaultBackendURL_NoAPI(t *testing.T) {
	srv := NewWebRTCProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	v := srv.(*webRTCProxyServer)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whip/", strings.NewReader(""))
	_, err := v.backendURL(&lb.OriginServer{IP: "10.0.0.1"}, req)
	if err == nil || !strings.Contains(err.Error(), "no http api server") {
		t.Fatalf("expected no-api error, got %v", err)
	}
}

func TestNewWebRTCProxyServer_DefaultBackendURL_BadPort(t *testing.T) {
	srv := NewWebRTCProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	v := srv.(*webRTCProxyServer)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/x", strings.NewReader(""))
	_, err := v.backendURL(&lb.OriginServer{IP: "10.0.0.1", API: []string{"not-a-port"}}, req)
	if err == nil || !strings.Contains(err.Error(), "parse http port") {
		t.Fatalf("expected parse-port error, got %v", err)
	}
}

func TestNewWebRTCProxyServer_DefaultBackendURL_Success(t *testing.T) {
	srv := NewWebRTCProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	v := srv.(*webRTCProxyServer)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whip/?app=live&stream=demo", strings.NewReader(""))
	got, err := v.backendURL(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}}, req)
	if err != nil {
		t.Fatalf("backendURL: %v", err)
	}
	want := "http://10.0.0.1:1985/rtc/v1/whip/?app=live&stream=demo"
	if got != want {
		t.Fatalf("backendURL=%q, want %q", got, want)
	}
}

func TestNewWebRTCProxyServer_DefaultBackendURL_NoQuery(t *testing.T) {
	// When the inbound request has no raw query, the URL must not get a
	// dangling "?" appended.
	srv := NewWebRTCProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	v := srv.(*webRTCProxyServer)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whep/", strings.NewReader(""))
	got, err := v.backendURL(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}}, req)
	if err != nil {
		t.Fatalf("backendURL: %v", err)
	}
	want := "http://10.0.0.1:1985/rtc/v1/whep/"
	if got != want {
		t.Fatalf("backendURL=%q, want %q", got, want)
	}
}

func TestNewWebRTCProxyServer_AppliesOptions(t *testing.T) {
	var listenCalls, backendCalls atomic.Int32
	srv := NewWebRTCProxyServer(
		&envfakes.FakeProxyEnvironment{},
		&lbfakes.FakeOriginLoadBalancer{},
		func(v *webRTCProxyServer) {
			v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
				listenCalls.Add(1)
				return nil, errors.New("unused")
			}
			v.backendURL = func(backend *lb.OriginServer, r *http.Request) (string, error) {
				backendCalls.Add(1)
				return "http://example.test", nil
			}
		},
	)
	v := srv.(*webRTCProxyServer)
	_, _ = v.listenUDP(context.Background(), ":0")
	_, _ = v.backendURL(&lb.OriginServer{}, httptest.NewRequest(http.MethodGet, "/", nil))
	if got := listenCalls.Load(); got != 1 {
		t.Fatalf("custom listenUDP called %d times, want 1", got)
	}
	if got := backendCalls.Load(); got != 1 {
		t.Fatalf("custom backendURL called %d times, want 1", got)
	}
}

// ---------------------------------------------------------------------------
// webRTCProxyServer.Close
// ---------------------------------------------------------------------------

func TestWebRTCProxyServer_Close_NilListener(t *testing.T) {
	// Close before Run must not panic, must not hang, and must not error.
	srv := NewWebRTCProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	done := make(chan error, 1)
	go func() { done <- srv.Close() }()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("Close: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Close hung with no listener")
	}
}

// ---------------------------------------------------------------------------
// webRTCProxyServer.Run
// ---------------------------------------------------------------------------

func TestWebRTCProxyServer_Run_ListenError(t *testing.T) {
	envFake := &envfakes.FakeProxyEnvironment{}
	envFake.WebRTCServerReturns("18000")
	srv := NewWebRTCProxyServer(envFake, &lbfakes.FakeOriginLoadBalancer{}, func(v *webRTCProxyServer) {
		v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			return nil, errors.New("permission denied")
		}
	})

	err := srv.Run(context.Background())
	if err == nil {
		t.Fatal("expected error from Run when listenUDP fails")
	}
	if !strings.Contains(err.Error(), "listen udp") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestWebRTCProxyServer_Run_EndpointWithoutColon(t *testing.T) {
	// A bare port like "18000" must be normalized to ":18000".
	envFake := &envfakes.FakeProxyEnvironment{}
	envFake.WebRTCServerReturns("18000")
	listener := newBlockingUDPListener()
	var captured atomic.Value
	srv := NewWebRTCProxyServer(envFake, &lbfakes.FakeOriginLoadBalancer{}, func(v *webRTCProxyServer) {
		v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			captured.Store(endpoint)
			return listener, nil
		}
	})

	if err := srv.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	defer srv.Close()

	if got := captured.Load(); got != ":18000" {
		t.Fatalf("listenUDP endpoint=%v, want :18000", got)
	}
}

func TestWebRTCProxyServer_Run_EndpointWithColon(t *testing.T) {
	// An endpoint that already contains ":" must be passed through unchanged.
	envFake := &envfakes.FakeProxyEnvironment{}
	envFake.WebRTCServerReturns("127.0.0.1:18000")
	listener := newBlockingUDPListener()
	var captured atomic.Value
	srv := NewWebRTCProxyServer(envFake, &lbfakes.FakeOriginLoadBalancer{}, func(v *webRTCProxyServer) {
		v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			captured.Store(endpoint)
			return listener, nil
		}
	})

	if err := srv.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	defer srv.Close()

	if got := captured.Load(); got != "127.0.0.1:18000" {
		t.Fatalf("listenUDP endpoint=%v, want 127.0.0.1:18000", got)
	}
}

func TestWebRTCProxyServer_Run_CloseStopsReadLoop(t *testing.T) {
	// Start Run with an idle listener (no packets queued). The read goroutine
	// blocks in ReadFrom. Close must unblock it via the "closed network
	// connection" error and allow the wait group to drain.
	f := newWebRTCFixture()
	if err := f.server.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}

	done := make(chan error, 1)
	go func() { done <- f.server.Close() }()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("Close: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Close hung — read loop did not exit on listener close")
	}
}

// ---------------------------------------------------------------------------
// webRTCProxyServer.HandleApiForWHIP / HandleApiForWHEP
// ---------------------------------------------------------------------------

func TestWebRTCProxyServer_HandleApiForWHIP_CORSPreflight(t *testing.T) {
	// OPTIONS short-circuits before reading the body, so the LB is untouched.
	f := newWebRTCFixture()
	req := httptest.NewRequest(http.MethodOptions, "http://example.com/rtc/v1/whip/", nil)
	rec := httptest.NewRecorder()

	if err := f.server.HandleApiForWHIP(context.Background(), rec, req); err != nil {
		t.Fatalf("WHIP: %v", err)
	}
	if rec.Code != http.StatusOK {
		t.Fatalf("status=%d, want 200", rec.Code)
	}
	if f.lb.PickCallCount() != 0 {
		t.Fatal("LB.Pick should not be called for CORS preflight")
	}
}

func TestWebRTCProxyServer_HandleApiForWHEP_CORSPreflight(t *testing.T) {
	f := newWebRTCFixture()
	req := httptest.NewRequest(http.MethodOptions, "http://example.com/rtc/v1/whep/", nil)
	rec := httptest.NewRecorder()

	if err := f.server.HandleApiForWHEP(context.Background(), rec, req); err != nil {
		t.Fatalf("WHEP: %v", err)
	}
	if rec.Code != http.StatusOK {
		t.Fatalf("status=%d, want 200", rec.Code)
	}
	if f.lb.PickCallCount() != 0 {
		t.Fatal("LB.Pick should not be called for CORS preflight")
	}
}

func TestWebRTCProxyServer_HandleApiForWHIP_PickError(t *testing.T) {
	f := newWebRTCFixture()
	f.lb.PickReturns(nil, errors.New("no backend"))

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whip/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()

	err := f.server.HandleApiForWHIP(context.Background(), rec, req)
	if err == nil || !strings.Contains(err.Error(), "pick backend") {
		t.Fatalf("expected pick-backend error, got %v", err)
	}
}

func TestWebRTCProxyServer_HandleApiForWHEP_PickError(t *testing.T) {
	f := newWebRTCFixture()
	f.lb.PickReturns(nil, errors.New("no backend"))

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whep/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()

	err := f.server.HandleApiForWHEP(context.Background(), rec, req)
	if err == nil || !strings.Contains(err.Error(), "pick backend") {
		t.Fatalf("expected pick-backend error, got %v", err)
	}
}

func TestWebRTCProxyServer_HandleApiForWHIP_HappyPath(t *testing.T) {
	// Drive a full WHIP exchange: the proxy forwards the offer to an httptest
	// backend, rewrites the UDP port in the answer, and calls StoreWebRTC.
	f := newWebRTCFixture()
	f.env.WebRTCServerReturns("19000")

	const backendRTCPort = "18000"
	var backendSawOffer atomic.Bool
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		if string(body) == sampleSDPOffer {
			backendSawOffer.Store(true)
		}
		w.WriteHeader(http.StatusCreated)
		_, _ = w.Write([]byte(sampleSDPAnswer(backendRTCPort)))
	}))
	defer backend.Close()

	// Override backendURL so the proxy talks to the httptest server.
	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return backend.URL + r.URL.Path, nil
	}

	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}, RTC: []string{backendRTCPort}}, nil)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whip/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()

	if err := f.server.HandleApiForWHIP(context.Background(), rec, req); err != nil {
		t.Fatalf("WHIP: %v", err)
	}
	if !backendSawOffer.Load() {
		t.Fatal("backend did not receive the SDP offer body")
	}
	if rec.Code != http.StatusCreated {
		t.Fatalf("client status=%d, want 201", rec.Code)
	}
	body := rec.Body.String()
	if !strings.Contains(body, " 19000 typ host") {
		t.Fatalf("answer did not rewrite backend port; got %q", body)
	}
	if strings.Contains(body, " "+backendRTCPort+" typ host") {
		t.Fatalf("answer still contains original backend port; got %q", body)
	}
	if f.lb.StoreWebRTCCallCount() != 1 {
		t.Fatalf("StoreWebRTC called %d times, want 1", f.lb.StoreWebRTCCallCount())
	}
	_, streamURL, stored := f.lb.StoreWebRTCArgsForCall(0)
	if !strings.HasSuffix(streamURL, "/live/demo") {
		t.Fatalf("StoreWebRTC streamURL=%q, want suffix /live/demo", streamURL)
	}
	if got := stored.GetUfrag(); got != "local-ufrag:remote-ufrag" {
		t.Fatalf("stored ufrag=%q, want local-ufrag:remote-ufrag", got)
	}
}

func TestWebRTCProxyServer_HandleApiForWHEP_HappyPath(t *testing.T) {
	f := newWebRTCFixture()
	f.env.WebRTCServerReturns("19000")

	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.ReadAll(r.Body)
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(sampleSDPAnswer("18000")))
	}))
	defer backend.Close()

	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return backend.URL + r.URL.Path, nil
	}
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}, RTC: []string{"18000"}}, nil)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whep/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()

	if err := f.server.HandleApiForWHEP(context.Background(), rec, req); err != nil {
		t.Fatalf("WHEP: %v", err)
	}
	if f.lb.StoreWebRTCCallCount() != 1 {
		t.Fatalf("StoreWebRTC called %d times, want 1", f.lb.StoreWebRTCCallCount())
	}
}

// Legacy /rtc/v1/play/ (used by srs_bench) wraps the SDP in a JSON envelope
// like {"sdp":"v=0\r\n..."} where \r\n is the literal 2-byte JSON escape, not
// real CRLF. The proxy must unwrap the envelope before parsing ICE attributes;
// otherwise the stored ufrag is contaminated with the next attributes and the
// STUN binding from the client cannot be matched to the connection.
func TestWebRTCProxyServer_HandleApiForWHEP_LegacyJSONEnvelope(t *testing.T) {
	f := newWebRTCFixture()
	f.env.WebRTCServerReturns("19000")

	const backendRTCPort = "18000"
	answerJSON := `{"code":0,"sessionid":"sid","sdp":"v=0\r\na=ice-ufrag:local-ufrag\r\na=ice-pwd:local-pwd-very-long-value-32xxxx\r\na=candidate:1 1 udp 1 1.2.3.4 ` + backendRTCPort + ` typ host\r\n"}`
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.ReadAll(r.Body)
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(answerJSON))
	}))
	defer backend.Close()

	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return backend.URL + r.URL.Path, nil
	}
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}, RTC: []string{backendRTCPort}}, nil)

	offerJSON := `{"api":"http://10.0.0.1:1985/rtc/v1/play/","clientip":"","sdp":"v=0\r\na=ice-ufrag:remote-ufrag\r\na=ice-pwd:remote-pwd-very-long-value-32xx\r\n","streamurl":"webrtc://example.com/live/demo"}`
	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/play/", strings.NewReader(offerJSON))
	rec := httptest.NewRecorder()

	if err := f.server.HandleApiForWHEP(context.Background(), rec, req); err != nil {
		t.Fatalf("WHEP: %v", err)
	}
	if f.lb.StoreWebRTCCallCount() != 1 {
		t.Fatalf("StoreWebRTC called %d times, want 1", f.lb.StoreWebRTCCallCount())
	}
	_, _, stored := f.lb.StoreWebRTCArgsForCall(0)
	if got, want := stored.GetUfrag(), "local-ufrag:remote-ufrag"; got != want {
		t.Fatalf("stored ufrag=%q, want %q", got, want)
	}
	// The response forwarded to the client should still be the JSON envelope
	// with the backend port rewritten to the proxy's WebRTC port.
	body := rec.Body.String()
	if !strings.Contains(body, " 19000 typ host") {
		t.Fatalf("answer did not rewrite backend port; got %q", body)
	}
	if strings.Contains(body, " "+backendRTCPort+" typ host") {
		t.Fatalf("answer still contains original backend port; got %q", body)
	}
}

func TestUnwrapSDPEnvelope(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want string
	}{
		{
			name: "raw sdp passthrough",
			in:   "v=0\r\na=ice-ufrag:abc\r\n",
			want: "v=0\r\na=ice-ufrag:abc\r\n",
		},
		{
			name: "json envelope unwrapped",
			in:   `{"code":0,"sdp":"v=0\r\na=ice-ufrag:abc\r\n"}`,
			want: "v=0\r\na=ice-ufrag:abc\r\n",
		},
		{
			name: "json envelope with leading whitespace",
			in:   "\n\t  " + `{"sdp":"v=0\r\n"}`,
			want: "v=0\r\n",
		},
		{
			name: "malformed json falls back to body",
			in:   `{not json}`,
			want: `{not json}`,
		},
		{
			name: "json without sdp falls back to body",
			in:   `{"code":0}`,
			want: `{"code":0}`,
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := unwrapSDPEnvelope(tc.in); got != tc.want {
				t.Fatalf("unwrapSDPEnvelope(%q)=%q, want %q", tc.in, got, tc.want)
			}
		})
	}
}

// ---------------------------------------------------------------------------
// webRTCProxyServer.proxyApiToBackend: error paths
// ---------------------------------------------------------------------------

func TestWebRTCProxyServer_ProxyApiToBackend_BackendURLError(t *testing.T) {
	f := newWebRTCFixture()
	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return "", errors.New("build err")
	}
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}, RTC: []string{"18000"}}, nil)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whip/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()

	err := f.server.HandleApiForWHIP(context.Background(), rec, req)
	if err == nil || !strings.Contains(err.Error(), "build err") {
		t.Fatalf("expected build err, got %v", err)
	}
}

func TestWebRTCProxyServer_ProxyApiToBackend_BackendNon200(t *testing.T) {
	f := newWebRTCFixture()
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusBadGateway)
	}))
	defer backend.Close()

	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return backend.URL + r.URL.Path, nil
	}
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}, RTC: []string{"18000"}}, nil)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whip/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()

	err := f.server.HandleApiForWHIP(context.Background(), rec, req)
	if err == nil || !strings.Contains(err.Error(), "proxy api to") {
		t.Fatalf("expected proxy-api error, got %v", err)
	}
}

func TestWebRTCProxyServer_ProxyApiToBackend_BadAnswerNoIceUfrag(t *testing.T) {
	// Backend returns an answer missing the ice-ufrag/pwd attributes; the
	// proxy must surface the ParseIceUfragPwd error rather than calling
	// StoreWebRTC.
	f := newWebRTCFixture()
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte("v=0\r\n"))
	}))
	defer backend.Close()

	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return backend.URL + r.URL.Path, nil
	}
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}, RTC: []string{"18000"}}, nil)

	req := httptest.NewRequest(http.MethodPost, "http://example.com/rtc/v1/whip/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()

	err := f.server.HandleApiForWHIP(context.Background(), rec, req)
	if err == nil || !strings.Contains(err.Error(), "parse local sdp answer") {
		t.Fatalf("expected parse-answer error, got %v", err)
	}
	if f.lb.StoreWebRTCCallCount() != 0 {
		t.Fatal("StoreWebRTC should not be called when answer is malformed")
	}
}

// ---------------------------------------------------------------------------
// webRTCProxyServer.handleClientUDP
// ---------------------------------------------------------------------------

func TestWebRTCProxyServer_HandleClientUDP_NonStunIgnored(t *testing.T) {
	// A non-STUN, non-RTP/RTCP packet with no cached connection must return
	// without touching the LB.
	f := newWebRTCFixture()
	addr := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 7000}

	if err := f.server.handleClientUDP(context.Background(), addr, fakeNonStunPacket()); err != nil {
		t.Fatalf("handleClientUDP: %v", err)
	}
	if f.lb.LoadWebRTCByUfragCallCount() != 0 {
		t.Fatal("LB.LoadWebRTCByUfrag should not be called for non-STUN packet")
	}
}

func TestWebRTCProxyServer_HandleClientUDP_RTPLikeIgnored(t *testing.T) {
	// An RTP-like packet (first byte 0x80) skips STUN parsing entirely; the
	// LB must not be consulted because no connection lookup happens.
	f := newWebRTCFixture()
	addr := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 7001}

	if err := f.server.handleClientUDP(context.Background(), addr, fakeRTPPacket()); err != nil {
		t.Fatalf("handleClientUDP: %v", err)
	}
	if f.lb.LoadWebRTCByUfragCallCount() != 0 {
		t.Fatal("LB.LoadWebRTCByUfrag should not be called for RTP-like packet")
	}
}

func TestWebRTCProxyServer_HandleClientUDP_StunBadPacket(t *testing.T) {
	// A short payload that satisfies utils.RtcIsSTUN (first byte 0x00) but
	// is shorter than the 20-byte STUN header should surface the
	// unmarshaler's "too short" error.
	f := newWebRTCFixture()
	addr := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 7002}

	err := f.server.handleClientUDP(context.Background(), addr, []byte{0x00, 0x00, 0x00})
	if err == nil || !strings.Contains(err.Error(), "stun packet too short") {
		t.Fatalf("expected too-short err, got %v", err)
	}
}

func TestWebRTCProxyServer_HandleClientUDP_StunCachedUsername(t *testing.T) {
	// A STUN packet whose USERNAME matches a connection already in the
	// username cache must route directly to that connection. We pre-wire
	// the connection so its load balancer fails Pick, so HandlePacket exits
	// quickly with a recognizable error and we can assert routing.
	f := newWebRTCFixture()

	cachedLB := &lbfakes.FakeOriginLoadBalancer{}
	cachedLB.PickReturns(nil, errors.New("test terminate"))
	cached := newRTCConnection(func(c *rtcConnection) {
		c.loadBalancer = cachedLB
		c.StreamURL = "vhost/app/stream"
		c.Ufrag = "L:R"
	})
	cached.Initialize(context.Background(), f.listener)
	f.server.usernames.Store("L:R", cached)

	addr := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 7003}
	err := f.server.handleClientUDP(context.Background(), addr, newStunBindingRequest("L:R"))
	if err == nil || !strings.Contains(err.Error(), "test terminate") {
		t.Fatalf("expected terminate err, got %v", err)
	}
	// The address cache must have learned this addr.
	if _, ok := f.server.addresses.Load(addr.String()); !ok {
		t.Fatal("expected addr to be cached after routing via username")
	}
	if f.lb.LoadWebRTCByUfragCallCount() != 0 {
		t.Fatal("LB.LoadWebRTCByUfrag should not be called when cached")
	}
}

func TestWebRTCProxyServer_HandleClientUDP_StunLoadsFromLB(t *testing.T) {
	// STUN packet whose USERNAME is not in the cache: the proxy must consult
	// the load balancer, cache the returned connection by username, and then
	// dispatch to it. handleClientUDP rewires the loaded connection's
	// loadBalancer to the server's LB, so we make f.lb.Pick fail to keep the
	// HandlePacket call deterministic.
	f := newWebRTCFixture()
	f.lb.PickReturns(nil, errors.New("test terminate"))

	loaded := newRTCConnection(func(c *rtcConnection) {
		c.StreamURL = "vhost/app/stream"
		c.Ufrag = "L:R"
	})
	f.lb.LoadWebRTCByUfragReturns(loaded, nil)

	addr := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 7004}
	err := f.server.handleClientUDP(context.Background(), addr, newStunBindingRequest("L:R"))
	if err == nil || !strings.Contains(err.Error(), "test terminate") {
		t.Fatalf("expected terminate err, got %v", err)
	}
	if got := f.lb.LoadWebRTCByUfragCallCount(); got != 1 {
		t.Fatalf("LoadWebRTCByUfrag called %d times, want 1", got)
	}
	if _, ok := f.server.usernames.Load("L:R"); !ok {
		t.Fatal("expected username to be cached after LB load")
	}
	// The loaded connection should have been rewired to use the server's LB.
	if loaded.loadBalancer != f.lb {
		t.Fatal("loaded connection should adopt the server's load balancer")
	}
}

func TestWebRTCProxyServer_HandleClientUDP_StunLBError(t *testing.T) {
	// LB.LoadWebRTCByUfrag failure must surface as a wrapped error.
	f := newWebRTCFixture()
	f.lb.LoadWebRTCByUfragReturns(nil, errors.New("lookup failed"))

	addr := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 7005}
	err := f.server.handleClientUDP(context.Background(), addr, newStunBindingRequest("missing"))
	if err == nil || !strings.Contains(err.Error(), "load webrtc by ufrag") {
		t.Fatalf("expected load-webrtc err, got %v", err)
	}
}

func TestWebRTCProxyServer_HandleClientUDP_UsesCachedAddress(t *testing.T) {
	// A non-STUN packet from an address already in the address cache must be
	// dispatched to the cached connection without consulting the LB.
	f := newWebRTCFixture()
	cachedLB := &lbfakes.FakeOriginLoadBalancer{}
	cachedLB.PickReturns(nil, errors.New("test terminate"))
	cached := newRTCConnection(func(c *rtcConnection) {
		c.loadBalancer = cachedLB
		c.StreamURL = "vhost/app/stream"
		c.Ufrag = "L:R"
	})
	cached.Initialize(context.Background(), f.listener)

	addr := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 7006}
	f.server.addresses.Store(addr.String(), cached)

	err := f.server.handleClientUDP(context.Background(), addr, fakeRTPPacket())
	if err == nil || !strings.Contains(err.Error(), "test terminate") {
		t.Fatalf("expected terminate err, got %v", err)
	}
	if f.lb.LoadWebRTCByUfragCallCount() != 0 {
		t.Fatal("LB.LoadWebRTCByUfrag should not be called when address is cached")
	}
}
