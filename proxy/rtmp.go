// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"io"
	"math/rand"
	"net"
	"os"
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
	logger.Df(ctx, "RTMP connect app %v", connectReq.TcUrl())

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

				identifyRes.StreamID = 1
				currentStreamID = int(identifyRes.StreamID)
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
	logger.Df(ctx, "RTMP identify stream=%v, id=%v, type=%v",
		streamName, currentStreamID, clientType)

	for {
		m, err := client.ReadMessage(ctx)
		if err != nil {
			return errors.Wrapf(err, "read message")
		}

		_ = m
		logger.Df(ctx, "Got message %v, %v bytes", m.MessageType, len(m.Payload))
	}

	return nil
}

type RTMPClientType string

const (
	RTMPClientTypePublisher RTMPClientType = "publisher"
)
