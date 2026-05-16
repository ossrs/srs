// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"errors"
	"io"
	"net"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"srsx/internal/env/envfakes"
	"srsx/internal/lb"
	"srsx/internal/lb/lbfakes"
	"srsx/internal/rtmp"
	"srsx/internal/rtmp/rtmpfakes"
)

// fakeConn is an in-memory io.ReadWriteCloser used to replace the TCP
// connection returned by dial. Read/Write are no-ops because every protocol
// call on the connection is intercepted by FakeHandshake/FakeProtocol.
type fakeConn struct {
	closed atomic.Bool
}

func (c *fakeConn) Read(p []byte) (int, error)  { return 0, io.EOF }
func (c *fakeConn) Write(p []byte) (int, error) { return len(p), nil }
func (c *fakeConn) Close() error {
	c.closed.Store(true)
	return nil
}

// backendFixture bundles the fakes plus an rtmpClientToBackend wired against
// them. Tests configure the fakes, then exercise the methods.
type backendFixture struct {
	conn      *fakeConn
	lb        *lbfakes.FakeOriginLoadBalancer
	handshake *rtmpfakes.FakeHandshake
	protocol  *rtmpfakes.FakeProtocol
	client    *rtmpClientToBackend
}

func newBackendFixture(typ RTMPClientType) *backendFixture {
	f := &backendFixture{
		conn:      &fakeConn{},
		lb:        &lbfakes.FakeOriginLoadBalancer{},
		handshake: &rtmpfakes.FakeHandshake{},
		protocol:  &rtmpfakes.FakeProtocol{},
	}
	f.client = newRTMPClientToBackend(func(c *rtmpClientToBackend) {
		c.typ = typ
		c.loadBalancer = f.lb
		c.dial = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
			return f.conn, nil
		}
		c.newHandshake = func() rtmp.Handshake { return f.handshake }
		c.newProtocol = func(rw io.ReadWriter) rtmp.Protocol { return f.protocol }
	})
	return f
}

// queueDecode programs FakeProtocol.DecodeMessage to return the given packets
// in order, one per call. After the queue is drained, it returns an EOF-ish
// error to fail the test fast instead of looping forever.
func queueDecode(p *rtmpfakes.FakeProtocol, packets ...rtmp.Packet) {
	var i atomic.Int32
	p.DecodeMessageStub = func(m rtmp.Message) (rtmp.Packet, error) {
		idx := int(i.Add(1)) - 1
		if idx >= len(packets) {
			return nil, errors.New("decode queue drained")
		}
		return packets[idx], nil
	}
}

// readMessageOK programs ReadMessage to always return a fresh empty Message.
// The payload is irrelevant because DecodeMessage is stubbed.
func readMessageOK(p *rtmpfakes.FakeProtocol) {
	p.ReadMessageStub = func(ctx context.Context) (rtmp.Message, error) {
		return rtmp.NewMessage(), nil
	}
}

// onStatusPacket builds a *rtmp.CallPacket whose Args is an Amf0Object
// carrying the given code. Used to drive both publish() (which inspects
// Args via Amf0Converter) and play() (which uses ArgsCode()).
func onStatusPacket(code string) *rtmp.CallPacket {
	pkt := rtmp.NewCallPacket()
	pkt.CommandName = "onStatus"
	pkt.CommandObject = rtmp.NewAmf0Null()
	data := rtmp.NewAmf0Object()
	data.Set("code", rtmp.NewAmf0String(code))
	pkt.Args = data
	return pkt
}

func resultCallPacket() *rtmp.CallPacket {
	pkt := rtmp.NewCallPacket()
	pkt.CommandName = "_result"
	return pkt
}

func createStreamRes(id int) *rtmp.CreateStreamResPacket {
	pkt := rtmp.NewCreateStreamResPacket(0)
	pkt.SetStreamID(id)
	return pkt
}

// pickOK programs the load balancer to return a backend with one RTMP
// endpoint, mimicking a typical registered SRS origin.
func pickOK(f *backendFixture) {
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", RTMP: []string{"1935"}}, nil)
}

// ---------------------------------------------------------------------------
// Close()
// ---------------------------------------------------------------------------

func TestRtmpClientToBackend_Close_NilConn(t *testing.T) {
	c := newRTMPClientToBackend()
	if err := c.Close(); err != nil {
		t.Fatalf("Close with nil tcpConn: %v", err)
	}
}

func TestRtmpClientToBackend_Close_FakeConn(t *testing.T) {
	conn := &fakeConn{}
	c := newRTMPClientToBackend(func(c *rtmpClientToBackend) {
		c.tcpConn = conn
	})
	if err := c.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	if !conn.closed.Load() {
		t.Fatal("fakeConn was not closed")
	}
}

// ---------------------------------------------------------------------------
// Connect() error paths
// ---------------------------------------------------------------------------

