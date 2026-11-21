// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	stdSync "sync"
	"time"

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/lb"
	"srsx/internal/logger"
	"srsx/internal/sync"
	"srsx/internal/utils"
)

type WebRTCProxyServer interface {
	Run(ctx context.Context) error
	Close() error
	HandleApiForWHIP(ctx context.Context, w http.ResponseWriter, r *http.Request) error
	HandleApiForWHEP(ctx context.Context, w http.ResponseWriter, r *http.Request) error
}

type webRTCProxyServer struct {
	environment  env.ProxyEnvironment
	loadBalancer lb.OriginLoadBalancer
	// An interface, so a test listenUDP can return a fake.
	listener net.PacketConn

	usernames sync.Map[string, *rtcConnection]
	// TODO: Support fast search by uint64 address.
	addresses sync.Map[string, *rtcConnection]

	// cacheMu serializes cache ownership changes with eviction. Never take it
	// while holding a connection mutex.
	cacheMu   stdSync.Mutex
	resources map[string]*rtcConnection
	stop      chan struct{}
	stopOnce  stdSync.Once
	closed    bool

	wg stdSync.WaitGroup

	// Tests replace backendURL to reach an httptest.Server, and listenUDP.
	backendURL func(backend *lb.OriginServer, r *http.Request) (string, error)

	listenUDP func(ctx context.Context, endpoint string) (net.PacketConn, error)
}

func NewWebRTCProxyServer(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, opts ...func(*webRTCProxyServer)) WebRTCProxyServer {
	v := &webRTCProxyServer{
		environment:  environment,
		loadBalancer: loadBalancer,
		usernames:    sync.NewMap[string, *rtcConnection](),
		addresses:    sync.NewMap[string, *rtcConnection](),
		resources:    make(map[string]*rtcConnection),
		stop:         make(chan struct{}),
	}

	v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
		saddr, err := net.ResolveUDPAddr("udp", endpoint)
		if err != nil {
			return nil, errors.Wrapf(err, "resolve udp addr %v", endpoint)
		}
		return net.ListenUDP("udp", saddr)
	}

	v.backendURL = func(backend *lb.OriginServer, r *http.Request) (string, error) {
		if len(backend.API) == 0 {
			return "", errors.Errorf("no http api server")
		}
		apiPort, err := strconv.ParseInt(backend.API[0], 10, 64)
		if err != nil {
			return "", errors.Wrapf(err, "parse http port %v", backend.API[0])
		}
		u := fmt.Sprintf("http://%v:%v%s", backend.IP, apiPort, r.URL.Path)
		if r.URL.RawQuery != "" {
			u += "?" + r.URL.RawQuery
		}
		return u, nil
	}

	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *webRTCProxyServer) Close() error {
	v.stopOnce.Do(func() { close(v.stop) })
	v.cacheMu.Lock()
	v.closed = true
	v.usernames.Range(func(_ string, c *rtcConnection) bool { c.Close(); return true })
	v.evictClosedLocked()
	v.cacheMu.Unlock()
	if v.listener != nil {
		_ = v.listener.Close()
	}

	v.wg.Wait()
	return nil
}

