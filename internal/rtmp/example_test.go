// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp_test

import (
	"bytes"
	"context"
	"fmt"
	"net"
	"time"

	"srsx/internal/rtmp"
)

func ExampleAmf0Number() {
	number := rtmp.NewAmf0Number(3.14)
	b, err := number.MarshalBinary()
	if err != nil {
		panic(err)
	}

	value, err := rtmp.Amf0Discovery(b)
	if err != nil {
		panic(err)
	}
	if err := value.UnmarshalBinary(b); err != nil {
		panic(err)
	}

	converter := rtmp.NewAmf0Converter(value)
	fmt.Println("number:", converter.ToNumber().Float64())
	fmt.Println("is string:", converter.ToString() != nil)

	// Output:
	// number: 3.14
	// is string: false
}

func ExampleAmf0Object() {
	object := rtmp.NewAmf0Object().
		Set("code", rtmp.NewAmf0Number(100)).
		Set("level", rtmp.NewAmf0String("status"))
	b, err := object.MarshalBinary()
	if err != nil {
		panic(err)
	}

	value, err := rtmp.Amf0Discovery(b)
	if err != nil {
		panic(err)
	}
	if err := value.UnmarshalBinary(b); err != nil {
		panic(err)
	}

	converter := rtmp.NewAmf0Converter(value)
	fmt.Println("code:", rtmp.NewAmf0Converter(converter.ToObject().Get("code")).ToNumber().Float64())
	fmt.Println("level:", rtmp.NewAmf0Converter(converter.ToObject().Get("level")).ToString().String())
	fmt.Println("is number:", converter.ToNumber() != nil)

	// Output:
	// code: 100
	// level: status
	// is number: false
}

func ExampleNewHandshake() {
	client := rtmp.NewHandshake()
	server := rtmp.NewHandshake()

	var clientToServer bytes.Buffer
	if err := client.WriteC0S0(&clientToServer); err != nil {
		panic(err)
	}
	if err := client.WriteC1S1(&clientToServer); err != nil {
		panic(err)
	}

	c0, err := server.ReadC0S0(&clientToServer)
	if err != nil {
		panic(err)
	}
	c1, err := server.ReadC1S1(&clientToServer)
	if err != nil {
		panic(err)
	}

	var serverToClient bytes.Buffer
	if err := server.WriteC0S0(&serverToClient); err != nil {
		panic(err)
	}
	if err := server.WriteC1S1(&serverToClient); err != nil {
		panic(err)
	}
	if err := server.WriteC2S2(&serverToClient, c1); err != nil {
		panic(err)
	}

	s0, err := client.ReadC0S0(&serverToClient)
	if err != nil {
		panic(err)
	}
	s1, err := client.ReadC1S1(&serverToClient)
	if err != nil {
		panic(err)
	}
	s2, err := client.ReadC2S2(&serverToClient)
	if err != nil {
		panic(err)
	}

	if err := client.WriteC2S2(&clientToServer, s1); err != nil {
		panic(err)
	}
	c2, err := server.ReadC2S2(&clientToServer)
	if err != nil {
		panic(err)
	}

	fmt.Println("client version:", c0[0])
	fmt.Println("server version:", s0[0])
	fmt.Println("c1 bytes:", len(c1))
	fmt.Println("s1 bytes:", len(s1))
	fmt.Println("s2 echoes c1:", bytes.Equal(s2, c1))
	fmt.Println("c2 echoes s1:", bytes.Equal(c2, s1))
	fmt.Println("server cached c1:", bytes.Equal(server.C1S1(), c1))

	// Output:
	// client version: 3
	// server version: 3
	// c1 bytes: 1536
	// s1 bytes: 1536
	// s2 echoes c1: true
	// c2 echoes s1: true
	// server cached c1: true
}