func TestRtmpClientToBackend_Connect_BuildStreamURLError(t *testing.T) {
	// url.Parse rejects URLs that start with a colon (no scheme/host parseable),
	// so this drives BuildStreamURL's error branch before LB.Pick is reached.
	f := newBackendFixture(RTMPClientTypePublisher)
	err := f.client.Connect(context.Background(), ":bad-url", "stream")
	if err == nil {
		t.Fatal("expected error from BuildStreamURL")
	}
	if !strings.Contains(err.Error(), "build stream url") {
		t.Fatalf("unexpected error %v", err)
	}
	if f.lb.PickCallCount() != 0 {
		t.Fatalf("LB.Pick should not be called when URL is bad; got %d calls", f.lb.PickCallCount())
	}
}

func TestRtmpClientToBackend_Connect_PickError(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	f.lb.PickReturns(nil, errors.New("no backend"))

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "pick backend") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Connect_NoRTMPEndpoint(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1"}, nil) // empty RTMP slice

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "no rtmp server") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Connect_BadRTMPPort(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", RTMP: []string{"not-a-port"}}, nil)

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "parse backend") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Connect_DialError(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	pickOK(f)
	f.client.dial = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
		return nil, errors.New("dial refused")
	}

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "dial backend") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Connect_DialHonorsCtxCancel(t *testing.T) {
	// The default dial uses net.Dialer.DialContext, so a canceled ctx must
	// surface as a dial error rather than hanging on the kernel connect.
	// We assert this contract by having the test dial honor ctx itself.
	f := newBackendFixture(RTMPClientTypePublisher)
	pickOK(f)
	f.client.dial = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
		return nil, ctx.Err()
	}

	ctx, cancel := context.WithCancel(context.Background())
	cancel() // already-canceled ctx
	err := f.client.Connect(ctx, "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "dial backend") {
		t.Fatalf("unexpected error %v", err)
	}
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("expected ctx.Canceled in chain, got %v", err)
	}
}

func TestRtmpClientToBackend_Connect_HandshakeWriteC0Error(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	pickOK(f)
	f.handshake.WriteC0S0Returns(errors.New("write c0"))

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "write c0") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Connect_HandshakeReadS0Error(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	pickOK(f)
	f.handshake.ReadC0S0Returns(nil, errors.New("read s0"))

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "read s0") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Connect_WriteConnectAppError(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	pickOK(f)
	f.protocol.WritePacketReturns(errors.New("write packet"))

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "write connect app") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Connect_ExpectConnectAppResError(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	pickOK(f)
	// WritePacket succeeds, but ReadMessage inside ExpectPacket fails.
	f.protocol.ReadMessageReturns(nil, errors.New("read message"))

	err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "expect connect app res") {
		t.Fatalf("unexpected error %v", err)
	}
}

// ---------------------------------------------------------------------------
// Connect() happy paths
// ---------------------------------------------------------------------------

func TestRtmpClientToBackend_Connect_PublisherHappyPath(t *testing.T) {
	f := newBackendFixture(RTMPClientTypePublisher)
	pickOK(f)
	readMessageOK(f.protocol)
	queueDecode(f.protocol,
		rtmp.NewConnectAppResPacket(0),            // connect app res
		resultCallPacket(),                        // releaseStream res
		resultCallPacket(),                        // FCPublish res
		createStreamRes(1),                        // createStream res
		onStatusPacket("NetStream.Publish.Start"), // publish onStatus
	)

	if err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream"); err != nil {
		t.Fatalf("Connect: %+v", err)
	}

	if f.client.tcpConn != f.conn {
		t.Fatal("tcpConn should be the fake conn from dial")
	}
	if f.client.client != f.protocol {
		t.Fatal("client field should be the fake protocol")
	}
	// One WritePacket each for: connectApp, releaseStream, FCPublish, createStream, publish.
	if got := f.protocol.WritePacketCallCount(); got != 5 {
		t.Fatalf("WritePacket called %d times, want 5", got)
	}
}

func TestRtmpClientToBackend_Connect_ViewerHappyPath(t *testing.T) {
	f := newBackendFixture(RTMPClientTypeViewer)
	pickOK(f)
	readMessageOK(f.protocol)
	queueDecode(f.protocol,
		rtmp.NewConnectAppResPacket(0),         // connect app res
		createStreamRes(1),                     // createStream res
		onStatusPacket("NetStream.Play.Start"), // play onStatus
	)

	if err := f.client.Connect(context.Background(), "rtmp://1.2.3.4/live", "stream"); err != nil {
		t.Fatalf("Connect: %+v", err)
	}

	// One WritePacket each for: connectApp, createStream, play.
	if got := f.protocol.WritePacketCallCount(); got != 3 {
		t.Fatalf("WritePacket called %d times, want 3", got)
	}
}

// ---------------------------------------------------------------------------
// publish() in isolation
// ---------------------------------------------------------------------------

func newIsolatedBackend(t *testing.T, typ RTMPClientType) (*rtmpClientToBackend, *rtmpfakes.FakeProtocol) {
	t.Helper()
	p := &rtmpfakes.FakeProtocol{}
	readMessageOK(p)
	c := newRTMPClientToBackend(func(c *rtmpClientToBackend) { c.typ = typ })
	return c, p
}

