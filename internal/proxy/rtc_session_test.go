// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"runtime"
	"strings"
	stdSync "sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"srsx/internal/clock"
	"srsx/internal/lb"
)

// rtcSessionFixture negotiates sessions through the proxy with an origin that
// answers like SRS: each POST gets its own answer ufrag, and a Location whose
// session= is the ICE username "<answer ufrag>:<offer ufrag>".
type rtcSessionFixture struct {
	*webRTCFixture
	origin       *httptest.Server
	posts        atomic.Int32
	deleteStatus atomic.Int32

	mu       stdSync.Mutex
	backends map[string]*fakeBackendUDP
}

func newRTCSessionFixture(t *testing.T) *rtcSessionFixture {
	f := &rtcSessionFixture{webRTCFixture: newWebRTCFixture(), backends: map[string]*fakeBackendUDP{}}
	f.deleteStatus.Store(http.StatusOK)

	f.origin = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Connection", "Close")
		if r.Method == http.MethodDelete {
			w.WriteHeader(int(f.deleteStatus.Load()))
			return
		}
		n := f.posts.Add(1)
		answerUfrag := fmt.Sprintf("local-%v", n)
		w.Header().Set("Location", fmt.Sprintf("/rtc/v1/whip/?action=delete&token=t%v&app=live&stream=demo&session=%v:remote-ufrag", n, answerUfrag))
		w.WriteHeader(http.StatusCreated)
		fmt.Fprintf(w, "v=0\r\na=ice-ufrag:%v\r\na=ice-pwd:local-pwd-very-long-value-32xxxx\r\n"+
			"a=candidate:1 1 udp 1 1.2.3.4 8000 typ host\r\n", answerUfrag)
	}))
	t.Cleanup(f.origin.Close)

	f.server.listener = f.listener
	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return f.origin.URL + r.URL.Path + "?" + r.URL.RawQuery, nil
	}
	f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.1", API: []string{"1985"}, RTC: []string{"8000"}}, nil)
	f.lb.LoadWebRTCByUfragReturns(nil, errors.New("no WebRTC streaming"))

	var dials atomic.Int32
	f.server.dialBackendUDP = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
		b := newFakeBackendUDP()
		f.mu.Lock()
		f.backends[fmt.Sprint(dials.Add(1))] = b
		f.mu.Unlock()
		return b, nil
	}
	return f
}

