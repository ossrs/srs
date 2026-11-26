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
	"slices"
	"strconv"
	"strings"
	stdSync "sync"
	"time"

	"srsx/internal/clock"
	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/lb"
	"srsx/internal/logger"
	"srsx/internal/sync"
	"srsx/internal/utils"
)

// The idle sweep runs at this interval, or at a quarter of a shorter session TTL.
const rtcIdleCheckInterval = 10 * time.Second

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

	// How long a session lives after its client's last packet: PROXY_RTC_SESSION_TTL, set by Run.
	sessionTTL time.Duration

	usernames sync.Map[string, *rtcConnection]
	// TODO: Support fast search by uint64 address.
	addresses sync.Map[string, *rtcConnection]

	wg     stdSync.WaitGroup
	cancel context.CancelFunc

	// Tests replace backendURL to reach an httptest.Server, and listenUDP.
	backendURL func(backend *lb.OriginServer, r *http.Request) (string, error)

	listenUDP func(ctx context.Context, endpoint string) (net.PacketConn, error)

	// Tests replace it.
	dialBackendUDP func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error)
}

func NewWebRTCProxyServer(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, opts ...func(*webRTCProxyServer)) WebRTCProxyServer {
	v := &webRTCProxyServer{
		environment:  environment,
		loadBalancer: loadBalancer,
		sessionTTL:   lb.DefaultRTCSessionTTL,
		usernames:    sync.NewMap[string, *rtcConnection](),
		addresses:    sync.NewMap[string, *rtcConnection](),
	}

	v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
		saddr, err := net.ResolveUDPAddr("udp", endpoint)
		if err != nil {
			return nil, errors.Wrapf(err, "resolve udp addr %v", endpoint)
		}
		return net.ListenUDP("udp", saddr)
	}

	v.dialBackendUDP = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
		var d net.Dialer
		return d.DialContext(ctx, "udp", net.JoinHostPort(ip, strconv.Itoa(port)))
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
	if v.cancel != nil {
		v.cancel()
	}
	if v.listener != nil {
		_ = v.listener.Close()
	}

	v.wg.Wait()

	v.usernames.Range(func(_ string, c *rtcConnection) bool {
		v.closeConnection(context.Background(), c)
		return true
	})
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

	if resp.StatusCode < http.StatusOK || resp.StatusCode >= http.StatusMultipleChoices {
		return errors.Errorf("proxy api to %v failed, status=%v", backendURL, resp.Status)
	}

	// A WHIP client ends its session by a DELETE to the Location of the answer,
	// which carries no SDP.
	if r.Method == http.MethodDelete {
		// SRS names the session in the Location by its ICE username, which the
		// client's STUN binding requests carry. It answers 200 to a DELETE of a
		// session it does not hold, without checking the token.
		q := r.URL.Query()
		if c, ok := v.usernames.Load(q.Get("session")); ok && c.deleteToken != "" && c.deleteToken == q.Get("token") {
			v.closeConnection(ctx, c)
		}

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
	location, err := url.Parse(resp.Header.Get("Location"))
	if err != nil {
		return errors.Wrapf(err, "parse location %v", resp.Header.Get("Location"))
	}
	if err := v.loadBalancer.StoreWebRTC(ctx, streamURL, newRTCConnection(func(c *rtcConnection) {
		c.loadBalancer = v.loadBalancer
		c.StreamURL, c.Ufrag = streamURL, icePair.Ufrag()
		c.deleteToken = location.Query().Get("token")
		c.dialBackendUDP = v.dialBackendUDP
		c.Initialize(ctx, v.listener)

		c.cacheBy(v.usernames, c.Ufrag)
	})); err != nil {
		return errors.Wrapf(err, "load or store webrtc %v", streamURL)
	}

	// net/http does not send headers changed after WriteHeader, and a WHIP
	// client needs the backend's Location to delete its session.
	copyBackendResponseHeaders(w.Header(), resp.Header)
	if localSDPAnswer != string(b) {
		repairRewrittenResponseHeaders(w.Header(), len(localSDPAnswer))
	}
	w.WriteHeader(resp.StatusCode)

	if _, err = w.Write([]byte(localSDPAnswer)); err != nil {
		return errors.Wrapf(err, "write local sdp answer %v", localSDPAnswer)
	}

	logger.Debug(ctx, "Create WebRTC connection with local answer %vB with ice-ufrag=%v, ice-pwd=%vB",
		len(localSDPAnswer), localICEUfrag, len(localICEPwd))
	return nil
}

