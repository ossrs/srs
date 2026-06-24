// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
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

// srsSRTProxyServer is the proxy for SRS server via SRT. It will figure out which backend server to
// proxy to. It only parses the SRT handshake messages, parses the stream id, and proxy to the
// backend server.
type srsSRTProxyServer struct {
	// The environment interface.
	environment env.ProxyEnvironment
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer
	// The UDP listener for SRT server. Stored as net.PacketConn so tests
	// can inject a fake listener via listenUDP.
	listener net.PacketConn

	// The SRT connections, identify by the socket ID.
	sockets sync.Map[uint32, *SRTConnection]
	// The system start time.
	start time.Time

	// The wait group for server.
	wg stdSync.WaitGroup

	// listenUDP opens the UDP listener for the SRT server. Defaults to a real
	// net.ListenUDP on the resolved endpoint; tests may override via a functional
	// option to supply a fake listener.
	listenUDP func(ctx context.Context, endpoint string) (net.PacketConn, error)
}

func NewSRSSRTProxyServer(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, opts ...func(*srsSRTProxyServer)) *srsSRTProxyServer {
	v := &srsSRTProxyServer{
		environment:  environment,
		loadBalancer: loadBalancer,
		start:        time.Now(),
		sockets:      sync.NewMap[uint32, *SRTConnection](),
	}

	// Default listenUDP: resolve the endpoint and open a real UDP socket.
	v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
		saddr, err := net.ResolveUDPAddr("udp", endpoint)
		if err != nil {
			return nil, errors.Wrapf(err, "resolve udp addr %v", endpoint)
		}
		return net.ListenUDP("udp", saddr)
	}

	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *srsSRTProxyServer) Close() error {
	if v.listener != nil {
		_ = v.listener.Close()
	}

	v.wg.Wait()
	return nil
}

func (v *srsSRTProxyServer) Run(ctx context.Context) error {
	// Parse address to listen.
	endpoint := v.environment.SRTServer()
	if !strings.Contains(endpoint, ":") {
		endpoint = ":" + endpoint
	}

	listener, err := v.listenUDP(ctx, endpoint)
	if err != nil {
		return errors.Wrapf(err, "listen udp %v", endpoint)
	}
	v.listener = listener
	logger.Debug(ctx, "SRT server listen at %v", listener.LocalAddr())

	// Consume all messages from UDP media transport.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		// Reuse a single receive buffer across iterations. SRTHandshakePacket.UnmarshalBinary
		// clones ExtraData, and backendUDP.Write copies into the kernel before returning, so
		// no caller retains a reference to this slice past the call.
		buf := make([]byte, 4096)
		for ctx.Err() == nil {
			n, caddr, err := v.listener.ReadFrom(buf)
			if err != nil {
				// If context is canceled or connection is closed, exit gracefully without logging error.
				if ctx.Err() != nil || utils.IsClosedNetworkError(err) {
					logger.Debug(ctx, "SRT server done")
					return
				}
				// TODO: If SRT server closed unexpectedly, we should notice the main loop to quit.
				logger.Warn(ctx, "SRT read from udp failed, err=%+v", err)
				time.Sleep(1 * time.Second)
				continue
			}

			if err := v.handleClientUDP(ctx, caddr, buf[:n]); err != nil {
				logger.Warn(ctx, "SRT handle udp %vB failed, addr=%v, err=%+v", n, caddr, err)
			}
		}
	}()

	return nil
}