func TestRtmpClientToBackend_Publish_HappyPath(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	queueDecode(p,
		resultCallPacket(),                        // releaseStream _result
		resultCallPacket(),                        // FCPublish _result
		createStreamRes(7),                        // createStream res
		onStatusPacket("NetStream.Publish.Start"), // final publish onStatus
	)

	if err := c.publish(context.Background(), p, "stream"); err != nil {
		t.Fatalf("publish: %+v", err)
	}
	if got := p.WritePacketCallCount(); got != 4 {
		t.Fatalf("WritePacket called %d times, want 4", got)
	}
}

func TestRtmpClientToBackend_Publish_ReleaseStreamWriteError(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	p.WritePacketReturns(errors.New("boom"))

	err := c.publish(context.Background(), p, "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "releaseStream") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Publish_FCPublishExpectError(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	// First ExpectPacket (releaseStream res) succeeds; the second (FCPublish res)
	// must fail. We fail ReadMessage on its second call.
	var reads atomic.Int32
	p.ReadMessageStub = func(ctx context.Context) (rtmp.Message, error) {
		if reads.Add(1) >= 2 {
			return nil, errors.New("read fail")
		}
		return rtmp.NewMessage(), nil
	}
	queueDecode(p, resultCallPacket()) // only the first decode is consumed

	err := c.publish(context.Background(), p, "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "FCPublish") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Publish_CreateStreamSkipsZeroID(t *testing.T) {
	// The createStream loop continues until StreamID != 0; verify it ignores
	// the first packet (StreamID 0) and accepts the second (StreamID 9).
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	queueDecode(p,
		resultCallPacket(),                        // releaseStream res
		resultCallPacket(),                        // FCPublish res
		createStreamRes(0),                        // ignored
		createStreamRes(9),                        // accepted
		onStatusPacket("NetStream.Publish.Start"), // final publish onStatus
	)

	if err := c.publish(context.Background(), p, "stream"); err != nil {
		t.Fatalf("publish: %+v", err)
	}
}

func TestRtmpClientToBackend_Publish_SkipsNonOnStatus(t *testing.T) {
	// publish() loops past onFCPublish (a CallPacket whose CommandName != onStatus)
	// until it sees onStatus(NetStream.Publish.Start).
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	onFC := rtmp.NewCallPacket()
	onFC.CommandName = "onFCPublish"
	queueDecode(p,
		resultCallPacket(),
		resultCallPacket(),
		createStreamRes(1),
		onFC, // skipped: not onStatus
		onStatusPacket("NetStream.Publish.Start"),
	)

	if err := c.publish(context.Background(), p, "stream"); err != nil {
		t.Fatalf("publish: %+v", err)
	}
}

func TestRtmpClientToBackend_Publish_OnStatusArgsNotObject(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	bad := rtmp.NewCallPacket()
	bad.CommandName = "onStatus"
	bad.Args = rtmp.NewAmf0String("not-an-object")
	queueDecode(p,
		resultCallPacket(),
		resultCallPacket(),
		createStreamRes(1),
		bad,
	)

	err := c.publish(context.Background(), p, "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "args not object") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Publish_OnStatusMissingCode(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	bad := rtmp.NewCallPacket()
	bad.CommandName = "onStatus"
	bad.Args = rtmp.NewAmf0Object() // empty: no "code"
	queueDecode(p,
		resultCallPacket(),
		resultCallPacket(),
		createStreamRes(1),
		bad,
	)

	err := c.publish(context.Background(), p, "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "code not string") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Publish_OnStatusWrongCode(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypePublisher)
	queueDecode(p,
		resultCallPacket(),
		resultCallPacket(),
		createStreamRes(1),
		onStatusPacket("NetStream.Publish.Failed"),
	)

	err := c.publish(context.Background(), p, "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "NetStream.Publish.Start") {
		t.Fatalf("unexpected error %v", err)
	}
}

// ---------------------------------------------------------------------------
// play() in isolation
// ---------------------------------------------------------------------------

func TestRtmpClientToBackend_Play_HappyPath(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypeViewer)
	queueDecode(p,
		createStreamRes(3),
		onStatusPacket("NetStream.Play.Start"),
	)

	if err := c.play(context.Background(), p, "stream"); err != nil {
		t.Fatalf("play: %+v", err)
	}
	// One WritePacket each for: createStream and play.
	if got := p.WritePacketCallCount(); got != 2 {
		t.Fatalf("WritePacket called %d times, want 2", got)
	}
}

func TestRtmpClientToBackend_Play_CreateStreamWriteError(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypeViewer)
	p.WritePacketReturns(errors.New("boom"))

	err := c.play(context.Background(), p, "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "createStream") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Play_CreateStreamExpectError(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypeViewer)
	p.ReadMessageReturns(nil, errors.New("read fail"))

	err := c.play(context.Background(), p, "stream")
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "createStream res") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpClientToBackend_Play_CreateStreamSkipsZeroID(t *testing.T) {
	c, p := newIsolatedBackend(t, RTMPClientTypeViewer)
	queueDecode(p,
		createStreamRes(0), // skipped
		createStreamRes(5),
		onStatusPacket("NetStream.Play.Start"),
	)

	if err := c.play(context.Background(), p, "stream"); err != nil {
		t.Fatalf("play: %+v", err)
	}
}

func TestRtmpClientToBackend_Play_FiltersUntilPlayStart(t *testing.T) {
	// play() ignores onStatus packets whose code is not NetStream.Play.Start
	// (e.g. the proxy sees a NetStream.Play.Reset first).
	c, p := newIsolatedBackend(t, RTMPClientTypeViewer)
	queueDecode(p,
		createStreamRes(1),
		onStatusPacket("NetStream.Play.Reset"), // skipped
		onStatusPacket("NetStream.Play.Start"),
	)

	if err := c.play(context.Background(), p, "stream"); err != nil {
		t.Fatalf("play: %+v", err)
	}
}

// ---------------------------------------------------------------------------
// rtmpConnection: fakes, fixture, and packet builders
// ---------------------------------------------------------------------------

// fakeNetConn is a net.Conn replacement for serve(), which takes net.Conn.
// Read/Write are no-ops because every protocol call is intercepted by the
// fake handshake/protocol; RemoteAddr/Close are called directly by serve.
type fakeNetConn struct {
	closed atomic.Bool
}

func (c *fakeNetConn) Read(p []byte) (int, error)      { return 0, io.EOF }
func (c *fakeNetConn) Write(p []byte) (int, error)     { return len(p), nil }
func (c *fakeNetConn) Close() error                    { c.closed.Store(true); return nil }
func (c *fakeNetConn) LocalAddr() net.Addr             { return fakeAddr{} }
func (c *fakeNetConn) RemoteAddr() net.Addr            { return fakeAddr{} }
func (c *fakeNetConn) SetDeadline(time.Time) error     { return nil }
func (c *fakeNetConn) SetReadDeadline(time.Time) error { return nil }
func (c *fakeNetConn) SetWriteDeadline(time.Time) error {
	return nil
}

type fakeAddr struct{}

func (fakeAddr) Network() string { return "fake" }
func (fakeAddr) String() string  { return "fake-addr" }

// connFixture bundles the fakes plus an rtmpConnection wired against them.
// Tests configure the fakes, then call rc.serve(ctx, conn).
//
// The injected newBackend always returns a "terminating" backend whose
// inner load balancer fails Pick. This drives serve() far enough to call
// newBackend (so we can assert clientType), but Connect then fails fast
// so the test does not need to drive the proxy goroutines.
type connFixture struct {
	netConn     *fakeNetConn
	clientHs    *rtmpfakes.FakeHandshake
	clientProto *rtmpfakes.FakeProtocol
	lb          *lbfakes.FakeOriginLoadBalancer

	backendCalls      atomic.Int32
	backendClientType atomic.Value // RTMPClientType

	rc *rtmpConnection
}

func newConnFixture() *connFixture {
	f := &connFixture{
		netConn:     &fakeNetConn{},
		clientHs:    &rtmpfakes.FakeHandshake{},
		clientProto: &rtmpfakes.FakeProtocol{},
		lb:          &lbfakes.FakeOriginLoadBalancer{},
	}
	// Default: protocol.ReadMessage returns a fresh empty Message so
	// ExpectPacket can proceed to DecodeMessage. DecodeMessage must be
	// queued per test via queueDecode.
	readMessageOK(f.clientProto)

	f.rc = newRTMPConnection(func(c *rtmpConnection) {
		c.loadBalancer = f.lb
		c.newHandshake = func() rtmp.Handshake { return f.clientHs }
		c.newProtocol = func(rw io.ReadWriter) rtmp.Protocol { return f.clientProto }
		c.newBackend = func(clientType RTMPClientType) *rtmpClientToBackend {
			f.backendCalls.Add(1)
			f.backendClientType.Store(clientType)
			// Terminating backend: inner LB.Pick fails, so backend.Connect
			// returns an error wrapped by serve() as "connect backend".
			terminateLb := &lbfakes.FakeOriginLoadBalancer{}
			terminateLb.PickReturns(nil, errors.New("test terminate"))
			return newRTMPClientToBackend(func(b *rtmpClientToBackend) {
				b.typ = clientType
				b.loadBalancer = terminateLb
			})
		}
	})
	return f
}

// connectReqPacket builds a ConnectAppPacket with the given tcUrl.
func connectReqPacket(tcUrl string) *rtmp.ConnectAppPacket {
	p := rtmp.NewConnectAppPacket()
	p.CommandObject.Set("tcUrl", rtmp.NewAmf0String(tcUrl))
	return p
}

// publishReqPacket builds a PublishPacket with the given stream name.
func publishReqPacket(streamName string) *rtmp.PublishPacket {
	p := rtmp.NewPublishPacket()
	p.StreamName = rtmp.NewAmf0String(streamName)
	return p
}

// playReqPacket builds a PlayPacket with the given stream name.
func playReqPacket(streamName string) *rtmp.PlayPacket {
	p := rtmp.NewPlayPacket()
	p.StreamName = rtmp.NewAmf0String(streamName)
	return p
}

// rtmp.CallPacket's CommandName field uses an unexported amf0String, which
// only accepts untyped string literals. The three identify-loop branches
// each get their own helper.
func createStreamCallPacket() *rtmp.CallPacket {
	p := rtmp.NewCallPacket()
	p.CommandName = "createStream"
	return p
}

func releaseStreamCallPacket() *rtmp.CallPacket {
	p := rtmp.NewCallPacket()
	p.CommandName = "releaseStream"
	return p
}

func getStreamLengthCallPacket() *rtmp.CallPacket {
	p := rtmp.NewCallPacket()
	p.CommandName = "getStreamLength"
	return p
}

// ---------------------------------------------------------------------------
// rtmpConnection: constructor & defaults
// ---------------------------------------------------------------------------

func TestRtmpConnection_NewSetsDefaults(t *testing.T) {
	c := newRTMPConnection()
	if c.newHandshake == nil {
		t.Fatal("newHandshake should default to a non-nil factory")
	}
	if c.newProtocol == nil {
		t.Fatal("newProtocol should default to a non-nil factory")
	}
	if c.newBackend == nil {
		t.Fatal("newBackend should default to a non-nil factory")
	}
	// Defaults are real factories — call them to confirm they return
	// non-nil concrete values.
	if hs := c.newHandshake(); hs == nil {
		t.Fatal("default newHandshake returned nil")
	}
	if p := c.newProtocol(&fakeConn{}); p == nil {
		t.Fatal("default newProtocol returned nil")
	}
}

func TestRtmpConnection_DefaultNewBackendWiresFields(t *testing.T) {
	lbInst := &lbfakes.FakeOriginLoadBalancer{}
	c := newRTMPConnection(func(c *rtmpConnection) {
		c.loadBalancer = lbInst
	})

	pub := c.newBackend(RTMPClientTypePublisher)
	if pub.typ != RTMPClientTypePublisher {
		t.Fatalf("publisher backend typ=%v, want %v", pub.typ, RTMPClientTypePublisher)
	}
	if pub.loadBalancer != lbInst {
		t.Fatal("publisher backend should reuse the connection's load balancer")
	}

	view := c.newBackend(RTMPClientTypeViewer)
	if view.typ != RTMPClientTypeViewer {
		t.Fatalf("viewer backend typ=%v, want %v", view.typ, RTMPClientTypeViewer)
	}
	if view.loadBalancer != lbInst {
		t.Fatal("viewer backend should reuse the connection's load balancer")
	}
}

func TestRtmpConnection_OptionOverridesNewBackend(t *testing.T) {
	var called atomic.Int32
	override := func(clientType RTMPClientType) *rtmpClientToBackend {
		called.Add(1)
		return newRTMPClientToBackend()
	}
	c := newRTMPConnection(func(c *rtmpConnection) { c.newBackend = override })

	_ = c.newBackend(RTMPClientTypePublisher)
	if got := called.Load(); got != 1 {
		t.Fatalf("override newBackend called %d times, want 1", got)
	}
}

// ---------------------------------------------------------------------------
// rtmpConnection.serve: handshake error paths
// ---------------------------------------------------------------------------

func TestRtmpConnection_Serve_HandshakeReadC0Error(t *testing.T) {
	f := newConnFixture()
	f.clientHs.ReadC0S0Returns(nil, errors.New("boom"))

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "read c0") {
		t.Fatalf("unexpected error %v", err)
	}
	if f.backendCalls.Load() != 0 {
		t.Fatal("newBackend should not be called on handshake failure")
	}
}

func TestRtmpConnection_Serve_HandshakeWriteC0Error(t *testing.T) {
	f := newConnFixture()
	f.clientHs.WriteC0S0Returns(errors.New("boom"))

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	// The write-c0 branch is wrapped as "write s1" in serve() (typo in
	// production, but the test pins the current behavior).
	if !strings.Contains(err.Error(), "write s1") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpConnection_Serve_HandshakeReadC2Error(t *testing.T) {
	f := newConnFixture()
	f.clientHs.ReadC2S2Returns(nil, errors.New("boom"))

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "read c2") {
		t.Fatalf("unexpected error %v", err)
	}
}

// ---------------------------------------------------------------------------
// rtmpConnection.serve: protocol error paths
// ---------------------------------------------------------------------------

func TestRtmpConnection_Serve_ExpectConnectReqError(t *testing.T) {
	f := newConnFixture()
	// Fail ReadMessage so ExpectPacket returns immediately.
	f.clientProto.ReadMessageStub = nil
	f.clientProto.ReadMessageReturns(nil, errors.New("read fail"))

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "expect connect req") {
		t.Fatalf("unexpected error %v", err)
	}
	if f.backendCalls.Load() != 0 {
		t.Fatal("newBackend should not be called when connect req fails")
	}
}

func TestRtmpConnection_Serve_WriteAckSizeError(t *testing.T) {
	f := newConnFixture()
	queueDecode(f.clientProto, connectReqPacket("rtmp://1.2.3.4/live"))
	// First WritePacket is the WindowAcknowledgementSize.
	f.clientProto.WritePacketReturnsOnCall(0, errors.New("boom"))

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "write set ack size") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpConnection_Serve_WriteConnectResError(t *testing.T) {
	f := newConnFixture()
	queueDecode(f.clientProto, connectReqPacket("rtmp://1.2.3.4/live"))
	// Third WritePacket is the ConnectAppResPacket; ack and chunk-size precede it.
	f.clientProto.WritePacketReturnsOnCall(2, errors.New("boom"))

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "write connect res") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRtmpConnection_Serve_ExpectIdentifyReqError(t *testing.T) {
	f := newConnFixture()
	// Connect req decodes fine, then the next ReadMessage fails.
	queueDecode(f.clientProto, connectReqPacket("rtmp://1.2.3.4/live"))
	var reads atomic.Int32
	f.clientProto.ReadMessageStub = func(ctx context.Context) (rtmp.Message, error) {
		if reads.Add(1) >= 2 {
			return nil, errors.New("read fail")
		}
		return rtmp.NewMessage(), nil
	}

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "expect identify req") {
		t.Fatalf("unexpected error %v", err)
	}
}

