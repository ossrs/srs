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

type srsSRTProxyServer struct {
	environment  env.ProxyEnvironment
	loadBalancer lb.OriginLoadBalancer
	// An interface, so a test listenUDP can return a fake.
	listener net.PacketConn

	sockets sync.Map[uint32, *SRTConnection]
	start   time.Time

	wg stdSync.WaitGroup

	// Tests replace it.
	listenUDP func(ctx context.Context, endpoint string) (net.PacketConn, error)
}

func NewSRSSRTProxyServer(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, opts ...func(*srsSRTProxyServer)) *srsSRTProxyServer {
	v := &srsSRTProxyServer{
		environment:  environment,
		loadBalancer: loadBalancer,
		start:        time.Now(),
		sockets:      sync.NewMap[uint32, *SRTConnection](),
	}

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

	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		// No callee keeps the slice: UnmarshalBinary clones ExtraData, and Write
		// copies it into the kernel.
		buf := make([]byte, 4096)
		for ctx.Err() == nil {
			n, caddr, err := v.listener.ReadFrom(buf)
			if err != nil {
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
		// After the handshake, the client sends to the backend's socket ID.
		// TODO: FIXME: Should cleanup the dead SRT connection.
		v.sockets.Store(newSocketID, conn)
	}

	return nil
}

// Like RTMP, nothing is shared between proxy servers. Unlike WebRTC, the client must keep its
// address and port, since another proxy server cannot find the connection.
type SRTConnection struct {
	ctx          context.Context
	loadBalancer lb.OriginLoadBalancer

	socketID uint32

	// An interface, so a test dialBackendUDP can return a fake.
	backendUDP io.ReadWriteCloser
	// An interface, so tests can inject a fake.
	listenerUDP net.PacketConn

	start time.Time

	handshake0 *SRTHandshakePacket
	handshake1 *SRTHandshakePacket
	handshake2 *SRTHandshakePacket
	handshake3 *SRTHandshakePacket

	// The client resends handshake 2 when our handshake 3 is lost.
	startReader stdSync.Once

	// Tests replace it.
	dialBackendUDP func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error)
}