// play negotiates a WHEP session, and returns its ICE username and the Location
// the client deletes it by.
func (f *rtcSessionFixture) play(t *testing.T) (ufrag, location string) {
	t.Helper()
	req := httptest.NewRequest(http.MethodPost, "http://proxy/rtc/v1/whep/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	rec := httptest.NewRecorder()
	if err := f.server.HandleApiForWHEP(context.Background(), rec, req); err != nil {
		t.Fatalf("WHEP: %v", err)
	}
	location = rec.Header().Get("Location")
	u, err := url.Parse(location)
	if err != nil {
		t.Fatalf("parse Location %q: %v", location, err)
	}
	return u.Query().Get("session"), location
}

// delete sends the client's DELETE to the Location of its session.
func (f *rtcSessionFixture) delete(t *testing.T, location string) (*httptest.ResponseRecorder, error) {
	t.Helper()
	req := httptest.NewRequest(http.MethodDelete, "http://proxy"+location, nil)
	rec := httptest.NewRecorder()
	return rec, f.server.HandleApiForWHIP(context.Background(), rec, req)
}

// send delivers one client packet, as the proxy's UDP read loop does.
func (f *rtcSessionFixture) send(t *testing.T, from net.Addr, data []byte) {
	t.Helper()
	if err := f.server.handleClientUDP(context.Background(), from, data); err != nil {
		t.Fatalf("client packet from %v: %v", from, err)
	}
}

// connect sends the client's first STUN binding request from addr, and returns
// the backend socket the proxy opened for the session.
func (f *rtcSessionFixture) connect(t *testing.T, ufrag string, addr net.Addr) *fakeBackendUDP {
	t.Helper()
	f.send(t, addr, newStunBindingRequest(ufrag))
	backend := f.backendOf(t, ufrag)
	receive(t, backend, "the first STUN binding request")
	return backend
}

func (f *rtcSessionFixture) backendOf(t *testing.T, ufrag string) *fakeBackendUDP {
	t.Helper()
	c, ok := f.server.usernames.Load(ufrag)
	if !ok {
		t.Fatalf("no session for %v", ufrag)
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	for _, b := range f.backends {
		if c.backendUDP == io.ReadWriteCloser(b) {
			return b
		}
	}
	t.Fatalf("no backend socket for %v", ufrag)
	return nil
}

func receive(t *testing.T, backend *fakeBackendUDP, what string) {
	t.Helper()
	select {
	case <-backend.writes:
	case <-time.After(time.Second):
		t.Fatalf("origin did not receive %v", what)
	}
}

// requireEnded checks that nothing of the session is left: its socket to the
// origin, its ICE username and client addresses, and its load-balancer entry.
func (f *rtcSessionFixture) requireEnded(t *testing.T, ufrag string, backend *fakeBackendUDP) {
	t.Helper()
	if _, ok := f.server.usernames.Load(ufrag); ok {
		t.Errorf("session %v is still found by its ICE username", ufrag)
	}
	f.server.addresses.Range(func(addr string, c *rtcConnection) bool {
		if c.Ufrag == ufrag {
			t.Errorf("client address %v still maps to session %v", addr, ufrag)
		}
		return true
	})
	if backend != nil && !backend.closed.Load() {
		t.Errorf("socket of session %v to the origin is not closed", ufrag)
	}
	deleted := false
	for i := 0; i < f.lb.DeleteWebRTCCallCount(); i++ {
		if _, _, value := f.lb.DeleteWebRTCArgsForCall(i); value.GetUfrag() == ufrag {
			deleted = true
		}
	}
	if !deleted {
		t.Errorf("session %v is not deleted from the load balancer", ufrag)
	}
}

func (f *rtcSessionFixture) lastPacketOf(t *testing.T, ufrag string) time.Time {
	t.Helper()
	c, ok := f.server.usernames.Load(ufrag)
	if !ok {
		t.Fatalf("no session for %v", ufrag)
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.lastPacketAt
}

// waitClockPast waits for the cached clock to move past at, so a packet sent
// next is stamped later.
func waitClockPast(t *testing.T, at time.Time) {
	t.Helper()
	deadline := time.Now().Add(time.Second)
	for !clock.Now().After(at) {
		if time.Now().After(deadline) {
			t.Fatalf("cached clock did not pass %v in a second", at)
		}
		time.Sleep(clock.Resolution)
	}
}

func clientAddr(port int) *net.UDPAddr {
	return &net.UDPAddr{IP: net.IPv4(192, 168, 1, 10), Port: port}
}

// A WHIP or WHEP client ends its session by a DELETE to its Location (RFC 9725
// section 4.3). Other viewers of the same stream keep playing.
func TestRTCSession_DeleteEndsOnlyThatSession(t *testing.T) {
	f := newRTCSessionFixture(t)
	first, firstLocation := f.play(t)
	second, secondLocation := f.play(t)
	firstBackend := f.connect(t, first, clientAddr(5001))
	secondBackend := f.connect(t, second, clientAddr(5002))

	rec, err := f.delete(t, firstLocation)
	if err != nil {
		t.Fatalf("DELETE: %v", err)
	}
	if rec.Code != http.StatusOK {
		t.Fatalf("client status=%d, want 200", rec.Code)
	}
	f.requireEnded(t, first, firstBackend)

	f.send(t, clientAddr(5002), fakeRTPPacket())
	receive(t, secondBackend, "RTP of the other viewer")

	if _, err = f.delete(t, secondLocation); err != nil {
		t.Fatalf("DELETE: %v", err)
	}
	f.requireEnded(t, second, secondBackend)
}

// RFC 9110 makes every 2xx a success, and a WHIP server other than SRS may
// answer a DELETE with 204 No Content.
func TestRTCSession_Delete204EndsSession(t *testing.T) {
	f := newRTCSessionFixture(t)
	ufrag, location := f.play(t)
	backend := f.connect(t, ufrag, clientAddr(5001))

	f.deleteStatus.Store(http.StatusNoContent)
	rec, err := f.delete(t, location)
	if err != nil {
		t.Fatalf("DELETE answered 204: %v", err)
	}
	if rec.Code != http.StatusNoContent {
		t.Fatalf("client status=%d, want 204", rec.Code)
	}
	f.requireEnded(t, ufrag, backend)
}

// A success with no SDP answer, such as 204, gives the client nothing to
// connect with.
func TestRTCSession_OfferAnswered204IsRefused(t *testing.T) {
	f := newRTCSessionFixture(t)
	origin := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNoContent)
	}))
	defer origin.Close()
	f.server.backendURL = func(b *lb.OriginServer, r *http.Request) (string, error) {
		return origin.URL + r.URL.Path, nil
	}

	req := httptest.NewRequest(http.MethodPost, "http://proxy/rtc/v1/whep/?app=live&stream=demo", strings.NewReader(sampleSDPOffer))
	if err := f.server.HandleApiForWHEP(context.Background(), httptest.NewRecorder(), req); err == nil {
		t.Fatal("expected an offer answered without SDP to fail")
	}
	if f.lb.StoreWebRTCCallCount() != 0 {
		t.Fatal("stored a session with no SDP answer")
	}
	f.server.usernames.Range(func(ufrag string, _ *rtcConnection) bool {
		t.Errorf("cached session %v with no SDP answer", ufrag)
		return true
	})
}