// ---------------------------------------------------------------------------
// rtmpConnection.serve: identify-loop branches
// ---------------------------------------------------------------------------

func TestRtmpConnection_Serve_IdentifyCreateStreamThenPublisher(t *testing.T) {
	f := newConnFixture()
	queueDecode(f.clientProto,
		connectReqPacket("rtmp://1.2.3.4/live"),
		createStreamCallPacket(),
		publishReqPacket("stream"),
	)

	err := f.rc.serve(context.Background(), f.netConn)
	// Reaches backend.Connect, which fails via the terminating LB.
	if err == nil || !strings.Contains(err.Error(), "connect backend") {
		t.Fatalf("expected connect backend error, got %v", err)
	}
	// WritePacket calls: ack, chunk, connectRes, createStreamRes, onFCPublish.
	if got := f.clientProto.WritePacketCallCount(); got != 5 {
		t.Fatalf("WritePacket called %d times, want 5", got)
	}
	if v := f.backendClientType.Load(); v != RTMPClientTypePublisher {
		t.Fatalf("backend clientType=%v, want publisher", v)
	}
}

func TestRtmpConnection_Serve_IdentifyDefaultCallThenViewer(t *testing.T) {
	// A generic CallPacket (e.g. releaseStream) must be acknowledged with
	// a _result reply before the identify loop sees the Play packet.
	f := newConnFixture()
	queueDecode(f.clientProto,
		connectReqPacket("rtmp://1.2.3.4/live"),
		releaseStreamCallPacket(),
		playReqPacket("stream"),
	)

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil || !strings.Contains(err.Error(), "connect backend") {
		t.Fatalf("expected connect backend error, got %v", err)
	}
	// WritePacket calls: ack, chunk, connectRes, _result, onStatus(play).
	if got := f.clientProto.WritePacketCallCount(); got != 5 {
		t.Fatalf("WritePacket called %d times, want 5", got)
	}
	if v := f.backendClientType.Load(); v != RTMPClientTypeViewer {
		t.Fatalf("backend clientType=%v, want viewer", v)
	}
}

