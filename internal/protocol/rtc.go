// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package protocol

import (
	"context"
	"encoding/binary"
	"fmt"
	"io/ioutil"
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

// srsWebRTCServer is the proxy for SRS WebRTC server via WHIP or WHEP protocol. It will figure out
// which backend server to proxy to. It will also replace the UDP port to the proxy server's in the
// SDP answer.
type srsWebRTCServer struct {
	// The environment interface.
	environment env.Environment
	// The UDP listener for WebRTC server.
	listener *net.UDPConn

	// Fast cache for the username to identify the connection.
	// The key is username, the value is the UDP address.
	usernames sync.Map[string, *RTCConnection]
	// Fast cache for the udp address to identify the connection.
	// The key is UDP address, the value is the username.
	// TODO: Support fast earch by uint64 address.
	addresses sync.Map[string, *RTCConnection]

	// The wait group for server.
	wg stdSync.WaitGroup
}

func NewSRSWebRTCServer(environment env.Environment, opts ...func(*srsWebRTCServer)) *srsWebRTCServer {
	v := &srsWebRTCServer{environment: environment}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *srsWebRTCServer) Close() error {
	if v.listener != nil {
		_ = v.listener.Close()
	}

	v.wg.Wait()
	return nil
}

func (v *srsWebRTCServer) HandleApiForWHIP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	defer r.Body.Close()
	ctx = logger.WithContext(ctx)

	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Read remote SDP offer from body.
	remoteSDPOffer, err := ioutil.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Df(ctx, "Got WebRTC WHIP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := lb.SrsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.proxyApiToBackend(ctx, w, r, backend, string(remoteSDPOffer), streamURL); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *srsWebRTCServer) HandleApiForWHEP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	defer r.Body.Close()
	ctx = logger.WithContext(ctx)

	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Read remote SDP offer from body.
	remoteSDPOffer, err := ioutil.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Df(ctx, "Got WebRTC WHEP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := lb.SrsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.proxyApiToBackend(ctx, w, r, backend, string(remoteSDPOffer), streamURL); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *srsWebRTCServer) proxyApiToBackend(
	ctx context.Context, w http.ResponseWriter, r *http.Request, backend *lb.SRSServer,
	remoteSDPOffer string, streamURL string,
) error {
	// Parse HTTP port from backend.
	if len(backend.API) == 0 {
		return errors.Errorf("no http api server")
	}

	var apiPort int
	if iv, err := strconv.ParseInt(backend.API[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse http port %v", backend.API[0])
	} else {
		apiPort = int(iv)
	}

	// Connect to backend SRS server via HTTP client.
	backendURL := fmt.Sprintf("http://%v:%v%s", backend.IP, apiPort, r.URL.Path)
	if r.URL.RawQuery != "" {
		backendURL += "?" + r.URL.RawQuery
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
	b, err := ioutil.ReadAll(resp.Body)
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

	// Fetch the ice-ufrag and ice-pwd from local SDP answer.
	remoteICEUfrag, remoteICEPwd, err := utils.ParseIceUfragPwd(remoteSDPOffer)
	if err != nil {
		return errors.Wrapf(err, "parse remote sdp offer")
	}

	localICEUfrag, localICEPwd, err := utils.ParseIceUfragPwd(localSDPAnswer)
	if err != nil {
		return errors.Wrapf(err, "parse local sdp answer")
	}

	// Save the new WebRTC connection to LB.
	icePair := &RTCICEPair{
		RemoteICEUfrag: remoteICEUfrag, RemoteICEPwd: remoteICEPwd,
		LocalICEUfrag: localICEUfrag, LocalICEPwd: localICEPwd,
	}
	if err := lb.SrsLoadBalancer.StoreWebRTC(ctx, streamURL, NewRTCConnection(func(c *RTCConnection) {
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

	logger.Df(ctx, "Create WebRTC connection with local answer %vB with ice-ufrag=%v, ice-pwd=%vB",
		len(localSDPAnswer), localICEUfrag, len(localICEPwd))
	return nil
}

func (v *srsWebRTCServer) Run(ctx context.Context) error {
	// Parse address to listen.
	endpoint := v.environment.WebRTCServer()
	if !strings.Contains(endpoint, ":") {
		endpoint = fmt.Sprintf(":%v", endpoint)
	}

	saddr, err := net.ResolveUDPAddr("udp", endpoint)
	if err != nil {
		return errors.Wrapf(err, "resolve udp addr %v", endpoint)
	}

	listener, err := net.ListenUDP("udp", saddr)
	if err != nil {
		return errors.Wrapf(err, "listen udp %v", saddr)
	}
	v.listener = listener
	logger.Df(ctx, "WebRTC server listen at %v", saddr)

	// Consume all messages from UDP media transport.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		for ctx.Err() == nil {
			buf := make([]byte, 4096)
			n, caddr, err := listener.ReadFromUDP(buf)
			if err != nil {
				// If context is canceled or connection is closed, exit gracefully without logging error.
				if ctx.Err() != nil || utils.IsClosedNetworkError(err) {
					logger.Df(ctx, "WebRTC server done")
					return
				}
				// TODO: If WebRTC server closed unexpectedly, we should notice the main loop to quit.
				logger.Wf(ctx, "WebRTC read from udp failed, err=%+v", err)
				time.Sleep(1 * time.Second)
				continue
			}

			if err := v.handleClientUDP(ctx, caddr, buf[:n]); err != nil {
				logger.Wf(ctx, "WebRTC handle udp %vB failed, addr=%v, err=%+v", n, caddr, err)
			}
		}
	}()

	return nil
}

func (v *srsWebRTCServer) handleClientUDP(ctx context.Context, addr *net.UDPAddr, data []byte) error {
	var connection *RTCConnection

	// If STUN binding request, parse the ufrag and identify the connection.
	if err := func() error {
		if utils.RtcIsRTPOrRTCP(data) || !utils.RtcIsSTUN(data) {
			return nil
		}

		var pkt RTCStunPacket
		if err := pkt.UnmarshalBinary(data); err != nil {
			return errors.Wrapf(err, "unmarshal stun packet")
		}

		// Search the connection in fast cache.
		if s, ok := v.usernames.Load(pkt.Username); ok {
			connection = s
			return nil
		}

		// Load connection by username.
		if s, err := lb.SrsLoadBalancer.LoadWebRTCByUfrag(ctx, pkt.Username); err != nil {
			return errors.Wrapf(err, "load webrtc by ufrag %v", pkt.Username)
		} else {
			connection = s.(*RTCConnection).Initialize(ctx, v.listener)
			logger.Df(ctx, "Create WebRTC connection by ufrag=%v, stream=%v", pkt.Username, connection.StreamURL)
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

// RTCConnection is a WebRTC connection proxy, for both WHIP and WHEP. It represents a WebRTC
// connection, identify by the ufrag in sdp offer/answer and ICE binding request.
//
// It's not like RTMP or HTTP FLV/TS proxy connection, which are stateless and all state is
// in the client request. The RTCConnection is stateful, and need to sync the ufrag between
// proxy servers.
//
// The media transport is UDP, which is also a special thing for WebRTC. So if the client switch
// to another UDP address, it may connect to another WebRTC proxy, then we should discover the
// RTCConnection by the ufrag from the ICE binding request.
type RTCConnection struct {
	// The stream context for WebRTC streaming.
	ctx context.Context

	// The stream URL in vhost/app/stream schema.
	StreamURL string `json:"stream_url"`
	// The ufrag for this WebRTC connection.
	Ufrag string `json:"ufrag"`

	// The UDP connection proxy to backend.
	backendUDP *net.UDPConn
	// The client UDP address. Note that it may change.
	clientUDP *net.UDPAddr
	// The listener UDP connection, used to send messages to client.
	listenerUDP *net.UDPConn
}

func NewRTCConnection(opts ...func(*RTCConnection)) *RTCConnection {
	v := &RTCConnection{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *RTCConnection) Initialize(ctx context.Context, listener *net.UDPConn) *RTCConnection {
	if v.ctx == nil {
		v.ctx = logger.WithContext(ctx)
	}
	if listener != nil {
		v.listenerUDP = listener
	}
	return v
}

func (v *RTCConnection) GetUfrag() string {
	return v.Ufrag
}

func (v *RTCConnection) HandlePacket(addr *net.UDPAddr, data []byte) error {
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

	// Proxy all messages from backend to client.
	go func() {
		for ctx.Err() == nil {
			buf := make([]byte, 4096)
			n, _, err := v.backendUDP.ReadFromUDP(buf)
			if err != nil {
				// TODO: If backend server closed unexpectedly, we should notice the stream to quit.
				logger.Wf(ctx, "read from backend failed, err=%v", err)
				break
			}

			if _, err = v.listenerUDP.WriteToUDP(buf[:n], v.clientUDP); err != nil {
				// TODO: If backend server closed unexpectedly, we should notice the stream to quit.
				logger.Wf(ctx, "write to client failed, err=%v", err)
				break
			}
		}
	}()

	if _, err := v.backendUDP.Write(data); err != nil {
		return errors.Wrapf(err, "write to backend %v", v.StreamURL)
	}

	return nil
}

func (v *RTCConnection) connectBackend(ctx context.Context) error {
	if v.backendUDP != nil {
		return nil
	}

	// Pick a backend SRS server to proxy the RTC stream.
	backend, err := lb.SrsLoadBalancer.Pick(ctx, v.StreamURL)
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
	backendAddr := net.UDPAddr{IP: net.ParseIP(backend.IP), Port: int(udpPort)}
	if backendUDP, err := net.DialUDP("udp", nil, &backendAddr); err != nil {
		return errors.Wrapf(err, "dial udp to %v", backendAddr)
	} else {
		v.backendUDP = backendUDP
	}

	return nil
}

type RTCICEPair struct {
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
func (v *RTCICEPair) Ufrag() string {
	return fmt.Sprintf("%v:%v", v.LocalICEUfrag, v.RemoteICEUfrag)
}

type RTCStunPacket struct {
	// The stun message type.
	MessageType uint16
	// The stun username, or ufrag.
	Username string
}

func (v *RTCStunPacket) UnmarshalBinary(data []byte) error {
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