// SRS refuses a DELETE whose token does not match the session, and the session
// goes on.
func TestRTCSession_DeleteRefusedByOriginKeepsSession(t *testing.T) {
	f := newRTCSessionFixture(t)
	ufrag, location := f.play(t)
	backend := f.connect(t, ufrag, clientAddr(5001))

	f.deleteStatus.Store(http.StatusInternalServerError)
	if _, err := f.delete(t, location); err == nil {
		t.Fatal("expected the refused DELETE to fail")
	}

	f.send(t, clientAddr(5001), fakeRTPPacket())
	receive(t, backend, "RTP after a refused DELETE")
	if backend.closed.Load() {
		t.Fatal("refused DELETE closed the socket to the origin")
	}
}

// A DELETE goes to the origin the stream is picked to now, which may not be the
// origin of the session, such as after the Redis load balancer picks again. SRS
// answers 200 to a DELETE of a session it does not hold, without checking the
// token.
func TestRTCSession_DeleteWithForgedTokenKeepsSession(t *testing.T) {
	f := newRTCSessionFixture(t)
	ufrag, location := f.play(t)
	backend := f.connect(t, ufrag, clientAddr(5001))

	forged := strings.Replace(location, "token=t1", "token=forged", 1)
	if _, err := f.delete(t, forged); err != nil {
		t.Fatalf("DELETE: %v", err)
	}

	f.send(t, clientAddr(5001), fakeRTPPacket())
	receive(t, backend, "RTP after a DELETE with a forged token")
	if backend.closed.Load() {
		t.Fatal("DELETE with a forged token closed the socket to the origin")
	}
	if _, ok := f.server.usernames.Load(ufrag); !ok {
		t.Fatal("DELETE with a forged token forgot the session")
	}
}