func TestRtmpConnection_Serve_IdentifyGetStreamLengthSkipsResponse(t *testing.T) {
	// getStreamLength is ignored — no response written, no error, the loop
	// just reads the next packet (the Publish).
	f := newConnFixture()
	queueDecode(f.clientProto,
		connectReqPacket("rtmp://1.2.3.4/live"),
		getStreamLengthCallPacket(),
		publishReqPacket("stream"),
	)

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil || !strings.Contains(err.Error(), "connect backend") {
		t.Fatalf("expected connect backend error, got %v", err)
	}
	// WritePacket calls: ack, chunk, connectRes, onFCPublish.
	// getStreamLength contributes nothing because the switch falls through.
	if got := f.clientProto.WritePacketCallCount(); got != 4 {
		t.Fatalf("WritePacket called %d times, want 4", got)
	}
}

func TestRtmpConnection_Serve_IdentifyResponseWriteError(t *testing.T) {
	f := newConnFixture()
	queueDecode(f.clientProto,
		connectReqPacket("rtmp://1.2.3.4/live"),
		createStreamCallPacket(),
	)
	// First three WritePacket calls (ack, chunk, connectRes) succeed;
	// the fourth (createStream response) fails.
	f.clientProto.WritePacketReturnsOnCall(3, errors.New("boom"))

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil {
		t.Fatal("expected error")
	}
	if !strings.Contains(err.Error(), "write identify res") {
		t.Fatalf("unexpected error %v", err)
	}
}