func (v *srsSRTProxyServer) handleClientUDP(ctx context.Context, addr net.Addr, data []byte) error {
	socketID := utils.SrtParseSocketID(data)

	var pkt *SRTHandshakePacket
	if utils.SrtIsHandshake(data) {
		pkt = &SRTHandshakePacket{}
		if err := pkt.UnmarshalBinary(data); err != nil {
			return err
		}

		if socketID == 0 {
			socketID = pkt.SRTSocketID
		}
	}

	conn, ok := v.sockets.LoadOrStore(socketID, NewSRTConnection(func(c *SRTConnection) {
		c.ctx = logger.WithContext(ctx)
		c.listenerUDP, c.socketID = v.listener, socketID
		c.loadBalancer = v.loadBalancer
		c.start = v.start
	}))

	ctx = conn.ctx
	if !ok {
		logger.Debug(ctx, "Create new SRT connection skt=%v", socketID)
	}

	if newSocketID, err := conn.HandlePacket(pkt, addr, data); err != nil {
		return errors.Wrapf(err, "handle packet")
	} else if newSocketID != 0 && newSocketID != socketID {
		// The connection may use a new socket ID.
		// TODO: FIXME: Should cleanup the dead SRT connection.
		v.sockets.Store(newSocketID, conn)
	}

	return nil
}

// SRTConnection is an SRT connection proxy, for both caller and listener. It represents an SRT
// connection, identify by the socket ID.
//
// It's similar to RTMP or HTTP FLV/TS proxy connection, which are stateless and all state is in
// the client request. The SRTConnection is stateless, and no need to sync between proxy servers.
//
// Unlike the WebRTC connection, SRTConnection does not support address changes. This means the
// client should never switch to another network or port. If this occurs, the client may be served
// by a different proxy server and fail because the other proxy server cannot identify the client.
type SRTConnection struct {
	// The stream context for SRT connection.
	ctx context.Context
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer

	// The current socket ID.
	socketID uint32

	// The UDP connection proxy to backend. Stored as io.ReadWriteCloser so tests
	// can inject a fake connection by overriding dialBackendUDP.
	backendUDP io.ReadWriteCloser
	// The listener UDP connection, used to send messages to client. Stored as
	// net.PacketConn so tests can inject a fake listener.
	listenerUDP net.PacketConn

	// Listener start time.
	start time.Time

	// Handshake packets with client.
	handshake0 *SRTHandshakePacket
	handshake1 *SRTHandshakePacket
	handshake2 *SRTHandshakePacket
	handshake3 *SRTHandshakePacket

	// Guards the spawn of the backend->client reader goroutine. Keeps the spawn
	// idempotent if the client retries handshake 2 (e.g. because our handshake 3
	// was dropped) and re-enters the handshake path.
	startReader stdSync.Once

	// dialBackendUDP opens a UDP connection to a backend SRS server. Defaults to a real
	// UDP dial; tests may override via a functional option to supply a fake connection.
	dialBackendUDP func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error)
}