// UDP has no close: a client that leaves without a DELETE, or never connects
// after its offer, only stops sending. A live client sends a STUN consent check
// every few seconds (RFC 7675), and the origin's media to a departed client is
// no sign of life.
func TestRTCSession_IdleSessionExpires(t *testing.T) {
	f := newRTCSessionFixture(t)
	departed, _ := f.play(t)
	neverConnected, _ := f.play(t)
	live, _ := f.play(t)
	departedBackend := f.connect(t, departed, clientAddr(5001))
	liveBackend := f.connect(t, live, clientAddr(5003))

	departedBackend.reads <- []byte("media to the departed client")
	select {
	case <-f.listener.writes:
	case <-time.After(time.Second):
		t.Fatal("origin media did not reach the client")
	}

	waitClockPast(t, f.lastPacketOf(t, departed))
	f.send(t, clientAddr(5003), fakeRTPPacket())
	receive(t, liveBackend, "RTP of the live client")

	f.server.expireIdleConnections(f.lastPacketOf(t, live).Add(lb.DefaultRTCSessionTTL - time.Nanosecond))

	f.requireEnded(t, departed, departedBackend)
	f.requireEnded(t, neverConnected, nil)
	f.send(t, clientAddr(5003), fakeRTPPacket())
	receive(t, liveBackend, "RTP of the live client after the idle check")
}

// A browser checks every candidate pair (RFC 8445), and goes on checking its
// other pairs without USE-CANDIDATE while media flows on the nominated one.
func TestRTCSession_BackupCheckKeepsNominatedAddress(t *testing.T) {
	f := newRTCSessionFixture(t)
	ufrag, location := f.play(t)
	nominated, backup := clientAddr(5001), &net.UDPAddr{IP: net.IPv4(10, 8, 0, 2), Port: 6001}
	backend := f.connect(t, ufrag, nominated)

	f.send(t, backup, newStunBindingRequest(ufrag))
	receive(t, backend, "the STUN check of the backup pair")
	f.send(t, nominated, fakeRTPPacket())
	receive(t, backend, "RTP on the nominated pair after a backup check")

	if _, err := f.delete(t, location); err != nil {
		t.Fatalf("DELETE: %v", err)
	}
	f.requireEnded(t, ufrag, backend)
}

// A new session can get the client address of one that ended, such as a new
// socket on a freed port. The old session ending must not take it away.
func TestRTCSession_ReusedAddressSurvivesOldSession(t *testing.T) {
	f := newRTCSessionFixture(t)
	old, oldLocation := f.play(t)
	fresh, _ := f.play(t)
	addr := clientAddr(5001)
	oldBackend := f.connect(t, old, addr)
	freshBackend := f.connect(t, fresh, addr)

	if _, err := f.delete(t, oldLocation); err != nil {
		t.Fatalf("DELETE: %v", err)
	}
	f.requireEnded(t, old, oldBackend)

	f.send(t, addr, fakeRTPPacket())
	receive(t, freshBackend, "RTP of the new session on the reused address")
}

// A client keeps sending STUN checks, from any of its addresses, while its
// DELETE is in flight.
func TestRTCSession_ChecksDuringDeleteLeaveNothing(t *testing.T) {
	for i := 0; i < 20; i++ {
		f := newRTCSessionFixture(t)
		ufrag, location := f.play(t)
		backend := f.connect(t, ufrag, clientAddr(5000))
		go func() {
			for range backend.writes {
			}
		}()

		var wg stdSync.WaitGroup
		wg.Add(1)
		go func() {
			defer wg.Done()
			for port := 5001; port < 5100; port++ {
				_ = f.server.handleClientUDP(context.Background(), clientAddr(port), newStunBindingRequest(ufrag))
			}
		}()
		if _, err := f.delete(t, location); err != nil {
			t.Fatalf("DELETE: %v", err)
		}
		wg.Wait()

		f.requireEnded(t, ufrag, backend)
	}
}