// ---------------------------------------------------------------------------
// rtmpConnection.serve: newBackend invocation contract
// ---------------------------------------------------------------------------

func TestRtmpConnection_Serve_PublisherInvokesNewBackend(t *testing.T) {
	// A direct Publish (no createStream beforehand) still drives the
	// identify loop to set clientType=Publisher and call newBackend.
	f := newConnFixture()
	queueDecode(f.clientProto,
		connectReqPacket("rtmp://1.2.3.4/live"),
		publishReqPacket("stream"),
	)

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil || !strings.Contains(err.Error(), "connect backend") {
		t.Fatalf("expected connect backend error, got %v", err)
	}
	if got := f.backendCalls.Load(); got != 1 {
		t.Fatalf("newBackend called %d times, want 1", got)
	}
	if v := f.backendClientType.Load(); v != RTMPClientTypePublisher {
		t.Fatalf("backend clientType=%v, want publisher", v)
	}
}

func TestRtmpConnection_Serve_ViewerInvokesNewBackend(t *testing.T) {
	f := newConnFixture()
	queueDecode(f.clientProto,
		connectReqPacket("rtmp://1.2.3.4/live"),
		playReqPacket("stream"),
	)

	err := f.rc.serve(context.Background(), f.netConn)
	if err == nil || !strings.Contains(err.Error(), "connect backend") {
		t.Fatalf("expected connect backend error, got %v", err)
	}
	if got := f.backendCalls.Load(); got != 1 {
		t.Fatalf("newBackend called %d times, want 1", got)
	}
	if v := f.backendClientType.Load(); v != RTMPClientTypeViewer {
		t.Fatalf("backend clientType=%v, want viewer", v)
	}
}

