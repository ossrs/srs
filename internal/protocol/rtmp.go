// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package protocol

import (
	"context"
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/lb"
	"srsx/internal/logger"
	"srsx/internal/rtmp"
	"srsx/internal/utils"
	"srsx/internal/version"
)

// srsRTMPServer is the proxy for SRS RTMP server, to proxy the RTMP stream to backend SRS
// server. It will figure out the backend server to proxy to. Unlike the edge server, it will
// not cache the stream, but just proxy the stream to backend.
type srsRTMPServer struct {
	// The environment interface.
	environment env.Environment
	// The TCP listener for RTMP server.
	listener *net.TCPListener
	// The wait group for all goroutines.
	wg sync.WaitGroup
}

func NewSRSRTMPServer(environment env.Environment, opts ...func(*srsRTMPServer)) *srsRTMPServer {
	v := &srsRTMPServer{environment: environment}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *srsRTMPServer) Close() error {
	if v.listener != nil {
		v.listener.Close()
	}

	v.wg.Wait()
	return nil
}

func (v *srsRTMPServer) Run(ctx context.Context) error {
	endpoint := v.environment.RtmpServer()
	if !strings.Contains(endpoint, ":") {
		endpoint = ":" + endpoint
	}

	addr, err := net.ResolveTCPAddr("tcp", endpoint)
	if err != nil {
		return errors.Wrapf(err, "resolve rtmp addr %v", endpoint)
	}

	listener, err := net.ListenTCP("tcp", addr)
	if err != nil {
		return errors.Wrapf(err, "listen rtmp addr %v", addr)
	}
	v.listener = listener
	logger.Df(ctx, "RTMP server listen at %v", addr)

	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		for {
			conn, err := v.listener.AcceptTCP()
			if err != nil {
				// If context is canceled or connection is closed, exit gracefully without logging error.
				if ctx.Err() != nil || utils.IsClosedNetworkError(err) {
					logger.Df(ctx, "RTMP server done")
				} else {
					// TODO: If RTMP server closed unexpectedly, we should notice the main loop to quit.
					logger.Wf(ctx, "RTMP server accept err %+v", err)
				}
				return
			}

			v.wg.Add(1)
			go func(ctx context.Context, conn *net.TCPConn) {
				defer v.wg.Done()
				defer conn.Close()

				handleErr := func(err error) {
					if utils.IsPeerClosedError(err) || utils.IsClosedNetworkError(err) {
						logger.Df(ctx, "RTMP connection closed")
					} else {
						logger.Wf(ctx, "RTMP serve err %+v", err)
					}
				}

				rc := NewRTMPConnection()
				if err := rc.serve(ctx, conn); err != nil {
					handleErr(err)
				} else {
					logger.Df(ctx, "RTMP client done")
				}
			}(logger.WithContext(ctx), conn)
		}
	}()

	return nil
}

// RTMPConnection is an RTMP streaming connection. There is no state need to be sync between
// proxy servers.
//
// When we got an RTMP request, we will parse the stream URL from the RTMP publish or play request,
// then proxy to the corresponding backend server. All state is in the RTMP request, so this
// connection is stateless.
type RTMPConnection struct {
}