func NewSRTConnection(opts ...func(*SRTConnection)) *SRTConnection {
	v := &SRTConnection{}

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

func (v *SRTConnection) HandlePacket(pkt *SRTHandshakePacket, addr net.Addr, data []byte) (uint32, error) {
	ctx := v.ctx

	// If not handshake, try to proxy to backend directly.
	if pkt == nil {
		// Proxy client message to backend.
		if v.backendUDP != nil {
			if _, err := v.backendUDP.Write(data); err != nil {
				return v.socketID, errors.Wrapf(err, "write to backend")
			}
		}

		return v.socketID, nil
	}

	// Handle handshake messages.
	if err := v.handleHandshake(ctx, pkt, addr, data); err != nil {
		return v.socketID, errors.Wrapf(err, "handle handshake %v", pkt)
	}

	return v.socketID, nil
}

func (v *SRTConnection) handleHandshake(ctx context.Context, pkt *SRTHandshakePacket, addr net.Addr, data []byte) error {
	// Handle handshake 0 and 1 messages.
	if pkt.SynCookie == 0 {
		// Save handshake 0 packet.
		v.handshake0 = pkt
		logger.Debug(ctx, "SRT Handshake 0: %v", v.handshake0)

		// Response handshake 1.
		v.handshake1 = &SRTHandshakePacket{
			ControlFlag:     pkt.ControlFlag,
			ControlType:     0,
			SubType:         0,
			AdditionalInfo:  0,
			Timestamp:       uint32(time.Since(v.start).Microseconds()),
			SocketID:        pkt.SRTSocketID,
			Version:         5,
			EncryptionField: 0,
			ExtensionField:  0x4A17,
			InitSequence:    pkt.InitSequence,
			MTU:             pkt.MTU,
			FlowWindow:      pkt.FlowWindow,
			HandshakeType:   1,
			SRTSocketID:     pkt.SRTSocketID,
			SynCookie:       0x418d5e4e,
			PeerIP:          net.ParseIP("127.0.0.1"),
		}
		logger.Debug(ctx, "SRT Handshake 1: %v", v.handshake1)

		if b, err := v.handshake1.MarshalBinary(); err != nil {
			return errors.Wrapf(err, "marshal handshake 1")
		} else if _, err = v.listenerUDP.WriteTo(b, addr); err != nil {
			return errors.Wrapf(err, "write handshake 1")
		}

		return nil
	}

	// Handle handshake 2 and 3 messages.
	// Parse stream id from packet.
	streamID, err := pkt.StreamID()
	if err != nil {
		return errors.Wrapf(err, "parse stream id")
	}

	// Save handshake packet.
	v.handshake2 = pkt
	logger.Debug(ctx, "SRT Handshake 2: %v, sid=%v", v.handshake2, streamID)

	// Start the UDP proxy to backend.
	if err := v.connectBackend(ctx, streamID); err != nil {
		return errors.Wrapf(err, "connect backend for %v", streamID)
	}

	// Proxy client message to backend.
	if v.backendUDP == nil {
		return errors.Errorf("no backend for %v", streamID)
	}

	// Proxy handshake 0 to backend server.
	if b, err := v.handshake0.MarshalBinary(); err != nil {
		return errors.Wrapf(err, "marshal handshake 0")
	} else if _, err = v.backendUDP.Write(b); err != nil {
		return errors.Wrapf(err, "write handshake 0")
	}
	logger.Debug(ctx, "Proxy send handshake 0: %v", v.handshake0)

	// Read handshake 1 from backend server.
	b := make([]byte, 4096)
	handshake1p := &SRTHandshakePacket{}
	if nn, err := v.backendUDP.Read(b); err != nil {
		return errors.Wrapf(err, "read handshake 1")
	} else if err := handshake1p.UnmarshalBinary(b[:nn]); err != nil {
		return errors.Wrapf(err, "unmarshal handshake 1")
	}
	logger.Debug(ctx, "Proxy got handshake 1: %v", handshake1p)

	// Proxy handshake 2 to backend server.
	handshake2p := *v.handshake2
	handshake2p.SynCookie = handshake1p.SynCookie
	if b, err := handshake2p.MarshalBinary(); err != nil {
		return errors.Wrapf(err, "marshal handshake 2")
	} else if _, err = v.backendUDP.Write(b); err != nil {
		return errors.Wrapf(err, "write handshake 2")
	}
	logger.Debug(ctx, "Proxy send handshake 2: %v", handshake2p)

	// Read handshake 3 from backend server.
	handshake3p := &SRTHandshakePacket{}
	if nn, err := v.backendUDP.Read(b); err != nil {
		return errors.Wrapf(err, "read handshake 3")
	} else if err := handshake3p.UnmarshalBinary(b[:nn]); err != nil {
		return errors.Wrapf(err, "unmarshal handshake 3")
	}
	logger.Debug(ctx, "Proxy got handshake 3: %v", handshake3p)

	// Response handshake 3 to client. Copy so rewriting the cookie below does
	// not mutate the struct just decoded from the backend.
	handshake3c := *handshake3p
	v.handshake3 = &handshake3c
	v.handshake3.SynCookie = v.handshake1.SynCookie
	v.socketID = handshake3p.SRTSocketID
	logger.Debug(ctx, "Handshake 3: %v", v.handshake3)

	if b, err := v.handshake3.MarshalBinary(); err != nil {
		return errors.Wrapf(err, "marshal handshake 3")
	} else if _, err = v.listenerUDP.WriteTo(b, addr); err != nil {
		return errors.Wrapf(err, "write handshake 3")
	}

	// Start a goroutine to proxy message from backend to client. The Once guard
	// keeps the spawn idempotent if the client retries handshake 2 (because our
	// handshake 3 was lost); the existing reader keeps running and we don't want
	// a second one racing it on backendUDP.Read.
	// TODO: FIXME: Support close the connection when timeout or client disconnected.
	v.startReader.Do(func() {
		go func() {
			buf := make([]byte, 4096)
			for ctx.Err() == nil {
				nn, err := v.backendUDP.Read(buf)
				if err != nil {
					// TODO: If backend server closed unexpectedly, we should notice the stream to quit.
					logger.Warn(ctx, "read from backend failed, err=%v", err)
					return
				}
				if _, err = v.listenerUDP.WriteTo(buf[:nn], addr); err != nil {
					// TODO: If backend server closed unexpectedly, we should notice the stream to quit.
					logger.Warn(ctx, "write to client failed, err=%v", err)
					return
				}
			}
		}()
	})
	return nil
}

func (v *SRTConnection) connectBackend(ctx context.Context, streamID string) error {
	if v.backendUDP != nil {
		return nil
	}

	// Parse stream id to host and resource.
	host, resource, err := utils.ParseSRTStreamID(streamID)
	if err != nil {
		return errors.Wrapf(err, "parse stream id %v", streamID)
	}

	if host == "" {
		host = "localhost"
	}

	streamURL, err := utils.BuildStreamURL(fmt.Sprintf("srt://%v/%v", host, resource))
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", streamID)
	}

	// Pick a backend SRS server to proxy the SRT stream.
	backend, err := v.loadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	// Parse UDP port from backend.
	if len(backend.SRT) == 0 {
		return errors.Errorf("no udp server %v for %v", backend, streamURL)
	}

	_, _, udpPort, err := utils.ParseListenEndpoint(backend.SRT[0])
	if err != nil {
		return errors.Wrapf(err, "parse udp port %v of %v for %v", backend.SRT[0], backend, streamURL)
	}

	// Connect to backend SRS server via UDP client.
	// TODO: FIXME: Support close the connection when timeout or client disconnected.
	backendUDP, err := v.dialBackendUDP(ctx, backend.IP, int(udpPort))
	if err != nil {
		return errors.Wrapf(err, "dial udp to %v:%v of %v for %v", backend.IP, udpPort, backend, streamURL)
	}
	v.backendUDP = backendUDP

	return nil
}