// An operator who sets PROXY_RTC_SESSION_TTL gets that lifetime: a client
// silent for the TTL ends, while one that sent within it lives on.
func TestRTCSession_ConfiguredTTLExpiresSession(t *testing.T) {
	f := newRTCSessionFixture(t)
	f.env.RTCSessionTTLReturns("5s")
	if err := f.server.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	t.Cleanup(func() { _ = f.server.Close() })

	departed, _ := f.play(t)
	live, _ := f.play(t)
	departedBackend := f.connect(t, departed, clientAddr(5001))
	liveBackend := f.connect(t, live, clientAddr(5003))

	waitClockPast(t, f.lastPacketOf(t, departed))
	f.send(t, clientAddr(5003), fakeRTPPacket())
	receive(t, liveBackend, "RTP of the live client")

	f.server.expireIdleConnections(f.lastPacketOf(t, live).Add(5*time.Second - time.Nanosecond))

	f.requireEnded(t, departed, departedBackend)
	f.send(t, clientAddr(5003), fakeRTPPacket())
	receive(t, liveBackend, "RTP of the live client after the idle check")
}

// When the proxy stops, every session ends with it.
func TestRTCSession_CloseEndsAllSessions(t *testing.T) {
	f := newRTCSessionFixture(t)
	if err := f.server.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	ufrags := map[string]*fakeBackendUDP{}
	for i := 0; i < 3; i++ {
		ufrag, _ := f.play(t)
		ufrags[ufrag] = f.connect(t, ufrag, clientAddr(5001+i))
	}

	if err := f.server.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	for ufrag, backend := range ufrags {
		f.requireEnded(t, ufrag, backend)
	}
}

// Viewers come and go all day; whether they DELETE or just leave, the proxy
// must hold no socket, goroutine or cache entry for them afterwards.
func TestRTCSession_ChurnLeavesNothing(t *testing.T) {
	f := newRTCSessionFixture(t)
	baseline := runtime.NumGoroutine()

	var leftAt time.Time
	for i := 0; i < 200; i++ {
		ufrag, location := f.play(t)
		addr := clientAddr(10000 + i)
		backend := f.connect(t, ufrag, addr)
		f.send(t, addr, fakeRTPPacket())
		receive(t, backend, "RTP")
		if i%2 == 0 {
			if _, err := f.delete(t, location); err != nil {
				t.Fatalf("DELETE: %v", err)
			}
		}
		leftAt = time.Now()
	}
	f.server.expireIdleConnections(leftAt.Add(lb.DefaultRTCSessionTTL))

	f.server.usernames.Range(func(ufrag string, _ *rtcConnection) bool {
		t.Errorf("session %v is still found by its ICE username", ufrag)
		return true
	})
	f.server.addresses.Range(func(addr string, c *rtcConnection) bool {
		t.Errorf("client address %v still maps to session %v", addr, c.Ufrag)
		return true
	})
	f.mu.Lock()
	for _, b := range f.backends {
		if !b.closed.Load() {
			t.Error("a socket to the origin is not closed")
			break
		}
	}
	f.mu.Unlock()

	deadline := time.Now().Add(2 * time.Second)
	for runtime.NumGoroutine() > baseline+2 && time.Now().Before(deadline) {
		time.Sleep(10 * time.Millisecond)
	}
	if n := runtime.NumGoroutine(); n > baseline+2 {
		t.Fatalf("goroutines grew from %v to %v", baseline, n)
	}
}

// Any host can send a STUN binding request with an unknown ufrag, and with the
// Redis load balancer the proxy looks it up over the network. A slow lookup
// must not hold up the signaling of other clients.
func TestRTCSession_SlowLookupDoesNotBlockSignaling(t *testing.T) {
	f := newRTCSessionFixture(t)
	looking, release := make(chan struct{}), make(chan struct{})
	defer close(release)
	f.lb.LoadWebRTCByUfragStub = func(ctx context.Context, ufrag string) (lb.RTCConnection, error) {
		close(looking)
		<-release
		return nil, errors.New("no WebRTC streaming")
	}
	go func() {
		_ = f.server.handleClientUDP(context.Background(), clientAddr(7000), newStunBindingRequest("unknown:ufrag"))
	}()
	<-looking

	done := make(chan string, 1)
	go func() {
		ufrag, location := f.play(t)
		_, _ = f.delete(t, location)
		done <- ufrag
	}()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("WHEP POST and DELETE blocked behind an unrelated STUN lookup")
	}
}