func (v *webRTCProxyServer) HandleApiForWHIP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	defer r.Body.Close()
	ctx = logger.WithContext(ctx)

	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	remoteSDPOffer, err := io.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Debug(ctx, "Got WebRTC WHIP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	backend, err := v.loadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.proxyApiToBackend(ctx, w, r, backend, string(remoteSDPOffer), streamURL); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *webRTCProxyServer) HandleApiForWHEP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	defer r.Body.Close()
	ctx = logger.WithContext(ctx)

	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	remoteSDPOffer, err := io.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Debug(ctx, "Got WebRTC WHEP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	backend, err := v.loadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.proxyApiToBackend(ctx, w, r, backend, string(remoteSDPOffer), streamURL); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *webRTCProxyServer) proxyApiToBackend(
	ctx context.Context, w http.ResponseWriter, r *http.Request, backend *lb.OriginServer,
	remoteSDPOffer string, streamURL string,
) error {
	// A session DELETE belongs to the Origin that created it, even if stream
	// routing has since moved to another Origin.
	if r.Method == http.MethodDelete {
		v.cacheMu.Lock()
		if c := v.resources[rtcResourceKey(r.URL)]; c != nil && c.origin != nil {
			backend = c.origin
		}
		v.cacheMu.Unlock()
	}
	backendURL, err := v.backendURL(backend, r)
	if err != nil {
		return errors.Wrapf(err, "build backend url")
	}

	req, err := http.NewRequestWithContext(ctx, r.Method, backendURL, strings.NewReader(remoteSDPOffer))
	if err != nil {
		return errors.Wrapf(err, "create request to %v", backendURL)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return errors.Errorf("do request to %v EOF", backendURL)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK && resp.StatusCode != http.StatusCreated && !(r.Method == http.MethodDelete && resp.StatusCode == http.StatusNoContent) {
		return errors.Errorf("proxy api to %v failed, status=%v", backendURL, resp.Status)
	}

	// A WHIP client ends its session by a DELETE to the Location of the answer,
	// which carries no SDP.
	if r.Method == http.MethodDelete {
		v.cacheMu.Lock()
		if c := v.resources[rtcResourceKey(r.URL)]; c != nil {
			c.Close()
		}
		v.evictClosedLocked()
		v.cacheMu.Unlock()
		copyBackendResponseHeaders(w.Header(), resp.Header)
		w.WriteHeader(resp.StatusCode)
		if _, err = io.Copy(w, resp.Body); err != nil {
			return errors.Wrapf(err, "copy delete response from %v", backendURL)
		}
		return nil
	}

	b, err := io.ReadAll(resp.Body)
	if err != nil {
		return errors.Wrapf(err, "read stream from %v", backendURL)
	}

	localSDPAnswer := string(b)
	for _, endpoint := range backend.RTC {
		_, _, port, err := utils.ParseListenEndpoint(endpoint)
		if err != nil {
			return errors.Wrapf(err, "parse endpoint %v", endpoint)
		}

		from := fmt.Sprintf(" %v typ host", port)
		to := fmt.Sprintf(" %v typ host", v.environment.WebRTCServer())
		localSDPAnswer = strings.Replace(localSDPAnswer, from, to, -1)
	}

	// A legacy API client expects its JSON envelope back, so only the ICE
	// parsing unwraps it.
	remoteICEUfrag, remoteICEPwd, err := utils.ParseIceUfragPwd(unwrapSDPEnvelope(remoteSDPOffer))
	if err != nil {
		return errors.Wrapf(err, "parse remote sdp offer")
	}

	localICEUfrag, localICEPwd, err := utils.ParseIceUfragPwd(unwrapSDPEnvelope(localSDPAnswer))
	if err != nil {
		return errors.Wrapf(err, "parse local sdp answer")
	}

	icePair := &rtcICEPair{
		RemoteICEUfrag: remoteICEUfrag, RemoteICEPwd: remoteICEPwd,
		LocalICEUfrag: localICEUfrag, LocalICEPwd: localICEPwd,
	}
	c := newRTCConnection(func(c *rtcConnection) {
		c.loadBalancer = v.loadBalancer
		c.StreamURL, c.Ufrag = streamURL, icePair.Ufrag()
		c.origin = backend
		// HTTP request cancellation must not end the negotiated media session.
		c.Initialize(context.WithoutCancel(ctx), v.listener)
	})
	if err := v.loadBalancer.StoreWebRTC(ctx, streamURL, c); err != nil {
		c.Close()
		return errors.Wrapf(err, "load or store webrtc %v", streamURL)
	}
	v.cacheMu.Lock()
	if v.closed {
		c.Close()
		v.forgetConnection(c)
		v.cacheMu.Unlock()
		return errors.Errorf("WebRTC proxy closed")
	}
	if old, ok := v.usernames.Load(c.Ufrag); ok && old != c {
		old.Close()
	}
	v.usernames.Store(c.Ufrag, c)
	if location, err := url.Parse(resp.Header.Get("Location")); err == nil && resp.Header.Get("Location") != "" {
		v.resources[rtcResourceKey(r.URL.ResolveReference(location))] = c
	}
	v.evictClosedLocked()
	v.cacheMu.Unlock()

	// net/http does not send headers changed after WriteHeader, and a WHIP
	// client needs the backend's Location to delete its session.
	copyBackendResponseHeaders(w.Header(), resp.Header)
	if localSDPAnswer != string(b) {
		repairRewrittenResponseHeaders(w.Header(), len(localSDPAnswer))
	}
	w.WriteHeader(resp.StatusCode)

	if _, err = w.Write([]byte(localSDPAnswer)); err != nil {
		c.Close()
		v.sweep(time.Now())
		return errors.Wrapf(err, "write local sdp answer %v", localSDPAnswer)
	}

	logger.Debug(ctx, "Create WebRTC connection with local answer %vB with ice-ufrag=%v, ice-pwd=%vB",
		len(localSDPAnswer), localICEUfrag, len(localICEPwd))
	return nil
}

func (v *webRTCProxyServer) Run(ctx context.Context) error {
	endpoint := v.environment.WebRTCServer()
	if !strings.Contains(endpoint, ":") {
		endpoint = fmt.Sprintf(":%v", endpoint)
	}

	listener, err := v.listenUDP(ctx, endpoint)
	if err != nil {
		return errors.Wrapf(err, "listen udp %v", endpoint)
	}
	v.listener = listener
	logger.Debug(ctx, "WebRTC server listen at %v", listener.LocalAddr())

	// UDP has no EOF on client departure. Bound abandoned negotiations and
	// disconnected clients by their last inbound packet, not outgoing video.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case now := <-ticker.C:
				v.sweep(now)
			case <-v.stop:
				return
			case <-ctx.Done():
				// Unblock ReadFrom; Close also waits for the worker goroutines.
				_ = listener.Close()
				v.cacheMu.Lock()
				v.closed = true
				v.usernames.Range(func(_ string, c *rtcConnection) bool { c.Close(); return true })
				v.evictClosedLocked()
				v.cacheMu.Unlock()
				return
			}
		}
	}()

	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		// No callee keeps the slice: sendto copies it into the kernel, and the
		// STUN username is copied by string().
		buf := make([]byte, 4096)
		for ctx.Err() == nil {
			n, addr, err := listener.ReadFrom(buf)
			if err != nil {
				if ctx.Err() != nil || utils.IsClosedNetworkError(err) {
					logger.Debug(ctx, "WebRTC server done")
					return
				}
				// TODO: If WebRTC server closed unexpectedly, we should notice the main loop to quit.
				logger.Warn(ctx, "WebRTC read from udp failed, err=%+v", err)
				time.Sleep(1 * time.Second)
				continue
			}

			if err := v.handleClientUDP(ctx, addr, buf[:n]); err != nil {
				logger.Warn(ctx, "WebRTC handle udp %vB failed, addr=%v, err=%+v", n, addr, err)
			}
		}
	}()

	return nil
}