// See https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-3.2
// See https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-3.2.1
type SRTHandshakePacket struct {
	// F: 1 bit.  Packet Type Flag.  The control packet has this flag set to
	//      "1".  The data packet has this flag set to "0".
	ControlFlag uint8
	// Control Type: 15 bits.  Control Packet Type.  The use of these bits
	//      is determined by the control packet type definition.
	// Handshake control packets (Control Type = 0x0000) are used to
	//   exchange peer configurations, to agree on connection parameters, and
	//   to establish a connection.
	ControlType uint16
	// Subtype: 16 bits.  This field specifies an additional subtype for
	//      specific packets.
	SubType uint16
	// Type-specific Information: 32 bits.  The use of this field depends on
	//      the particular control packet type.  Handshake packets do not use
	//      this field.
	AdditionalInfo uint32
	// Timestamp: 32 bits.
	Timestamp uint32
	// Destination Socket ID: 32 bits.
	SocketID uint32

	// Version: 32 bits.  A base protocol version number.  Currently used
	//      values are 4 and 5.  Values greater than 5 are reserved for future
	//      use.
	Version uint32
	// Encryption Field: 16 bits.  Block cipher family and key size.  The
	//      values of this field are described in Table 2.  The default value
	//      is AES-128.
	// 0     |  No Encryption Advertised
	// 2     |          AES-128
	// 3     |          AES-192
	// 4     |          AES-256
	EncryptionField uint16
	// Extension Field: 16 bits.  This field is message specific extension
	//      related to Handshake Type field.  The value MUST be set to 0
	//      except for the following cases.  (1) If the handshake control
	//      packet is the INDUCTION message, this field is sent back by the
	//      Listener. (2) In the case of a CONCLUSION message, this field
	//      value should contain a combination of Extension Type values.
	// 0x00000001 | HSREQ
	// 0x00000002 | KMREQ
	// 0x00000004 | CONFIG
	// 0x4A17 if HandshakeType is INDUCTION, see https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-4.3.1.1
	ExtensionField uint16
	// Initial Packet Sequence Number: 32 bits.  The sequence number of the
	//      very first data packet to be sent.
	InitSequence uint32
	// Maximum Transmission Unit Size: 32 bits.  This value is typically set
	//      to 1500, which is the default Maximum Transmission Unit (MTU) size
	//      for Ethernet, but can be less.
	MTU uint32
	// Maximum Flow Window Size: 32 bits.  The value of this field is the
	//      maximum number of data packets allowed to be "in flight" (i.e. the
	//      number of sent packets for which an ACK control packet has not yet
	//      been received).
	FlowWindow uint32
	// Handshake Type: 32 bits.  This field indicates the handshake packet
	//      type.
	// 0xFFFFFFFD |      DONE
	// 0xFFFFFFFE |   AGREEMENT
	// 0xFFFFFFFF |   CONCLUSION
	// 0x00000000 |    WAVEHAND
	// 0x00000001 |   INDUCTION
	HandshakeType uint32
	// SRT Socket ID: 32 bits.  This field holds the ID of the source SRT
	//      socket from which a handshake packet is issued.
	SRTSocketID uint32
	// SYN Cookie: 32 bits.  Randomized value for processing a handshake.
	//      The value of this field is specified by the handshake message
	//      type.
	SynCookie uint32
	// Peer IP Address: 128 bits.  IPv4 or IPv6 address of the packet's
	//      sender.  The value consists of four 32-bit fields.
	PeerIP net.IP
	// Extensions.
	// Extension Type: 16 bits.  The value of this field is used to process
	//      an integrated handshake.  Each extension can have a pair of
	//      request and response types.
	// Extension Length: 16 bits.  The length of the Extension Contents
	//      field in four-byte blocks.
	// Extension Contents: variable length.  The payload of the extension.
	ExtraData []byte
}

