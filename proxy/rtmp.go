// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"io"
	"math/rand"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"srs-proxy/errors"
	"srs-proxy/logger"
	"srs-proxy/rtmp"
)

type rtmpServer struct {
	// The TCP listener for RTMP server.
	listener *net.TCPListener
	// The random number generator.
	rd *rand.Rand
	// The wait group for all goroutines.
	wg sync.WaitGroup
}

func NewRtmpServer(opts ...func(*rtmpServer)) *rtmpServer {
	v := &rtmpServer{
		rd: rand.New(rand.NewSource(time.Now().UnixNano())),
	}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *rtmpServer) Close() error {
	if v.listener != nil {
		v.listener.Close()
	}

	v.wg.Wait()
	return nil
}

func (v *rtmpServer) Run(ctx context.Context) error {
	endpoint := os.Getenv("PROXY_RTMP_SERVER")
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
				if ctx.Err() != context.Canceled {
					// TODO: If RTMP server closed unexpectedly, we should notice the main loop to quit.
					logger.Wf(ctx, "RTMP server accept err %+v", err)
				} else {
					logger.Df(ctx, "RTMP server done")
				}
				return
			}

			go func(ctx context.Context, conn *net.TCPConn) {
				defer conn.Close()
				if err := v.serve(ctx, conn); err != nil {
					if errors.Cause(err) == io.EOF {
						logger.Df(ctx, "RTMP client peer closed")
					} else {
						logger.Wf(ctx, "serve conn %v err %+v", conn.RemoteAddr(), err)
					}
				} else {
					logger.Df(ctx, "RTMP client done")
				}
			}(logger.WithContext(ctx), conn)
		}
	}()

	return nil
}

func (v *rtmpServer) serve(ctx context.Context, conn *net.TCPConn) error {
	logger.Df(ctx, "Got RTMP client from %v", conn.RemoteAddr())

	// Simple handshake with client.
	hs := rtmp.NewHandshake(v.rd)
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
	connectResData.Set("srs_version", rtmp.NewAmf0String(Version()))
	connectResData.Set("srs_id", rtmp.NewAmf0String(logger.ContextID(ctx)))
	connectRes.Args.Set("data", connectResData)
	if err := client.WritePacket(ctx, connectRes, 0); err != nil {
		return errors.Wrapf(err, "write connect res")
	}

	tcUrl := connectReq.TcUrl()
	logger.Df(ctx, "RTMP connect app %v", tcUrl)

	// Expect RTMP command to identify the client, a publisher or viewer.
	var currentStreamID int
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

				currentStreamID = 1
				identifyRes.StreamID = *rtmp.NewAmf0Number(float64(currentStreamID))
			} else {
				// For releaseStream, FCPublish, etc.
				identifyRes := rtmp.NewCallPacket()
				response = identifyRes

				identifyRes.TransactionID = pkt.TransactionID
				identifyRes.CommandName = "_result"
				identifyRes.CommandObject = rtmp.NewAmf0Null()
				identifyRes.Args = rtmp.NewAmf0Null()
			}
		case *rtmp.PublishPacket:
			identifyRes := rtmp.NewCallPacket()
			response = identifyRes

			streamName = string(pkt.StreamName)
			clientType = RTMPClientTypePublisher

			identifyRes.CommandName = "onFCPublish"
			identifyRes.CommandObject = rtmp.NewAmf0Null()

			data := rtmp.NewAmf0Object()
			data.Set("code", rtmp.NewAmf0String("NetStream.Publish.Start"))
			data.Set("description", rtmp.NewAmf0String("Started publishing stream."))
			identifyRes.Args = data
		}

		if response != nil {
			if err := client.WritePacket(ctx, response, currentStreamID); err != nil {
				return errors.Wrapf(err, "write identify res for req=%v, stream=%v",
					identifyReq, currentStreamID)
			}
		}
	}
	logger.Df(ctx, "RTMP identify tcUrl=%v, stream=%v, id=%v, type=%v",
		tcUrl, streamName, currentStreamID, clientType)

	// Find a backend SRS server to proxy the RTMP stream.
	backend := NewRTMPClient(func(client *RTMPClient) {
		client.rd = v.rd
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
	}
	logger.Df(ctx, "RTMP start streaming")

	// Proxy all message from backend to client.
	go func() {
		for {
			m, err := backend.client.ReadMessage(ctx)
			if err != nil {
				return
			}

			if err := client.WriteMessage(ctx, m); err != nil {
				return
			}
		}
	}()

	// Proxy all messages from client to backend.
	for {
		m, err := client.ReadMessage(ctx)
		if err != nil {
			return errors.Wrapf(err, "read message")
		}

		if err := backend.client.WriteMessage(ctx, m); err != nil {
			return errors.Wrapf(err, "write message")
		}
	}

	return nil
}

type RTMPClientType string

const (
	RTMPClientTypePublisher RTMPClientType = "publisher"
)

type RTMPClient struct {
	// The random number generator.
	rd *rand.Rand
	// The underlayer tcp client.
	tcpConn *net.TCPConn
	// The RTMP protocol client.
	client *rtmp.Protocol
}

func NewRTMPClient(opts ...func(*RTMPClient)) *RTMPClient {
	v := &RTMPClient{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *RTMPClient) Close() error {
	if v.tcpConn != nil {
		v.tcpConn.Close()
	}
	return nil
}

func (v *RTMPClient) Connect(ctx context.Context, tcUrl, streamName string) error {
	// Pick a backend SRS server to proxy the RTMP stream.
	streamURL := fmt.Sprintf("%v/%v", tcUrl, streamName)
	backend, err := srsLoadBalancer.Pick(streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	// Parse RTMP port from backend.
	if len(backend.RTMP) == 0 {
		return errors.Errorf("no rtmp server for %v", streamURL)
	}

	var rtmpPort int
	if iv, err := strconv.ParseInt(backend.RTMP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse backend %v rtmp port %v", backend, backend.RTMP[0])
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

	hs := rtmp.NewHandshake(v.rd)
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
		logger.Df(ctx, "backend connect RTMP app, id=%v", connectAppRes.SrsID())
	}

	// Publish RTMP stream with server.
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

	if true {
		createStream := rtmp.NewCreateStreamPacket()
		createStream.TransactionID = 4
		createStream.CommandObject = rtmp.NewAmf0Null()
		if err := client.WritePacket(ctx, createStream, 0); err != nil {
			return errors.Wrapf(err, "createStream")
		}
	}
	var currentStreamID int
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
			if data := rtmp.Amf0AnyToObject(identifyRes.Args); data == nil {
				return errors.Errorf("onStatus args not object")
			} else if code := rtmp.Amf0AnyToString(data.Get("code")); *code != "NetStream.Publish.Start" {
				return errors.Errorf("onStatus code=%v not NetStream.Publish.Start", *code)
			}
			break
		}
	}
	logger.Df(ctx, "backend publish stream=%v, sid=%v", streamName, currentStreamID)

	return nil
}