func (v *webRTCProxyServer) handleClientUDP(ctx context.Context, addr net.Addr, data []byte) error {
	var connection *rtcConnection
	v.cacheMu.Lock()
	if v.closed {
		v.cacheMu.Unlock()
		return nil
	}

	if err := func() error {
		if utils.RtcIsRTPOrRTCP(data) || !utils.RtcIsSTUN(data) {
			return nil
		}

		var pkt rtcStunPacket
		if err := pkt.UnmarshalBinary(data); err != nil {
			return errors.Wrapf(err, "unmarshal stun packet")
		}

		if s, ok := v.usernames.Load(pkt.Username); ok {
			connection = s
			return nil
		}

		if s, err := v.loadBalancer.LoadWebRTCByUfrag(ctx, pkt.Username); err != nil {
			return errors.Wrapf(err, "load webrtc by ufrag %v", pkt.Username)
		} else {
			var ok bool
			connection, ok = s.(*rtcConnection)
			if !ok || connection == nil {
				return errors.Errorf("invalid WebRTC session")
			}
			if connection.isClosed() {
				return errors.Errorf("WebRTC session closed")
			}
			connection.Initialize(context.WithoutCancel(ctx), v.listener)
			connection.loadBalancer = v.loadBalancer
			logger.Debug(ctx, "Create WebRTC connection by ufrag=%v, stream=%v", pkt.Username, connection.StreamURL)
		}

		if connection != nil {
			v.usernames.Store(pkt.Username, connection)
		}
		return nil
	}(); err != nil {
		v.cacheMu.Unlock()
		return err
	}

	// A STUN binding names its connection by ufrag. A new session can reuse
	// the address of one that ended, so the ufrag wins and the address cache
	// follows it; other packets are found by the address.
	if connection != nil {
		// Keep only the current address for this session; rebinding must not grow
		// the address index forever or remove a replacement session's entry.
		if old := connection.cacheAddress; old != "" && old != addr.String() {
			if current, ok := v.addresses.Load(old); ok && current == connection {
				v.addresses.Delete(old)
			}
		}
		connection.cacheAddress = addr.String()
		if s, ok := v.addresses.Load(addr.String()); !ok || s != connection {
			v.addresses.Store(addr.String(), connection)
		}
	} else if s, ok := v.addresses.Load(addr.String()); ok {
		connection = s
	}

	v.cacheMu.Unlock()

	if connection == nil {
		// TODO: Should logging the dropped packet, only logging the first one for each address.
		return nil
	}

	if err := connection.HandlePacket(addr, data); err != nil {
		return errors.Wrapf(err, "proxy %vB for %v", len(data), connection.StreamURL)
	}

	return nil
}