// ---------------------------------------------------------------------------
// rtmpProxyServer: fakes and fixture
// ---------------------------------------------------------------------------

// fakeListener is a net.Listener whose Accept returns connections pushed via
// push() and unblocks Accept with a "use of closed network connection" error
// on Close. The error message satisfies utils.IsClosedNetworkError so the
// accept loop exits via the graceful branch.
type fakeListener struct {
	conns  chan net.Conn
	closed atomic.Bool
}

func newFakeListener() *fakeListener {
	return &fakeListener{conns: make(chan net.Conn, 4)}
}

func (l *fakeListener) push(c net.Conn) { l.conns <- c }

func (l *fakeListener) Accept() (net.Conn, error) {
	c, ok := <-l.conns
	if !ok {
		return nil, errors.New("use of closed network connection")
	}
	return c, nil
}

func (l *fakeListener) Close() error {
	if l.closed.CompareAndSwap(false, true) {
		close(l.conns)
	}
	return nil
}

func (l *fakeListener) Addr() net.Addr { return fakeAddr{} }

// proxyFixture bundles the fakes plus an rtmpProxyServer wired against them.
// The injected newConnection returns a connection whose handshake fails on
// ReadC0S0, so serve() returns fast without needing to drive the full RTMP
// protocol. Tests can assert how many connections were dispatched via
// newConnCalls and how the listen() option was invoked via listenCalls/
// listenAddr.
type proxyFixture struct {
	env          *envfakes.FakeProxyEnvironment
	lb           *lbfakes.FakeOriginLoadBalancer
	listener     *fakeListener
	listenCalls  atomic.Int32
	listenAddr   atomic.Value // string
	newConnCalls atomic.Int32
	serveDone    chan struct{}
	server       *rtmpProxyServer
}

func newProxyFixture() *proxyFixture {
	f := &proxyFixture{
		env:       &envfakes.FakeProxyEnvironment{},
		lb:        &lbfakes.FakeOriginLoadBalancer{},
		listener:  newFakeListener(),
		serveDone: make(chan struct{}, 16),
	}
	f.env.RtmpServerReturns("1935")

	srv := NewRTMPProxyServer(f.env, f.lb, func(v *rtmpProxyServer) {
		v.listen = func(ctx context.Context, addr string) (net.Listener, error) {
			f.listenCalls.Add(1)
			f.listenAddr.Store(addr)
			return f.listener, nil
		}
		v.newConnection = func() *rtmpConnection {
			f.newConnCalls.Add(1)
			hs := &rtmpfakes.FakeHandshake{}
			hs.ReadC0S0Returns(nil, errors.New("test terminate"))
			return newRTMPConnection(func(c *rtmpConnection) {
				// Signal when serve() actually enters the handshake step so
				// tests can sync on "per-conn goroutine has started".
				c.newHandshake = func() rtmp.Handshake {
					f.serveDone <- struct{}{}
					return hs
				}
			})
		}
	})
	f.server = srv.(*rtmpProxyServer)
	return f
}

// ---------------------------------------------------------------------------
// rtmpProxyServer: constructor & defaults
// ---------------------------------------------------------------------------