func NewSRTConnection(opts ...func(*SRTConnection)) *SRTConnection {
	v := &SRTConnection{}

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

func (v *SRTConnection) HandlePacket(pkt *SRTHandshakePacket, addr net.Addr, data []byte) (uint32, error) {
	ctx := v.ctx

	if pkt == nil {
		if v.backendUDP != nil {
			if _, err := v.backendUDP.Write(data); err != nil {
				return v.socketID, errors.Wrapf(err, "write to backend")
			}
		}

		return v.socketID, nil
	}

	if err := v.handleHandshake(ctx, pkt, addr, data); err != nil {
		return v.socketID, errors.Wrapf(err, "handle handshake %v", pkt)
	}

	return v.socketID, nil
}

func (v *SRTConnection) handleHandshake(ctx context.Context, pkt *SRTHandshakePacket, addr net.Addr, data []byte) error {
	// The stream ID comes only with handshake 2, so the proxy answers handshake 0
	// itself and replays it to the backend later.
	if pkt.SynCookie == 0 {
		v.handshake0 = pkt
		logger.Debug(ctx, "SRT Handshake 0: %v", v.handshake0)

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

	streamID, err := pkt.StreamID()
	if err != nil {
		return errors.Wrapf(err, "parse stream id")
	}

	v.handshake2 = pkt
	logger.Debug(ctx, "SRT Handshake 2: %v, sid=%v", v.handshake2, streamID)

	if err := v.connectBackend(ctx, streamID); err != nil {
		return errors.Wrapf(err, "connect backend for %v", streamID)
	}

	if v.backendUDP == nil {
		return errors.Errorf("no backend for %v", streamID)
	}

	if b, err := v.handshake0.MarshalBinary(); err != nil {
		return errors.Wrapf(err, "marshal handshake 0")
	} else if _, err = v.backendUDP.Write(b); err != nil {
		return errors.Wrapf(err, "write handshake 0")
	}
	logger.Debug(ctx, "Proxy send handshake 0: %v", v.handshake0)

	b := make([]byte, 4096)
	handshake1p := &SRTHandshakePacket{}
	if nn, err := v.backendUDP.Read(b); err != nil {
		return errors.Wrapf(err, "read handshake 1")
	} else if err := handshake1p.UnmarshalBinary(b[:nn]); err != nil {
		return errors.Wrapf(err, "unmarshal handshake 1")
	}
	logger.Debug(ctx, "Proxy got handshake 1: %v", handshake1p)

	handshake2p := *v.handshake2
	handshake2p.SynCookie = handshake1p.SynCookie
	if b, err := handshake2p.MarshalBinary(); err != nil {
		return errors.Wrapf(err, "marshal handshake 2")
	} else if _, err = v.backendUDP.Write(b); err != nil {
		return errors.Wrapf(err, "write handshake 2")
	}
	logger.Debug(ctx, "Proxy send handshake 2: %v", handshake2p)

	handshake3p := &SRTHandshakePacket{}
	if nn, err := v.backendUDP.Read(b); err != nil {
		return errors.Wrapf(err, "read handshake 3")
	} else if err := handshake3p.UnmarshalBinary(b[:nn]); err != nil {
		return errors.Wrapf(err, "unmarshal handshake 3")
	}
	logger.Debug(ctx, "Proxy got handshake 3: %v", handshake3p)

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

	backend, err := v.loadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if len(backend.SRT) == 0 {
		return errors.Errorf("no udp server %v for %v", backend, streamURL)
	}

	_, _, udpPort, err := utils.ParseListenEndpoint(backend.SRT[0])
	if err != nil {
		return errors.Wrapf(err, "parse udp port %v of %v for %v", backend.SRT[0], backend, streamURL)
	}

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
	ControlFlag uint8
	ControlType uint16
	SubType     uint16
	// Handshake packets do not use it.
	AdditionalInfo uint32
	Timestamp      uint32
	// The destination socket ID.
	SocketID uint32

	// Currently 4 or 5.
	Version uint32
	// 0     |  No Encryption Advertised
	// 2     |          AES-128
	// 3     |          AES-192
	// 4     |          AES-256
	EncryptionField uint16
	// In a CONCLUSION, a combination of:
	// 0x00000001 | HSREQ
	// 0x00000002 | KMREQ
	// 0x00000004 | CONFIG
	// 0x4A17 if HandshakeType is INDUCTION, see https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-4.3.1.1
	// Otherwise 0.
	ExtensionField uint16
	InitSequence   uint32
	MTU            uint32
	// The most data packets sent and not yet acknowledged.
	FlowWindow uint32
	// 0xFFFFFFFD |      DONE
	// 0xFFFFFFFE |   AGREEMENT
	// 0xFFFFFFFF |   CONCLUSION
	// 0x00000000 |    WAVEHAND
	// 0x00000001 |   INDUCTION
	HandshakeType uint32
	// The source socket ID.
	SRTSocketID uint32
	SynCookie   uint32
	// The sender's IPv4 or IPv6 address, as four 32-bit fields.
	PeerIP net.IP
	// Extensions, each a 16-bit type, a 16-bit length in four-byte blocks,
	// then the contents.
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

		// 0x05 is the stream ID extension.
		if extType != 0x05 {
			p = p[extSize*4:]
			continue
		}

		// Copy, so the swap below does not change ExtraData.
		data := append([]byte{}, p[:extSize*4]...)

		// SRT sends the stream ID in little-endian 32-bit words.
		for i := 0; i < len(data); i += 4 {
			value := binary.LittleEndian.Uint32(data[i:])
			binary.BigEndian.PutUint32(data[i:], value)
		}

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

	// Callers reuse the receive buffer and keep decoded packets, such as
	// handshake0 and handshake2.
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
