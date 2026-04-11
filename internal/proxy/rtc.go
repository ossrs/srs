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

// WebRTCProxyServer is the proxy for SRS WebRTC server via WHIP or WHEP protocol. It will figure out
// which backend server to proxy to. It will also replace the UDP port to the proxy server's in the
// SDP answer.
type WebRTCProxyServer interface {
	Run(ctx context.Context) error
	Close() error
	HandleApiForWHIP(ctx context.Context, w http.ResponseWriter, r *http.Request) error
	HandleApiForWHEP(ctx context.Context, w http.ResponseWriter, r *http.Request) error
}

type webRTCProxyServer struct {
	// The environment interface.
	environment env.ProxyEnvironment
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer
	// The UDP listener for WebRTC server. Stored as net.PacketConn so tests
	// can inject a fake listener via listenUDP.
	listener net.PacketConn

	// Fast cache for the username to identify the connection.
	// The key is username, the value is the UDP address.
	usernames sync.Map[string, *rtcConnection]
	// Fast cache for the udp address to identify the connection.
	// The key is UDP address, the value is the username.
	// TODO: Support fast earch by uint64 address.
	addresses sync.Map[string, *rtcConnection]

	// The wait group for server.
	wg stdSync.WaitGroup

	// backendURL builds the URL to forward a WHIP/WHEP SDP exchange to a backend
	// SRS server. Defaults to "http://<ip>:<api-port><path>?<query>"; tests may
	// override to redirect requests to an httptest.Server.
	backendURL func(backend *lb.OriginServer, r *http.Request) (string, error)

	// listenUDP opens the UDP listener for the WebRTC server. Defaults to a real
	// net.ListenUDP on the resolved endpoint; tests may override via a functional
	// option to supply a fake listener.
	listenUDP func(ctx context.Context, endpoint string) (net.PacketConn, error)
}

func NewWebRTCProxyServer(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, opts ...func(*webRTCProxyServer)) WebRTCProxyServer {
	v := &webRTCProxyServer{
		environment:  environment,
		loadBalancer: loadBalancer,
		usernames:    sync.NewMap[string, *rtcConnection](),
		addresses:    sync.NewMap[string, *rtcConnection](),
	}

	// Default listenUDP: resolve the endpoint and open a real UDP socket.
	v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
		saddr, err := net.ResolveUDPAddr("udp", endpoint)
		if err != nil {
			return nil, errors.Wrapf(err, "resolve udp addr %v", endpoint)
		}
		return net.ListenUDP("udp", saddr)
	}

	// Default backendURL: validate API endpoint, parse port, format URL preserving
	// the inbound request's path and raw query.
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
	if v.listener != nil {
		_ = v.listener.Close()
	}

	v.wg.Wait()
	return nil
}