func TestRTMPProxyServer_NewSetsDefaults(t *testing.T) {
	srv := NewRTMPProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	v := srv.(*rtmpProxyServer)
	if v.listen == nil {
		t.Fatal("listen should default to a non-nil factory")
	}
	if v.newConnection == nil {
		t.Fatal("newConnection should default to a non-nil factory")
	}
	// Default newConnection returns a wired-up rtmpConnection that reuses
	// the server's load balancer.
	rc := v.newConnection()
	if rc == nil {
		t.Fatal("default newConnection returned nil")
	}
	if rc.loadBalancer != v.loadBalancer {
		t.Fatal("default newConnection should propagate the server's loadBalancer")
	}
}

func TestRTMPProxyServer_NewAppliesOptions(t *testing.T) {
	var listenCalls, newConnCalls atomic.Int32
	srv := NewRTMPProxyServer(
		&envfakes.FakeProxyEnvironment{},
		&lbfakes.FakeOriginLoadBalancer{},
		func(v *rtmpProxyServer) {
			v.listen = func(ctx context.Context, addr string) (net.Listener, error) {
				listenCalls.Add(1)
				return nil, errors.New("unused")
			}
			v.newConnection = func() *rtmpConnection {
				newConnCalls.Add(1)
				return newRTMPConnection()
			}
		},
	)
	v := srv.(*rtmpProxyServer)
	_, _ = v.listen(context.Background(), ":0")
	_ = v.newConnection()
	if got := listenCalls.Load(); got != 1 {
		t.Fatalf("custom listen called %d times, want 1", got)
	}
	if got := newConnCalls.Load(); got != 1 {
		t.Fatalf("custom newConnection called %d times, want 1", got)
	}
}

// ---------------------------------------------------------------------------
// rtmpProxyServer.Close
// ---------------------------------------------------------------------------

func TestRTMPProxyServer_Close_NoListener(t *testing.T) {
	// Close before Run must not panic, must not hang, and must not error.
	srv := NewRTMPProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
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
// rtmpProxyServer.Run: listen and endpoint normalization
// ---------------------------------------------------------------------------

func TestRTMPProxyServer_Run_ListenError(t *testing.T) {
	envFake := &envfakes.FakeProxyEnvironment{}
	envFake.RtmpServerReturns("1935")
	srv := NewRTMPProxyServer(envFake, &lbfakes.FakeOriginLoadBalancer{}, func(v *rtmpProxyServer) {
		v.listen = func(ctx context.Context, addr string) (net.Listener, error) {
			return nil, errors.New("permission denied")
		}
	})

	err := srv.Run(context.Background())
	if err == nil {
		t.Fatal("expected error from Run when listen fails")
	}
	if !strings.Contains(err.Error(), "listen rtmp addr") {
		t.Fatalf("unexpected error %v", err)
	}
}

func TestRTMPProxyServer_Run_EndpointWithoutColon(t *testing.T) {
	// A bare port like "1935" must be normalized to ":1935" before reaching listen().
	f := newProxyFixture()
	f.env.RtmpServerReturns("1935")

	if err := f.server.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	defer f.server.Close()

	if got := f.listenAddr.Load(); got != ":1935" {
		t.Fatalf("listen addr=%v, want :1935", got)
	}
}

func TestRTMPProxyServer_Run_EndpointWithColon(t *testing.T) {
	// An endpoint that already contains ":" must be passed through unchanged.
	f := newProxyFixture()
	f.env.RtmpServerReturns("127.0.0.1:1935")

	if err := f.server.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	defer f.server.Close()

	if got := f.listenAddr.Load(); got != "127.0.0.1:1935" {
		t.Fatalf("listen addr=%v, want 127.0.0.1:1935", got)
	}
}

// ---------------------------------------------------------------------------
// rtmpProxyServer.Run: accept loop
// ---------------------------------------------------------------------------

func TestRTMPProxyServer_Run_AcceptInvokesNewConnection(t *testing.T) {
	f := newProxyFixture()
	if err := f.server.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}

	conn := &fakeNetConn{}
	f.listener.push(conn)

	// Wait for the per-conn goroutine to start (newHandshake is observed).
	select {
	case <-f.serveDone:
	case <-time.After(2 * time.Second):
		t.Fatal("newConnection was not invoked for accepted conn")
	}

	if got := f.newConnCalls.Load(); got != 1 {
		t.Fatalf("newConnection called %d times, want 1", got)
	}

	// Close shuts the listener and drains the accept goroutine cleanly.
	if err := f.server.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	// The accepted conn should have been closed by the per-conn goroutine's
	// defer once serve() returned.
	if !conn.closed.Load() {
		t.Fatal("accepted conn was not closed after serve returned")
	}
}

func TestRTMPProxyServer_Run_CloseShutsDownAcceptLoop(t *testing.T) {
	// Start Run with an idle listener (no queued conns). Accept blocks. Close
	// must unblock it and let Run/Close return cleanly via the closed-network
	// branch in the accept loop.
	f := newProxyFixture()
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
		t.Fatal("Close hung — accept loop did not exit on listener close")
	}

	if f.newConnCalls.Load() != 0 {
		t.Fatal("newConnection should not be called when no conn was accepted")
	}
}