// Unlike RTMP or HTTP-FLV/TS, the client's requests do not carry the session, so the
// proxy servers share it by the ufrag. A client that moves to another UDP address may
// reach another proxy, which finds the session by the ufrag of the STUN binding request.
type rtcConnection struct {
	mu               stdSync.Mutex
	closed           bool
	lastClientPacket time.Time
	cacheAddress     string           // guarded by the owning server's cacheMu
	origin           *lb.OriginServer // immutable after publication
	readerDone       chan struct{}

	ctx          context.Context
	loadBalancer lb.OriginLoadBalancer

	StreamURL string `json:"stream_url"`
	Ufrag     string `json:"ufrag"`

	// An interface, so a test dialBackendUDP can return a fake.
	backendUDP io.ReadWriteCloser
	// It may change during the session.
	clientUDP net.Addr
	// An interface, so tests can inject a fake.
	listenerUDP net.PacketConn

	// Tests replace it.
	dialBackendUDP func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error)

	// HandlePacket runs for every client packet, STUN keepalives and RTCP
	// feedback included.
	startReader stdSync.Once
}

func newRTCConnection(opts ...func(*rtcConnection)) *rtcConnection {
	v := &rtcConnection{lastClientPacket: time.Now()}

	// DialContext lets ctx abort the DNS lookup; UDP has no handshake.
	v.dialBackendUDP = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
		var d net.Dialer
		return d.DialContext(ctx, "udp", net.JoinHostPort(ip, strconv.Itoa(port)))
	}

	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *rtcConnection) Initialize(ctx context.Context, listener net.PacketConn) *rtcConnection {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.ctx == nil {
		v.ctx = logger.WithContext(ctx)
	}
	if listener != nil {
		v.listenerUDP = listener
	}
	return v
}

func (v *rtcConnection) GetUfrag() string {
	return v.Ufrag
}

// Close is idempotent and closes the socket to unblock the backend reader.
func (v *rtcConnection) Close() { v.mu.Lock(); defer v.mu.Unlock(); v.closeLocked() }
func (v *rtcConnection) closeLocked() {
	if v.closed {
		return
	}
	v.closed = true
	if v.backendUDP != nil {
		_ = v.backendUDP.Close()
	}
}
func (v *rtcConnection) isClosed() bool { v.mu.Lock(); defer v.mu.Unlock(); return v.closed }
func (v *rtcConnection) expire(now time.Time) {
	v.mu.Lock()
	defer v.mu.Unlock()
	if now.Sub(v.lastClientPacket) >= lb.RTCAliveDuration {
		v.closeLocked()
	}
}

func (v *rtcConnection) HandlePacket(addr net.Addr, data []byte) error {
	v.mu.Lock()
	if v.closed {
		v.mu.Unlock()
		return errors.Errorf("WebRTC session closed")
	}
	v.lastClientPacket, v.clientUDP = time.Now(), addr
	ctx := v.ctx
	if err := v.connectBackend(ctx); err != nil {
		v.closeLocked()
		v.mu.Unlock()
		return errors.Wrapf(err, "connect backend for %v", v.StreamURL)
	}
	backend, listener := v.backendUDP, v.listenerUDP

	// A reader per packet leaked tens of thousands of goroutines under WHEP
	// load. WriteTo copies into the kernel, so the next Read can reuse the buffer.
	v.startReader.Do(func() {
		v.readerDone = make(chan struct{})
		go func() {
			defer close(v.readerDone)
			defer v.Close()
			buf := make([]byte, 4096)
			for {
				n, err := backend.Read(buf)
				if err != nil {
					return
				}
				v.mu.Lock()
				client, closed := v.clientUDP, v.closed
				v.mu.Unlock()
				if closed {
					return
				}
				if _, err = listener.WriteTo(buf[:n], client); err != nil {
					return
				}
			}
		}()
	})
	v.mu.Unlock()
	if _, err := backend.Write(data); err != nil {
		v.Close()
		return errors.Wrapf(err, "write to backend %v", v.StreamURL)
	}
	return nil
}

func (v *rtcConnection) connectBackend(ctx context.Context) error {
	if v.backendUDP != nil {
		return nil
	}

	backend := v.origin
	var err error
	if backend == nil {
		backend, err = v.loadBalancer.Pick(ctx, v.StreamURL)
	}
	if err != nil {
		return errors.Wrapf(err, "pick backend")
	}

	if len(backend.RTC) == 0 {
		return errors.Errorf("no udp server")
	}

	_, _, udpPort, err := utils.ParseListenEndpoint(backend.RTC[0])
	if err != nil {
		return errors.Wrapf(err, "parse udp port %v of %v for %v", backend.RTC[0], backend, v.StreamURL)
	}

	// The owner closes this socket on DELETE, idle timeout, I/O failure or shutdown.
	backendUDP, err := v.dialBackendUDP(ctx, backend.IP, int(udpPort))
	if err != nil {
		return errors.Wrapf(err, "dial udp to %v:%v", backend.IP, udpPort)
	}
	v.backendUDP = backendUDP

	return nil
}