func (v *webRTCProxyServer) HandleApiForWHIP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	defer r.Body.Close()
	ctx = logger.WithContext(ctx)

	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Read remote SDP offer from body.
	remoteSDPOffer, err := io.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Debug(ctx, "Got WebRTC WHIP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
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

	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Read remote SDP offer from body.
	remoteSDPOffer, err := io.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Debug(ctx, "Got WebRTC WHEP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
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
	// Resolve the backend URL via the configurable seam (so tests can redirect to
	// an httptest.Server).
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

	if resp.StatusCode != http.StatusOK && resp.StatusCode != http.StatusCreated {
		return errors.Errorf("proxy api to %v failed, status=%v", backendURL, resp.Status)
	}

	// Copy all headers from backend to client.
	w.WriteHeader(resp.StatusCode)
	for k, v := range resp.Header {
		for _, vv := range v {
			w.Header().Add(k, vv)
		}
	}

	// Parse the local SDP answer from backend.
	b, err := io.ReadAll(resp.Body)
	if err != nil {
		return errors.Wrapf(err, "read stream from %v", backendURL)
	}

	// Replace the WebRTC UDP port in answer.
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

	// Fetch the ice-ufrag and ice-pwd from local SDP answer. The legacy SRS
	// /rtc/v1/play/ and /rtc/v1/publish/ APIs wrap the SDP in a JSON envelope
	// like {"sdp":"v=0\r\n..."}, so unwrap it before parsing ICE attributes.
	// The forwarded bytes and the in-body candidate port rewrite still operate
	// on the raw envelope, which is what the client expects to see back.
	remoteICEUfrag, remoteICEPwd, err := utils.ParseIceUfragPwd(unwrapSDPEnvelope(remoteSDPOffer))
	if err != nil {
		return errors.Wrapf(err, "parse remote sdp offer")
	}

	localICEUfrag, localICEPwd, err := utils.ParseIceUfragPwd(unwrapSDPEnvelope(localSDPAnswer))
	if err != nil {
		return errors.Wrapf(err, "parse local sdp answer")
	}

	// Save the new WebRTC connection to LB.
	icePair := &rtcICEPair{
		RemoteICEUfrag: remoteICEUfrag, RemoteICEPwd: remoteICEPwd,
		LocalICEUfrag: localICEUfrag, LocalICEPwd: localICEPwd,
	}
	if err := v.loadBalancer.StoreWebRTC(ctx, streamURL, newRTCConnection(func(c *rtcConnection) {
		c.loadBalancer = v.loadBalancer
		c.StreamURL, c.Ufrag = streamURL, icePair.Ufrag()
		c.Initialize(ctx, v.listener)

		// Cache the connection for fast search by username.
		v.usernames.Store(c.Ufrag, c)
	})); err != nil {
		return errors.Wrapf(err, "load or store webrtc %v", streamURL)
	}

	// Response client with local answer.
	if _, err = w.Write([]byte(localSDPAnswer)); err != nil {
		return errors.Wrapf(err, "write local sdp answer %v", localSDPAnswer)
	}

	logger.Debug(ctx, "Create WebRTC connection with local answer %vB with ice-ufrag=%v, ice-pwd=%vB",
		len(localSDPAnswer), localICEUfrag, len(localICEPwd))
	return nil
}

func (v *webRTCProxyServer) Run(ctx context.Context) error {
	// Parse address to listen.
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

	// Consume all messages from UDP media transport.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		// Reuse a single receive buffer across iterations. handleClientUDP and the
		// downstream HandlePacket consume the slice synchronously (kernel sendto
		// copies bytes; STUN parsing copies the username via string()), so no caller
		// retains the slice past the call.
		buf := make([]byte, 4096)
		for ctx.Err() == nil {
			n, addr, err := listener.ReadFrom(buf)
			if err != nil {
				// If context is canceled or connection is closed, exit gracefully without logging error.
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

	// If STUN binding request, parse the ufrag and identify the connection.
	if err := func() error {
		if utils.RtcIsRTPOrRTCP(data) || !utils.RtcIsSTUN(data) {
			return nil
		}

		var pkt rtcStunPacket
		if err := pkt.UnmarshalBinary(data); err != nil {
			return errors.Wrapf(err, "unmarshal stun packet")
		}

		// Search the connection in fast cache.
		if s, ok := v.usernames.Load(pkt.Username); ok {
			connection = s
			return nil
		}

		// Load connection by username.
		if s, err := v.loadBalancer.LoadWebRTCByUfrag(ctx, pkt.Username); err != nil {
			return errors.Wrapf(err, "load webrtc by ufrag %v", pkt.Username)
		} else {
			connection = s.(*rtcConnection).Initialize(ctx, v.listener)
			connection.loadBalancer = v.loadBalancer
			logger.Debug(ctx, "Create WebRTC connection by ufrag=%v, stream=%v", pkt.Username, connection.StreamURL)
		}

		// Cache connection for fast search.
		if connection != nil {
			v.usernames.Store(pkt.Username, connection)
		}
		return nil
	}(); err != nil {
		return err
	}

	// Search the connection by addr.
	if s, ok := v.addresses.Load(addr.String()); ok {
		connection = s
	} else if connection != nil {
		// Cache the address for fast search.
		v.addresses.Store(addr.String(), connection)
	}

	// If connection is not found, ignore the packet.
	if connection == nil {
		// TODO: Should logging the dropped packet, only logging the first one for each address.
		return nil
	}

	// Proxy the packet to backend.
	if err := connection.HandlePacket(addr, data); err != nil {
		return errors.Wrapf(err, "proxy %vB for %v", len(data), connection.StreamURL)
	}

	return nil
}

// rtcConnection is a WebRTC connection proxy, for both WHIP and WHEP. It represents a WebRTC
// connection, identify by the ufrag in sdp offer/answer and ICE binding request.
//
// It's not like RTMP or HTTP FLV/TS proxy connection, which are stateless and all state is
// in the client request. The rtcConnection is stateful, and need to sync the ufrag between
// proxy servers.
//
// The media transport is UDP, which is also a special thing for WebRTC. So if the client switch
// to another UDP address, it may connect to another WebRTC proxy, then we should discover the
// rtcConnection by the ufrag from the ICE binding request.
type rtcConnection struct {
	// The stream context for WebRTC streaming.
	ctx context.Context
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer

	// The stream URL in vhost/app/stream schema.
	StreamURL string `json:"stream_url"`
	// The ufrag for this WebRTC connection.
	Ufrag string `json:"ufrag"`

	// The UDP connection proxy to backend. Stored as io.ReadWriteCloser so tests
	// can inject a fake connection by overriding dialBackendUDP.
	backendUDP io.ReadWriteCloser
	// The client UDP address. Note that it may change.
	clientUDP net.Addr
	// The listener UDP connection, used to send messages to client. Stored as
	// net.PacketConn so tests can inject a fake listener.
	listenerUDP net.PacketConn

	// dialBackendUDP opens a UDP connection to a backend SRS server. Defaults to a real
	// UDP dial; tests may override via a functional option to supply a fake connection.
	dialBackendUDP func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error)

	// Guards the spawn of the backend->client reader goroutine. HandlePacket is
	// called on every inbound client packet (STUN keepalives + RTCP feedback at
	// steady state) but the reader must only start once per connection.
	startReader stdSync.Once
}

func newRTCConnection(opts ...func(*rtcConnection)) *rtcConnection {
	v := &rtcConnection{}

	// Default dial: a real UDP connection to the backend. Uses Dialer.DialContext
	// so ctx cancellation/deadline aborts DNS resolution (UDP itself has no handshake).
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

func (v *rtcConnection) HandlePacket(addr net.Addr, data []byte) error {
	ctx := v.ctx

	// Update the current UDP address.
	v.clientUDP = addr

	// Start the UDP proxy to backend.
	if err := v.connectBackend(ctx); err != nil {
		return errors.Wrapf(err, "connect backend for %v", v.StreamURL)
	}

	// Proxy client message to backend.
	if v.backendUDP == nil {
		return nil
	}

	// Spawn the backend->client reader exactly once per connection. Previously
	// this goroutine was launched unconditionally here on every inbound client
	// packet, which leaked tens of thousands of goroutines under steady-state
	// WHEP load (STUN keepalives + RTCP feedback). The buffer is reused across
	// iterations: WriteTo copies into the kernel before returning, so the next
	// Read can safely overwrite.
	v.startReader.Do(func() {
		go func() {
			buf := make([]byte, 4096)
			for ctx.Err() == nil {
				n, err := v.backendUDP.Read(buf)
				if err != nil {
					// TODO: If backend server closed unexpectedly, we should notice the stream to quit.
					logger.Warn(ctx, "read from backend failed, err=%v", err)
					return
				}

				if _, err = v.listenerUDP.WriteTo(buf[:n], v.clientUDP); err != nil {
					// TODO: If backend server closed unexpectedly, we should notice the stream to quit.
					logger.Warn(ctx, "write to client failed, err=%v", err)
					return
				}
			}
		}()
	})

	if _, err := v.backendUDP.Write(data); err != nil {
		return errors.Wrapf(err, "write to backend %v", v.StreamURL)
	}

	return nil
}

func (v *rtcConnection) connectBackend(ctx context.Context) error {
	if v.backendUDP != nil {
		return nil
	}

	// Pick a backend SRS server to proxy the RTC stream.
	backend, err := v.loadBalancer.Pick(ctx, v.StreamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend")
	}

	// Parse UDP port from backend.
	if len(backend.RTC) == 0 {
		return errors.Errorf("no udp server")
	}

	_, _, udpPort, err := utils.ParseListenEndpoint(backend.RTC[0])
	if err != nil {
		return errors.Wrapf(err, "parse udp port %v of %v for %v", backend.RTC[0], backend, v.StreamURL)
	}

	// Connect to backend SRS server via UDP client.
	// TODO: FIXME: Support close the connection when timeout or DTLS alert.
	backendUDP, err := v.dialBackendUDP(ctx, backend.IP, int(udpPort))
	if err != nil {
		return errors.Wrapf(err, "dial udp to %v:%v", backend.IP, udpPort)
	}
	v.backendUDP = backendUDP

	return nil
}

// unwrapSDPEnvelope returns the SDP string carried inside the legacy SRS RTC
// JSON envelope used by /rtc/v1/play/ and /rtc/v1/publish/, e.g. body of the
// form {"sdp":"v=0\r\n...", ...}. For standards-based WHIP/WHEP bodies (raw
// SDP), or any input we can't recognise, the original body is returned
// unchanged so the caller can parse it as raw SDP.
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
	// The remote ufrag, used for ICE username and session id.
	RemoteICEUfrag string `json:"remote_ufrag"`
	// The remote pwd, used for ICE password.
	RemoteICEPwd string `json:"remote_pwd"`
	// The local ufrag, used for ICE username and session id.
	LocalICEUfrag string `json:"local_ufrag"`
	// The local pwd, used for ICE password.
	LocalICEPwd string `json:"local_pwd"`
}

// Generate the ICE ufrag for the WebRTC streaming, format is remote-ufrag:local-ufrag.
func (v *rtcICEPair) Ufrag() string {
	return fmt.Sprintf("%v:%v", v.LocalICEUfrag, v.RemoteICEUfrag)
}

type rtcStunPacket struct {
	// The stun message type.
	MessageType uint16
	// The stun username, or ufrag.
	Username string
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
		typ := binary.BigEndian.Uint16(p)
		length := binary.BigEndian.Uint16(p[2:])
		p = p[4:]

		if len(p) < int(length) {
			return errors.Errorf("stun attribute length invalid %v < %v", len(p), length)
		}

		value := p[:length]
		p = p[length:]

		if length%4 != 0 {
			p = p[4-length%4:]
		}

		switch typ {
		case 0x0006:
			v.Username = string(value)
		}
	}

	return nil
}