func (v *webRTCProxyServer) Run(ctx context.Context) error {
	sessionTTL, err := lb.ParseRTCSessionTTL(v.environment.RTCSessionTTL())
	if err != nil {
		return err
	}
	v.sessionTTL = sessionTTL

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

	ctx, v.cancel = context.WithCancel(ctx)

	// UDP has no close, so a client that leaves without a DELETE only stops sending.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		ticker := time.NewTicker(min(rtcIdleCheckInterval, v.sessionTTL/4))
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case now := <-ticker.C:
				v.expireIdleConnections(now)
			}
		}
	}()

	// TODO: Use more than one listener, such as SO_REUSEPORT sockets each with
	// its own read loop. This one loop reads the packets of every session, so a
	// packet that waits for a lock or takes long to handle holds up all others.
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
			connection = s.(*rtcConnection).Initialize(ctx, v.listener)
			connection.loadBalancer = v.loadBalancer
			logger.Debug(ctx, "Create WebRTC connection by ufrag=%v, stream=%v", pkt.Username, connection.StreamURL)
		}

		if connection != nil {
			connection.cacheBy(v.usernames, pkt.Username)
		}
		return nil
	}(); err != nil {
		return err
	}

	// A STUN binding names its connection by ufrag. A new session can reuse
	// the address of one that ended, so the ufrag wins and the address cache
	// follows it; other packets are found by the address.
	if connection != nil {
		if s, ok := v.addresses.Load(addr.String()); !ok || s != connection {
			connection.cacheBy(v.addresses, addr.String())
		}
	} else if s, ok := v.addresses.Load(addr.String()); ok {
		connection = s
	}

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
	ctx          context.Context
	loadBalancer lb.OriginLoadBalancer

	StreamURL string `json:"stream_url"`
	Ufrag     string `json:"ufrag"`
	// From the Location of the answer, which only the client holds.
	deleteToken string

	// An interface, so tests can inject a fake.
	listenerUDP net.PacketConn

	// Tests replace it.
	dialBackendUDP func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error)

	// HandlePacket runs for every client packet, STUN keepalives and RTCP
	// feedback included.
	startReader stdSync.Once

	// The UDP read loop, the backend reader, and the HTTP and timer goroutines
	// that end the session all reach the fields below.
	mu stdSync.Mutex
	// An interface, so a test dialBackendUDP can return a fake.
	backendUDP io.ReadWriteCloser
	// It may change during the session.
	clientUDP    net.Addr
	closed       bool
	lastPacketAt time.Time
	// A client may send from more than one address, such as the backup pairs
	// that ICE keeps checking.
	cached []rtcCacheEntry
}

// The server finds a session by the ICE username "<answer ufrag>:<offer ufrag>"
// of its STUN binding requests, or by the client address of its other packets.
type rtcCacheEntry struct {
	// The usernames or the addresses of the server.
	index sync.Map[string, *rtcConnection]
	// The ICE username, or the client address as ip:port.
	key string
}