func NewRTMPConnection(opts ...func(*RTMPConnection)) *RTMPConnection {
	v := &RTMPConnection{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *RTMPConnection) serve(ctx context.Context, conn *net.TCPConn) error {
	logger.Df(ctx, "Got RTMP client from %v", conn.RemoteAddr())

	// If any goroutine quit, cancel another one.
	parentCtx := ctx
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	var backend *RTMPClientToBackend
	if true {
		go func() {
			<-ctx.Done()
			conn.Close()
			if backend != nil {
				backend.Close()
			}
		}()
	}

	// Simple handshake with client.
	hs := rtmp.NewHandshake()
	if _, err := hs.ReadC0S0(conn); err != nil {
		return errors.Wrapf(err, "read c0")
	}
	if _, err := hs.ReadC1S1(conn); err != nil {
		return errors.Wrapf(err, "read c1")
	}
	if err := hs.WriteC0S0(conn); err != nil {
		return errors.Wrapf(err, "write s1")
	}
	if err := hs.WriteC1S1(conn); err != nil {
		return errors.Wrapf(err, "write s1")
	}
	if err := hs.WriteC2S2(conn, hs.C1S1()); err != nil {
		return errors.Wrapf(err, "write s2")
	}
	if _, err := hs.ReadC2S2(conn); err != nil {
		return errors.Wrapf(err, "read c2")
	}

	client := rtmp.NewProtocol(conn)
	logger.Df(ctx, "RTMP simple handshake done")

	// Expect RTMP connect command with tcUrl.
	var connectReq *rtmp.ConnectAppPacket
	if _, err := rtmp.ExpectPacket(ctx, client, &connectReq); err != nil {
		return errors.Wrapf(err, "expect connect req")
	}

	if true {
		ack := rtmp.NewWindowAcknowledgementSize()
		ack.AckSize = 2500000
		if err := client.WritePacket(ctx, ack, 0); err != nil {
			return errors.Wrapf(err, "write set ack size")
		}
	}
	if true {
		chunk := rtmp.NewSetChunkSize()
		chunk.ChunkSize = 128
		if err := client.WritePacket(ctx, chunk, 0); err != nil {
			return errors.Wrapf(err, "write set chunk size")
		}
	}

	connectRes := rtmp.NewConnectAppResPacket(connectReq.TransactionID)
	connectRes.CommandObject.Set("fmsVer", rtmp.NewAmf0String("FMS/3,5,3,888"))
	connectRes.CommandObject.Set("capabilities", rtmp.NewAmf0Number(127))
	connectRes.CommandObject.Set("mode", rtmp.NewAmf0Number(1))
	connectRes.Args.Set("level", rtmp.NewAmf0String("status"))
	connectRes.Args.Set("code", rtmp.NewAmf0String("NetConnection.Connect.Success"))
	connectRes.Args.Set("description", rtmp.NewAmf0String("Connection succeeded"))
	connectRes.Args.Set("objectEncoding", rtmp.NewAmf0Number(0))
	connectResData := rtmp.NewAmf0EcmaArray()
	connectResData.Set("version", rtmp.NewAmf0String("3,5,3,888"))
	connectResData.Set("srs_version", rtmp.NewAmf0String(version.Version()))
	connectResData.Set("srs_id", rtmp.NewAmf0String(logger.ContextID(ctx)))
	connectRes.Args.Set("data", connectResData)
	if err := client.WritePacket(ctx, connectRes, 0); err != nil {
		return errors.Wrapf(err, "write connect res")
	}

	tcUrl := connectReq.TcUrl()
	logger.Df(ctx, "RTMP connect app %v", tcUrl)

	// Expect RTMP command to identify the client, a publisher or viewer.
	var currentStreamID, nextStreamID int
	var streamName string
	var clientType RTMPClientType
	for clientType == "" {
		var identifyReq rtmp.Packet
		if _, err := rtmp.ExpectPacket(ctx, client, &identifyReq); err != nil {
			return errors.Wrapf(err, "expect identify req")
		}

		var response rtmp.Packet
		switch pkt := identifyReq.(type) {
		case *rtmp.CallPacket:
			if pkt.CommandName == "createStream" {
				identifyRes := rtmp.NewCreateStreamResPacket(pkt.TransactionID)
				response = identifyRes

				nextStreamID = 1
				identifyRes.StreamID = *rtmp.NewAmf0Number(float64(nextStreamID))
			} else if pkt.CommandName == "getStreamLength" {
				// Ignore and do not reply these packets.
			} else {
				// For releaseStream, FCPublish, etc.
				identifyRes := rtmp.NewCallPacket()
				response = identifyRes

				identifyRes.TransactionID = pkt.TransactionID
				identifyRes.CommandName = "_result"
				identifyRes.CommandObject = rtmp.NewAmf0Null()
				identifyRes.Args = rtmp.NewAmf0Undefined()
			}
		case *rtmp.PublishPacket:
			streamName = string(pkt.StreamName)
			clientType = RTMPClientTypePublisher

			identifyRes := rtmp.NewCallPacket()
			response = identifyRes

			identifyRes.CommandName = "onFCPublish"
			identifyRes.CommandObject = rtmp.NewAmf0Null()

			data := rtmp.NewAmf0Object()
			data.Set("code", rtmp.NewAmf0String("NetStream.Publish.Start"))
			data.Set("description", rtmp.NewAmf0String("Started publishing stream."))
			identifyRes.Args = data
		case *rtmp.PlayPacket:
			streamName = string(pkt.StreamName)
			clientType = RTMPClientTypeViewer

			identifyRes := rtmp.NewCallPacket()
			response = identifyRes

			identifyRes.CommandName = "onStatus"
			identifyRes.CommandObject = rtmp.NewAmf0Null()

			data := rtmp.NewAmf0Object()
			data.Set("level", rtmp.NewAmf0String("status"))
			data.Set("code", rtmp.NewAmf0String("NetStream.Play.Reset"))
			data.Set("description", rtmp.NewAmf0String("Playing and resetting stream."))
			data.Set("details", rtmp.NewAmf0String("stream"))
			data.Set("clientid", rtmp.NewAmf0String("ASAICiss"))
			identifyRes.Args = data
		}

		if response != nil {
			if err := client.WritePacket(ctx, response, currentStreamID); err != nil {
				return errors.Wrapf(err, "write identify res for req=%v, stream=%v",
					identifyReq, currentStreamID)
			}
		}

		// Update the stream ID for next request.
		currentStreamID = nextStreamID
	}
	logger.Df(ctx, "RTMP identify tcUrl=%v, stream=%v, id=%v, type=%v",
		tcUrl, streamName, currentStreamID, clientType)

	// Find a backend SRS server to proxy the RTMP stream.
	backend = NewRTMPClientToBackend(func(client *RTMPClientToBackend) {
		client.typ = clientType
	})
	defer backend.Close()

	if err := backend.Connect(ctx, tcUrl, streamName); err != nil {
		return errors.Wrapf(err, "connect backend, tcUrl=%v, stream=%v", tcUrl, streamName)
	}

	// Start the streaming.
	if clientType == RTMPClientTypePublisher {
		identifyRes := rtmp.NewCallPacket()

		identifyRes.CommandName = "onStatus"
		identifyRes.CommandObject = rtmp.NewAmf0Null()

		data := rtmp.NewAmf0Object()
		data.Set("level", rtmp.NewAmf0String("status"))
		data.Set("code", rtmp.NewAmf0String("NetStream.Publish.Start"))
		data.Set("description", rtmp.NewAmf0String("Started publishing stream."))
		data.Set("clientid", rtmp.NewAmf0String("ASAICiss"))
		identifyRes.Args = data

		if err := client.WritePacket(ctx, identifyRes, currentStreamID); err != nil {
			return errors.Wrapf(err, "start publish")
		}
	} else if clientType == RTMPClientTypeViewer {
		identifyRes := rtmp.NewCallPacket()

		identifyRes.CommandName = "onStatus"
		identifyRes.CommandObject = rtmp.NewAmf0Null()

		data := rtmp.NewAmf0Object()
		data.Set("level", rtmp.NewAmf0String("status"))
		data.Set("code", rtmp.NewAmf0String("NetStream.Play.Start"))
		data.Set("description", rtmp.NewAmf0String("Started playing stream."))
		data.Set("details", rtmp.NewAmf0String("stream"))
		data.Set("clientid", rtmp.NewAmf0String("ASAICiss"))
		identifyRes.Args = data

		if err := client.WritePacket(ctx, identifyRes, currentStreamID); err != nil {
			return errors.Wrapf(err, "start play")
		}
	}
	logger.Df(ctx, "RTMP start streaming")

	// For all proxy goroutines.
	var wg sync.WaitGroup
	defer wg.Wait()

	// Proxy all message from backend to client.
	wg.Add(1)
	var r0 error
	go func() {
		defer wg.Done()
		defer cancel()

		r0 = func() error {
			for {
				m, err := backend.client.ReadMessage(ctx)
				if err != nil {
					return errors.Wrapf(err, "read message")
				}
				//logger.Df(ctx, "client<- %v %v %vB", m.MessageType, m.Timestamp, len(m.Payload))

				// TODO: Update the stream ID if not the same.
				if err := client.WriteMessage(ctx, m); err != nil {
					return errors.Wrapf(err, "write message")
				}
			}
		}()
	}()

	// Proxy all messages from client to backend.
	wg.Add(1)
	var r1 error
	go func() {
		defer wg.Done()
		defer cancel()

		r1 = func() error {
			for {
				m, err := client.ReadMessage(ctx)
				if err != nil {
					return errors.Wrapf(err, "read message")
				}
				//logger.Df(ctx, "client-> %v %v %vB", m.MessageType, m.Timestamp, len(m.Payload))

				// TODO: Update the stream ID if not the same.
				if err := backend.client.WriteMessage(ctx, m); err != nil {
					return errors.Wrapf(err, "write message")
				}
			}
		}()
	}()

	// Wait until all goroutine quit.
	wg.Wait()

	// Reset the error if caused by another goroutine.
	if r0 != nil {
		// If backend connection closed normally, treat as normal disconnection
		if utils.IsClosedNetworkError(r0) || utils.IsPeerClosedError(r0) {
			logger.Df(ctx, "RTMP backend disconnected")
			return nil
		}
		return errors.Wrapf(r0, "proxy backend->client")
	}
	if r1 != nil {
		// If client connection closed normally, treat as normal disconnection
		if utils.IsClosedNetworkError(r1) || utils.IsPeerClosedError(r1) {
			logger.Df(ctx, "RTMP client disconnected")
			return nil
		}
		return errors.Wrapf(r1, "proxy client->backend")
	}

	return parentCtx.Err()
}

type RTMPClientType string

const (
	RTMPClientTypePublisher RTMPClientType = "publisher"
	RTMPClientTypeViewer    RTMPClientType = "viewer"
)

// RTMPClientToBackend is a RTMP client to proxy the RTMP stream to backend.
type RTMPClientToBackend struct {
	// The underlayer tcp client.
	tcpConn *net.TCPConn
	// The RTMP protocol client.
	client *rtmp.Protocol
	// The stream type.
	typ RTMPClientType
}

func NewRTMPClientToBackend(opts ...func(*RTMPClientToBackend)) *RTMPClientToBackend {
	v := &RTMPClientToBackend{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *RTMPClientToBackend) Close() error {
	if v.tcpConn != nil {
		v.tcpConn.Close()
	}
	return nil
}

func (v *RTMPClientToBackend) Connect(ctx context.Context, tcUrl, streamName string) error {
	// Build the stream URL in vhost/app/stream schema.
	streamURL, err := utils.BuildStreamURL(fmt.Sprintf("%v/%v", tcUrl, streamName))
	if err != nil {
		return errors.Wrapf(err, "build stream url %v/%v", tcUrl, streamName)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := lb.SrsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	// Parse RTMP port from backend.
	if len(backend.RTMP) == 0 {
		return errors.Errorf("no rtmp server %+v for %v", backend, streamURL)
	}

	var rtmpPort int
	if iv, err := strconv.ParseInt(backend.RTMP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse backend %+v rtmp port %v", backend, backend.RTMP[0])
	} else {
		rtmpPort = int(iv)
	}

	// Connect to backend SRS server via TCP client.
	addr := &net.TCPAddr{IP: net.ParseIP(backend.IP), Port: rtmpPort}
	c, err := net.DialTCP("tcp", nil, addr)
	if err != nil {
		return errors.Wrapf(err, "dial backend addr=%v, srs=%v", addr, backend)
	}
	v.tcpConn = c

	hs := rtmp.NewHandshake()
	client := rtmp.NewProtocol(c)
	v.client = client

	// Simple RTMP handshake with server.
	if err := hs.WriteC0S0(c); err != nil {
		return errors.Wrapf(err, "write c0")
	}
	if err := hs.WriteC1S1(c); err != nil {
		return errors.Wrapf(err, "write c1")
	}

	if _, err = hs.ReadC0S0(c); err != nil {
		return errors.Wrapf(err, "read s0")
	}
	if _, err := hs.ReadC1S1(c); err != nil {
		return errors.Wrapf(err, "read s1")
	}
	if _, err = hs.ReadC2S2(c); err != nil {
		return errors.Wrapf(err, "read c2")
	}
	logger.Df(ctx, "backend simple handshake done, server=%v", addr)

	if err := hs.WriteC2S2(c, hs.C1S1()); err != nil {
		return errors.Wrapf(err, "write c2")
	}

	// Connect RTMP app on tcUrl with server.
	if true {
		connectApp := rtmp.NewConnectAppPacket()
		connectApp.CommandObject.Set("tcUrl", rtmp.NewAmf0String(tcUrl))
		if err := client.WritePacket(ctx, connectApp, 1); err != nil {
			return errors.Wrapf(err, "write connect app")
		}
	}

	if true {
		var connectAppRes *rtmp.ConnectAppResPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &connectAppRes); err != nil {
			return errors.Wrapf(err, "expect connect app res")
		}
		logger.Df(ctx, "backend connect RTMP app, tcUrl=%v, id=%v", tcUrl, connectAppRes.SrsID())
	}

	// Play or view RTMP stream with server.
	if v.typ == RTMPClientTypeViewer {
		return v.play(ctx, client, streamName)
	}

	// Publish RTMP stream with server.
	return v.publish(ctx, client, streamName)
}

func (v *RTMPClientToBackend) publish(ctx context.Context, client *rtmp.Protocol, streamName string) error {
	if true {
		identifyReq := rtmp.NewCallPacket()
		identifyReq.CommandName = "releaseStream"
		identifyReq.TransactionID = 2
		identifyReq.CommandObject = rtmp.NewAmf0Null()
		identifyReq.Args = rtmp.NewAmf0String(streamName)
		if err := client.WritePacket(ctx, identifyReq, 0); err != nil {
			return errors.Wrapf(err, "releaseStream")
		}
	}
	for {
		var identifyRes *rtmp.CallPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &identifyRes); err != nil {
			return errors.Wrapf(err, "expect releaseStream res")
		}
		if identifyRes.CommandName == "_result" {
			break
		}
	}

	if true {
		identifyReq := rtmp.NewCallPacket()
		identifyReq.CommandName = "FCPublish"
		identifyReq.TransactionID = 3
		identifyReq.CommandObject = rtmp.NewAmf0Null()
		identifyReq.Args = rtmp.NewAmf0String(streamName)
		if err := client.WritePacket(ctx, identifyReq, 0); err != nil {
			return errors.Wrapf(err, "FCPublish")
		}
	}
	for {
		var identifyRes *rtmp.CallPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &identifyRes); err != nil {
			return errors.Wrapf(err, "expect FCPublish res")
		}
		if identifyRes.CommandName == "_result" {
			break
		}
	}

	var currentStreamID int
	if true {
		createStream := rtmp.NewCreateStreamPacket()
		createStream.TransactionID = 4
		createStream.CommandObject = rtmp.NewAmf0Null()
		if err := client.WritePacket(ctx, createStream, 0); err != nil {
			return errors.Wrapf(err, "createStream")
		}
	}
	for {
		var identifyRes *rtmp.CreateStreamResPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &identifyRes); err != nil {
			return errors.Wrapf(err, "expect createStream res")
		}
		if sid := identifyRes.StreamID; sid != 0 {
			currentStreamID = int(sid)
			break
		}
	}

	if true {
		publishStream := rtmp.NewPublishPacket()
		publishStream.TransactionID = 5
		publishStream.CommandObject = rtmp.NewAmf0Null()
		publishStream.StreamName = *rtmp.NewAmf0String(streamName)
		publishStream.StreamType = *rtmp.NewAmf0String("live")
		if err := client.WritePacket(ctx, publishStream, currentStreamID); err != nil {
			return errors.Wrapf(err, "publish")
		}
	}
	for {
		var identifyRes *rtmp.CallPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &identifyRes); err != nil {
			return errors.Wrapf(err, "expect publish res")
		}
		// Ignore onFCPublish, expect onStatus(NetStream.Publish.Start).
		if identifyRes.CommandName == "onStatus" {
			if data := rtmp.NewAmf0Converter(identifyRes.Args).ToObject(); data == nil {
				return errors.Errorf("onStatus args not object")
			} else if code := rtmp.NewAmf0Converter(data.Get("code")).ToString(); code == nil {
				return errors.Errorf("onStatus code not string")
			} else if *code != "NetStream.Publish.Start" {
				return errors.Errorf("onStatus code=%v not NetStream.Publish.Start", *code)
			}
			break
		}
	}
	logger.Df(ctx, "backend publish stream=%v, sid=%v", streamName, currentStreamID)

	return nil
}