func (v *SRTHandshakePacket) IsData() bool {
	return v.ControlFlag == 0x00
}

func (v *SRTHandshakePacket) IsControl() bool {
	return v.ControlFlag == 0x80
}

func (v *SRTHandshakePacket) IsHandshake() bool {
	return v.IsControl() && v.ControlType == 0x00 && v.SubType == 0x00
}

func (v *SRTHandshakePacket) StreamID() (string, error) {
	p := v.ExtraData
	for {
		if len(p) < 2 {
			return "", errors.Errorf("Require 2 bytes, actual=%v, extra=%v", len(p), len(v.ExtraData))
		}

		extType := binary.BigEndian.Uint16(p)
		extSize := binary.BigEndian.Uint16(p[2:])
		p = p[4:]

		if len(p) < int(extSize*4) {
			return "", errors.Errorf("Require %v bytes, actual=%v, extra=%v", extSize*4, len(p), len(v.ExtraData))
		}

		// Ignore other packets except stream id.
		if extType != 0x05 {
			p = p[extSize*4:]
			continue
		}

		// We must copy it, because we will decode the stream id.
		data := append([]byte{}, p[:extSize*4]...)

		// Reverse the stream id encoded in little-endian to big-endian.
		for i := 0; i < len(data); i += 4 {
			value := binary.LittleEndian.Uint32(data[i:])
			binary.BigEndian.PutUint32(data[i:], value)
		}

		// Trim the trailing zero bytes.
		data = bytes.TrimRight(data, "\x00")
		return string(data), nil
	}
}