// failOncePacketConn fails the first WriteTo, as sendto does with ENOBUFS when
// the socket buffer is full.
type failOncePacketConn struct {
	*fakePacketConn
	failed atomic.Bool
}

func (c *failOncePacketConn) WriteTo(p []byte, addr net.Addr) (int, error) {
	if !c.failed.Swap(true) {
		return 0, syscall.ENOBUFS
	}
	return c.fakePacketConn.WriteTo(p, addr)
}

// failOnceBackendUDP fails the first Read, as a connected UDP socket does with
// ECONNREFUSED after one ICMP port unreachable, such as while the origin
// restarts.
type failOnceBackendUDP struct {
	*fakeBackendUDP
	failed atomic.Bool
}

func (b *failOnceBackendUDP) Read(p []byte) (int, error) {
	if !b.failed.Swap(true) {
		return 0, syscall.ECONNREFUSED
	}
	return b.fakeBackendUDP.Read(p)
}

// One failed packet on a socket loses that packet, not the session: the client
// and the origin are still within their consent window.
func TestRTCSession_TransientErrorKeepsSession(t *testing.T) {
	client := clientAddr(5001)
	start := func(t *testing.T, f *rtcConnFixture) {
		t.Helper()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", RTC: []string{"8000"}}, nil)
		if err := f.conn.HandlePacket(client, []byte("stun")); err != nil {
			t.Fatalf("HandlePacket: %v", err)
		}
		receive(t, f.backend, "the first packet")
	}
	toClient := func(t *testing.T, writes chan packetWrite, what string) {
		t.Helper()
		select {
		case <-writes:
		case <-time.After(time.Second):
			t.Fatalf("client did not receive %v", what)
		}
	}

	t.Run("write to client fails", func(t *testing.T) {
		f := newRtcConnFixture()
		listener := &failOncePacketConn{fakePacketConn: f.listener}
		f.conn.Initialize(context.Background(), listener)
		start(t, f)
		f.backend.reads <- []byte("lost")
		f.backend.reads <- []byte("media")
		toClient(t, f.listener.writes, "origin media after one failed write")
		_ = f.backend.Close()
	})

	t.Run("read from origin fails", func(t *testing.T) {
		f := newRtcConnFixture()
		backend := &failOnceBackendUDP{fakeBackendUDP: f.backend}
		f.conn.dialBackendUDP = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
			return backend, nil
		}
		f.conn.Initialize(context.Background(), f.listener)
		start(t, f)
		f.backend.reads <- []byte("media")
		toClient(t, f.listener.writes, "origin media after one failed read")
		_ = f.backend.Close()
	})

	t.Run("write to origin fails", func(t *testing.T) {
		f := newRtcConnFixture()
		f.conn.Initialize(context.Background(), f.listener)
		start(t, f)
		f.backend.writeErr = syscall.ECONNREFUSED
		_ = f.conn.HandlePacket(client, []byte("lost"))
		f.backend.writeErr = nil
		if err := f.conn.HandlePacket(client, []byte("rtp")); err != nil {
			t.Fatalf("HandlePacket after one failed write: %v", err)
		}
		receive(t, f.backend, "RTP after one failed write")
		_ = f.backend.Close()
	})

	t.Run("dial to origin fails", func(t *testing.T) {
		f := newRtcConnFixture()
		f.conn.Initialize(context.Background(), f.listener)
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", RTC: []string{"8000"}}, nil)
		f.dialErr = errors.New("dns lookup timed out")
		_ = f.conn.HandlePacket(client, []byte("lost"))
		f.dialErr = nil
		if err := f.conn.HandlePacket(client, []byte("stun")); err != nil {
			t.Fatalf("HandlePacket after one failed dial: %v", err)
		}
		receive(t, f.backend, "the packet after one failed dial")
		_ = f.backend.Close()
	})
}