func (v *RTMPClientToBackend) play(ctx context.Context, client *rtmp.Protocol, streamName string) error {
	var currentStreamID int
	if true {
		createStream := rtmp.NewCreateStreamPacket()
		createStream.TransactionID = 4
		createStream.CommandObject = rtmp.NewAmf0Null()
		if err := client.WritePacket(ctx, createStream, 0); err != nil {
			return errors.Wrapf(err, "createStream")
		}
	}
	for {
		var identifyRes *rtmp.CreateStreamResPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &identifyRes); err != nil {
			return errors.Wrapf(err, "expect createStream res")
		}
		if sid := identifyRes.StreamID; sid != 0 {
			currentStreamID = int(sid)
			break
		}
	}

	playStream := rtmp.NewPlayPacket()
	playStream.StreamName = *rtmp.NewAmf0String(streamName)
	if err := client.WritePacket(ctx, playStream, currentStreamID); err != nil {
		return errors.Wrapf(err, "play")
	}

	for {
		var identifyRes *rtmp.CallPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &identifyRes); err != nil {
			return errors.Wrapf(err, "expect releaseStream res")
		}
		if identifyRes.CommandName == "onStatus" && identifyRes.ArgsCode() == "NetStream.Play.Start" {
			break
		}
	}
	return nil
}