func (v *SRTHandshakePacket) String() string {
	return fmt.Sprintf("Control=%v, CType=%v, SType=%v, Timestamp=%v, SocketID=%v, Version=%v, Encrypt=%v, Extension=%v, InitSequence=%v, MTU=%v, FlowWnd=%v, HSType=%v, SRTSocketID=%v, Cookie=%v, Peer=%vB, Extra=%vB",
		v.IsControl(), v.ControlType, v.SubType, v.Timestamp, v.SocketID, v.Version, v.EncryptionField, v.ExtensionField, v.InitSequence, v.MTU, v.FlowWindow, v.HandshakeType, v.SRTSocketID, v.SynCookie, len(v.PeerIP), len(v.ExtraData))
}

func (v *SRTHandshakePacket) UnmarshalBinary(b []byte) error {
	if len(b) < 4 {
		return errors.Errorf("Invalid packet length %v", len(b))
	}
	v.ControlFlag = b[0] & 0x80
	v.ControlType = binary.BigEndian.Uint16(b[0:2]) & 0x7fff
	v.SubType = binary.BigEndian.Uint16(b[2:4])

	if len(b) < 64 {
		return errors.Errorf("Invalid packet length %v", len(b))
	}
	v.AdditionalInfo = binary.BigEndian.Uint32(b[4:])
	v.Timestamp = binary.BigEndian.Uint32(b[8:])
	v.SocketID = binary.BigEndian.Uint32(b[12:])
	v.Version = binary.BigEndian.Uint32(b[16:])
	v.EncryptionField = binary.BigEndian.Uint16(b[20:])
	v.ExtensionField = binary.BigEndian.Uint16(b[22:])
	v.InitSequence = binary.BigEndian.Uint32(b[24:])
	v.MTU = binary.BigEndian.Uint32(b[28:])
	v.FlowWindow = binary.BigEndian.Uint32(b[32:])
	v.HandshakeType = binary.BigEndian.Uint32(b[36:])
	v.SRTSocketID = binary.BigEndian.Uint32(b[40:])
	v.SynCookie = binary.BigEndian.Uint32(b[44:])

	// Only support IPv4.
	v.PeerIP = net.IPv4(b[51], b[50], b[49], b[48])

	// Clone so ExtraData owns its bytes independently of the caller's buffer.
	// Without this, callers that reuse the receive buffer would silently corrupt
	// any decoded packet they keep alive (e.g. v.handshake0 / v.handshake2).
	v.ExtraData = bytes.Clone(b[64:])

	return nil
}

func (v *SRTHandshakePacket) MarshalBinary() ([]byte, error) {
	b := make([]byte, 64+len(v.ExtraData))
	binary.BigEndian.PutUint16(b, uint16(v.ControlFlag)<<8|v.ControlType)
	binary.BigEndian.PutUint16(b[2:], v.SubType)
	binary.BigEndian.PutUint32(b[4:], v.AdditionalInfo)
	binary.BigEndian.PutUint32(b[8:], v.Timestamp)
	binary.BigEndian.PutUint32(b[12:], v.SocketID)
	binary.BigEndian.PutUint32(b[16:], v.Version)
	binary.BigEndian.PutUint16(b[20:], v.EncryptionField)
	binary.BigEndian.PutUint16(b[22:], v.ExtensionField)
	binary.BigEndian.PutUint32(b[24:], v.InitSequence)
	binary.BigEndian.PutUint32(b[28:], v.MTU)
	binary.BigEndian.PutUint32(b[32:], v.FlowWindow)
	binary.BigEndian.PutUint32(b[36:], v.HandshakeType)
	binary.BigEndian.PutUint32(b[40:], v.SRTSocketID)
	binary.BigEndian.PutUint32(b[44:], v.SynCookie)

	// Only support IPv4.
	ip := v.PeerIP.To4()
	b[48] = ip[3]
	b[49] = ip[2]
	b[50] = ip[1]
	b[51] = ip[0]

	if len(v.ExtraData) > 0 {
		copy(b[64:], v.ExtraData)
	}

	return b, nil
}