// Resource identity includes the full normalized query (including the session
// token), so deleting one viewer cannot end another viewer of the same stream.
func rtcResourceKey(u *url.URL) string { return u.EscapedPath() + "?" + u.Query().Encode() }

func (v *webRTCProxyServer) forgetConnection(c *rtcConnection) {
	// Redis entries already have a 120-second TTL. Memory LB needs explicit
	// deletion; implementations may also remove Redis entries eagerly.
	if remover, ok := v.loadBalancer.(interface {
		DeleteWebRTC(context.Context, string, lb.RTCConnection) error
	}); ok {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		if err := remover.DeleteWebRTC(ctx, c.StreamURL, c); err != nil {
			logger.Warn(ctx, "forget WebRTC session: %v", err)
		}
	}
}

func (v *webRTCProxyServer) evictClosedLocked() {
	active, reaped := 0, 0
	v.usernames.Range(func(key string, c *rtcConnection) bool {
		if c.isClosed() {
			v.usernames.Delete(key)
			v.forgetConnection(c)
			reaped++
		} else {
			active++
		}
		return true
	})
	v.addresses.Range(func(key string, c *rtcConnection) bool {
		if c.isClosed() {
			v.addresses.Delete(key)
		}
		return true
	})
	for key, c := range v.resources {
		if c.isClosed() {
			delete(v.resources, key)
		}
	}
	if reaped > 0 {
		logger.Debug(context.Background(), "WebRTC sessions: active=%v, reaped=%v", active, reaped)
	}
}

func (v *webRTCProxyServer) sweep(now time.Time) {
	v.cacheMu.Lock()
	defer v.cacheMu.Unlock()
	v.usernames.Range(func(_ string, c *rtcConnection) bool { c.expire(now); return true })
	v.evictClosedLocked()
}

// The legacy SRS /rtc/v1/play/ and /rtc/v1/publish/ APIs send the SDP in JSON, such as
// {"sdp":"v=0\r\n...", ...}; WHIP and WHEP send raw SDP.
func unwrapSDPEnvelope(body string) string {
	trimmed := strings.TrimLeft(body, " \t\r\n")
	if !strings.HasPrefix(trimmed, "{") {
		return body
	}
	var env struct {
		SDP string `json:"sdp"`
	}
	if err := json.Unmarshal([]byte(trimmed), &env); err != nil || env.SDP == "" {
		return body
	}
	return env.SDP
}

type rtcICEPair struct {
	RemoteICEUfrag string `json:"remote_ufrag"`
	RemoteICEPwd   string `json:"remote_pwd"`
	LocalICEUfrag  string `json:"local_ufrag"`
	LocalICEPwd    string `json:"local_pwd"`
}

// The client's STUN binding request names the answer's ufrag first, then the offer's.
func (v *rtcICEPair) Ufrag() string {
	return fmt.Sprintf("%v:%v", v.LocalICEUfrag, v.RemoteICEUfrag)
}

type rtcStunPacket struct {
	MessageType uint16
	Username    string
}

func (v *rtcStunPacket) UnmarshalBinary(data []byte) error {
	if len(data) < 20 {
		return errors.Errorf("stun packet too short %v", len(data))
	}

	p := data
	v.MessageType = binary.BigEndian.Uint16(p)
	messageLen := binary.BigEndian.Uint16(p[2:])
	//magicCookie := p[:8]
	//transactionID := p[:20]
	p = p[20:]

	if len(p) != int(messageLen) {
		return errors.Errorf("stun packet length invalid %v != %v", len(data), messageLen)
	}

	for len(p) > 0 {
		if len(p) < 4 {
			return errors.Errorf("stun attribute header invalid %v < 4", len(p))
		}

		typ := binary.BigEndian.Uint16(p)
		length := binary.BigEndian.Uint16(p[2:])
		p = p[4:]

		padding := (4 - int(length)%4) % 4
		if len(p) < int(length)+padding {
			return errors.Errorf("stun attribute length invalid %v < %v+%v", len(p), length, padding)
		}

		value := p[:length]
		p = p[int(length)+padding:]

		switch typ {
		case 0x0006:
			v.Username = string(value)
		}
	}

	return nil
}