func newRTCConnection(opts ...func(*rtcConnection)) *rtcConnection {
	v := &rtcConnection{lastPacketAt: clock.Now()}

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

func (v *rtcConnection) cacheBy(index sync.Map[string, *rtcConnection], key string) {
	v.mu.Lock()
	defer v.mu.Unlock()

	if v.closed {
		return
	}

	index.Store(key, v)
	entry := rtcCacheEntry{index: index, key: key}
	if !slices.Contains(v.cached, entry) {
		v.cached = append(v.cached, entry)
	}
}

func (v *rtcConnection) Close() bool {
	v.mu.Lock()
	defer v.mu.Unlock()

	if v.closed {
		return false
	}
	v.closed = true

	if v.backendUDP != nil {
		_ = v.backendUDP.Close()
	}
	for _, entry := range v.cached {
		entry.index.CompareAndDelete(entry.key, v)
	}
	return true
}

func (v *rtcConnection) idleSince(now time.Time) time.Duration {
	v.mu.Lock()
	defer v.mu.Unlock()
	return now.Sub(v.lastPacketAt)
}

func (v *rtcConnection) HandlePacket(addr net.Addr, data []byte) error {
	ctx := v.ctx

	backendUDP, err := v.receivedFrom(ctx, addr)
	if err != nil {
		return errors.Wrapf(err, "connect backend for %v", v.StreamURL)
	}

	if backendUDP == nil {
		return nil
	}

	// A reader per packet leaked tens of thousands of goroutines under WHEP
	// load.
	v.startReader.Do(func() {
		go v.readBackend(ctx, backendUDP)
	})

	if _, err := backendUDP.Write(data); err != nil {
		return errors.Wrapf(err, "write to backend %v", v.StreamURL)
	}

	return nil
}

// Returns no backend once the session is closed.
//
// TODO: Give each session a packet queue and a goroutine that handles it, so
// the read loop only puts a packet in the queue. This lock is held while the
// first packet picks and dials the backend, which may wait on Redis, and the
// read loop waits with it.
func (v *rtcConnection) receivedFrom(ctx context.Context, addr net.Addr) (io.ReadWriteCloser, error) {
	v.mu.Lock()
	defer v.mu.Unlock()

	if v.closed {
		return nil, nil
	}
	v.clientUDP, v.lastPacketAt = addr, clock.Now()

	if err := v.connectBackend(ctx); err != nil {
		return nil, err
	}
	return v.backendUDP, nil
}

func (v *rtcConnection) clientAddr() net.Addr {
	v.mu.Lock()
	defer v.mu.Unlock()
	return v.clientUDP
}

func (v *rtcConnection) readBackend(ctx context.Context, backendUDP io.Reader) {
	// WriteTo copies into the kernel, so the next Read can reuse the buffer.
	buf := make([]byte, 4096)
	for {
		n, err := backendUDP.Read(buf)
		if err == io.EOF || utils.IsClosedNetworkError(err) {
			return
		}
		if err != nil {
			// A connected UDP socket reports an ICMP error on the next read, such
			// as port unreachable while the backend restarts.
			logger.Warn(ctx, "read from backend failed, err=%v", err)
			continue
		}

		if _, err = v.listenerUDP.WriteTo(buf[:n], v.clientAddr()); err != nil {
			logger.Warn(ctx, "write to client failed, err=%v", err)
		}
	}
}

func (v *rtcConnection) connectBackend(ctx context.Context) error {
	if v.backendUDP != nil {
		return nil
	}

	backend, err := v.loadBalancer.Pick(ctx, v.StreamURL)
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

	backendUDP, err := v.dialBackendUDP(ctx, backend.IP, int(udpPort))
	if err != nil {
		return errors.Wrapf(err, "dial udp to %v:%v", backend.IP, udpPort)
	}
	v.backendUDP = backendUDP

	return nil
}

func (v *webRTCProxyServer) closeConnection(ctx context.Context, c *rtcConnection) {
	if !c.Close() {
		return
	}

	if err := v.loadBalancer.DeleteWebRTC(ctx, c.StreamURL, c); err != nil {
		logger.Warn(ctx, "delete WebRTC %v of %v failed, err=%v", c.Ufrag, c.StreamURL, err)
	}
}

// A live client sends a STUN consent check every few seconds (RFC 7675), while
// the backend's media to a client that left is no sign of life.
func (v *webRTCProxyServer) expireIdleConnections(now time.Time) {
	v.usernames.Range(func(_ string, c *rtcConnection) bool {
		if c.idleSince(now) >= v.sessionTTL {
			v.closeConnection(context.Background(), c)
		}
		return true
	})
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