func ExampleNewProtocol() {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()

	clientConn, serverConn := net.Pipe()
	defer clientConn.Close()
	defer serverConn.Close()

	client := rtmp.NewProtocol(clientConn)
	server := rtmp.NewProtocol(serverConn)

	backendConn, upstreamConn := net.Pipe()
	defer backendConn.Close()
	defer upstreamConn.Close()

	backend := rtmp.NewProtocol(backendConn)
	upstream := rtmp.NewProtocol(upstreamConn)

	done := make(chan error, 1)
	go func() {
		err := func() error {
			// The server can read a raw message first, then decode it explicitly.
			m, err := server.ExpectMessage(ctx, rtmp.MessageTypeSetChunkSize)
			if err != nil {
				return err
			}
			pkt, err := server.DecodeMessage(m)
			if err != nil {
				return err
			}
			chunkSize := pkt.(*rtmp.SetChunkSize)

			// ExpectPacket reads and decodes messages until it finds the requested packet type.
			var connectReq *rtmp.ConnectAppPacket
			if _, err := rtmp.ExpectPacket(ctx, server, &connectReq); err != nil {
				return err
			}

			ack := rtmp.NewWindowAcknowledgementSize()
			ack.AckSize = 2500000
			if err := server.WritePacket(ctx, ack, 0); err != nil {
				return err
			}

			serverChunk := rtmp.NewSetChunkSize()
			serverChunk.ChunkSize = chunkSize.ChunkSize
			if err := server.WritePacket(ctx, serverChunk, 0); err != nil {
				return err
			}

			connectRes := rtmp.NewConnectAppResPacket(connectReq.TransactionID)
			connectRes.CommandObject.Set("fmsVer", rtmp.NewAmf0String("FMS/3,5,3,888"))
			connectRes.Args.Set("level", rtmp.NewAmf0String("status"))
			connectRes.Args.Set("code", rtmp.NewAmf0String("NetConnection.Connect.Success"))
			if err := server.WritePacket(ctx, connectRes, 0); err != nil {
				return err
			}

			var createStream *rtmp.CallPacket
			if _, err := rtmp.ExpectPacket(ctx, server, &createStream); err != nil {
				return err
			}
			createStreamRes := rtmp.NewCreateStreamResPacket(createStream.TransactionID)
			createStreamRes.SetStreamID(1)
			if err := server.WritePacket(ctx, createStreamRes, 0); err != nil {
				return err
			}

			// For media forwarding, the proxy reads a complete RTMP message and writes
			// that same message to another protocol connection without decoding/repacking.
			publishMessage, err := server.ReadMessage(ctx)
			if err != nil {
				return err
			}
			if err := backend.WriteMessage(ctx, publishMessage); err != nil {
				return err
			}

			return nil
		}()

		select {
		case done <- err:
		case <-ctx.Done():
		}
	}()

	// Client runs the normal RTMP command workflow: configure chunk size,
	// connect to an app, create a stream, publish it, then verify the proxy
	// forwarded the publish message to the upstream side.
	if err := func() error {
		clientChunk := rtmp.NewSetChunkSize()
		clientChunk.ChunkSize = 128
		if err := client.WritePacket(ctx, clientChunk, 0); err != nil {
			return err
		}

		connectReq := rtmp.NewConnectAppPacket()
		connectReq.CommandObject.Set("tcUrl", rtmp.NewAmf0String("rtmp://example.com/live"))
		if err := client.WritePacket(ctx, connectReq, 0); err != nil {
			return err
		}

		ackMessage, err := client.ExpectMessage(ctx, rtmp.MessageTypeWindowAcknowledgementSize)
		if err != nil {
			return err
		}
		ackPacket, err := client.DecodeMessage(ackMessage)
		if err != nil {
			return err
		}
		ack, ok := ackPacket.(*rtmp.WindowAcknowledgementSize)
		if !ok {
			return fmt.Errorf("unexpected ack packet %T", ackPacket)
		}

		var serverChunk *rtmp.SetChunkSize
		if _, err := rtmp.ExpectPacket(ctx, client, &serverChunk); err != nil {
			return err
		}

		var connectRes *rtmp.ConnectAppResPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &connectRes); err != nil {
			return err
		}

		createStream := rtmp.NewCreateStreamPacket()
		if err := client.WritePacket(ctx, createStream, 0); err != nil {
			return err
		}
		var createStreamRes *rtmp.CreateStreamResPacket
		if _, err := rtmp.ExpectPacket(ctx, client, &createStreamRes); err != nil {
			return err
		}

		publish := rtmp.NewPublishPacket()
		publish.TransactionID = 5
		publish.StreamName = rtmp.NewAmf0String("livestream")
		publish.StreamType = rtmp.NewAmf0String("live")
		if err := client.WritePacket(ctx, publish, int(createStreamRes.StreamID)); err != nil {
			return err
		}

		var upstreamPublish *rtmp.PublishPacket
		if _, err := rtmp.ExpectPacket(ctx, upstream, &upstreamPublish); err != nil {
			return err
		}

		select {
		case err := <-done:
			if err != nil {
				return err
			}
		case <-ctx.Done():
			return ctx.Err()
		}

		fmt.Println("ack size:", ack.AckSize)
		fmt.Println("chunk size:", serverChunk.ChunkSize)
		fmt.Println("connect:", rtmp.NewAmf0Converter(connectRes.Args.Get("code")).ToString().String())
		fmt.Println("stream id:", int(createStreamRes.StreamID))
		fmt.Println("forward publish:", upstreamPublish.StreamName.String())

		return nil
	}(); err != nil {
		panic(err)
	}

	// Output:
	// ack size: 2500000
	// chunk size: 128
	// connect: NetConnection.Connect.Success
	// stream id: 1
	// forward publish: livestream
}
