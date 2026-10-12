// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// rtmpBadInput adds the RTMP bad-input cases to TestRtmp_Common: broken
// handshakes, broken chunk headers, unknown and out-of-order commands, a
// tcUrl without an app, and clients that close in the middle of a message.
// SRS answers a broken stream by closing the connection, with no error reply,
// so each case checks the close, or what SRS ignores, and that the server
// still serves the next client.
func rtmpBadInput(t *testing.T) {
	// HandshakeBadVersion sends C0 version 6, RTMPE, with a C1 that isn't a
	// complex one: SRS closes the connection without an S0.
	check.Case(t, "HandshakeBadVersion", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConn(c, srs)
		_, err := conn.Write(c0c1(6))
		c.Must(err, "write C0 and C1")
		checkClosed(c, conn, "after C0 version 6")
		checkRTMPServes(c, srs)
	})

	// HandshakeShortC0C1 closes after part of C1: SRS still serves the next
	// client.
	check.Case(t, "HandshakeShortC0C1", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConn(c, srs)
		_, err := conn.Write(c0c1(3)[:700])
		c.Must(err, "write part of C0 and C1")
		conn.Close()
		checkRTMPServes(c, srs)
	})

	// HandshakeShortC2 gets S0, S1 and S2 for a simple C1, then closes after
	// part of C2: SRS still serves the next client.
	check.Case(t, "HandshakeShortC2", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConn(c, srs)
		_, err := conn.Write(c0c1(3))
		c.Must(err, "write C0 and C1")
		s0s1s2 := make([]byte, 1+2*1536)
		conn.SetDeadline(time.Now().Add(time.Second))
		_, err = io.ReadFull(conn, s0s1s2)
		c.Must(err, "read S0, S1 and S2")
		c.CheckEqual(s0s1s2[0], byte(3), "S0 version")
		_, err = conn.Write(s0s1s2[1:700])
		c.Must(err, "write part of C2")
		conn.Close()
		checkRTMPServes(c, srs)
	})

	// ChunkFresh1Accepted starts a chunk stream with a format 1 header, which
	// librtmp does for pings: SRS takes it, and the connect after it succeeds.
	check.Case(t, "ChunkFresh1Accepted", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		b := chunkHeader(1, 10, 0, 4, rtmp.MsgAck, 0)
		b = binary.BigEndian.AppendUint32(b, 0)
		cl, err := dialBefore(c, rtmpURL(srs, "live/"+streamName(c)), nil, b)
		c.Must(err, "connect after a fresh format 1 chunk")
		c.CheckEqual(cl.Connected.Code, "NetConnection.Connect.Success", "connect")
	})

	// ChunkFresh2Closed and ChunkFresh3Closed start a chunk stream with a
	// format 2 or 3 header, which has no message length or type to start
	// from: SRS closes the connection.
	for _, f := range []byte{2, 3} {
		check.Case(t, fmt.Sprintf("ChunkFresh%dClosed", f), check.Normal, func(c *check.C) {
			srs := server.Common(c)
			_, err := dialBefore(c, rtmpURL(srs, "live/"+streamName(c)), nil, chunkHeader(f, 10, 0, 0, 0, 0))
			checkDialClosed(c, err, "a fresh chunk stream of format %d", f)
			checkRTMPServes(c, srs)
		})
	}

	// ChunkFmt0MidMessage sends a format 0 header on a chunk stream in the
	// middle of a message: SRS closes the connection.
	check.Case(t, "ChunkFmt0MidMessage", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		b := append(firstChunk(), chunkHeader(0, 10, 0, 300, rtmp.MsgDataAMF0, 0)...)
		_, err := dialBefore(c, rtmpURL(srs, "live/"+streamName(c)), &rtmp.Options{ChunkSize: 128}, b)
		checkDialClosed(c, err, "format 0 in the middle of a message")
		checkRTMPServes(c, srs)
	})

	// ChunkLengthChanged sends a format 1 header with another message length
	// in the middle of a message: SRS closes the connection.
	check.Case(t, "ChunkLengthChanged", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		b := append(firstChunk(), chunkHeader(1, 10, 0, 200, rtmp.MsgDataAMF0, 0)...)
		_, err := dialBefore(c, rtmpURL(srs, "live/"+streamName(c)), &rtmp.Options{ChunkSize: 128}, b)
		checkDialClosed(c, err, "a new length in the middle of a message")
		checkRTMPServes(c, srs)
	})

	// ChunkSizeTooSmall sets a chunk size of 64, under the RTMP minimum of
	// 128: SRS closes the connection.
	check.Case(t, "ChunkSizeTooSmall", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		b := rtmp.EncodeChunks(2, &rtmp.Message{Type: rtmp.MsgSetChunkSize, Payload: binary.BigEndian.AppendUint32(nil, 64)}, 128)
		_, err := dialBefore(c, rtmpURL(srs, "live/"+streamName(c)), &rtmp.Options{ChunkSize: 128}, b)
		checkDialClosed(c, err, "chunk size 64")
		checkRTMPServes(c, srs)
	})

	// MalformedCommand sends a connect whose AMF0 breaks after the name:
	// SRS closes the connection.
	check.Case(t, "MalformedCommand", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name, _ := rtmp.EncodeAMF0("connect")
		b := rtmp.EncodeChunks(3, &rtmp.Message{Type: rtmp.MsgCommandAMF0, Payload: append(name, 0xff)}, rtmp.DefaultChunkSize)
		_, err := dialBefore(c, rtmpURL(srs, "live/"+streamName(c)), nil, b)
		checkDialClosed(c, err, "a malformed connect")
		checkRTMPServes(c, srs)
	})

	// UnknownCommandAnswered calls a command SRS doesn't know before
	// publishing: SRS answers a _result, and the publish after it works.
	check.Case(t, "UnknownCommandAnswered", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := dialRTMP(c, rtmpURL(srs, "live/"+stream), nil)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		values, err := pub.Call(ctx, "fooBar", "x")
		c.Must(err, "call fooBar")
		c.CheckEqual(values[0], "_result", "answer to fooBar")
		_, err = pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish after fooBar")
		waitHook(c, srs, "on_publish", stream)
	})

	// UnknownCommandWhilePublishing sends a command SRS doesn't know in the
	// middle of an FMLE publish: SRS ignores it, and the player gets every
	// message before and after it.
	check.Case(t, "UnknownCommandWhilePublishing", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		tags := readTags(c, media.H264HighAAC, 0, 400*time.Millisecond)
		player := playerOf(c, url)
		pub := publishTags(c, url, tags[:len(tags)/2])
		c.Must(pub.Send("fooBar", "x"), "send fooBar")
		c.Must(writeTags(pub, tags[len(tags)/2:]), "publish after fooBar")
		checkSameMedia(c, readMedia(c, player, tags), tags)
	})

	// UnknownCommandWhilePlaying sends a command SRS doesn't know on a
	// player in the middle of a stream, and the player gets the rest of the
	// stream after it. It doesn't wait for the answer: SRS reads a playing
	// client's messages on a receive thread that waits 350ms after each one,
	// and the player's buffer length came just before, so the answer takes
	// about 350ms; UnknownCommandAnswered checks the answer.
	check.Case(t, "UnknownCommandWhilePlaying", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		tags := readTags(c, media.H264HighAAC, 0, 400*time.Millisecond)
		first, rest := tags[:len(tags)/2], tags[len(tags)/2:]
		player := playerOf(c, url)
		pub := publishTags(c, url, first)
		checkSameMedia(c, readMedia(c, player, first), first)

		c.Must(player.Send("fooBar", "x"), "send fooBar")
		c.Must(writeTags(pub, rest), "publish the rest")
		checkSameMedia(c, readMedia(c, player, rest), rest)
	})

	// CommandsBeforeConnect sends createStream and publish before connect:
	// SRS drops them, answers the connect, and publishes only the stream
	// published after it.
	check.Case(t, "CommandsBeforeConnect", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		early := stream + "-early"
		var b []byte
		for _, values := range [][]any{{"createStream", 1.0, nil}, {"publish", 2.0, nil, early, "live"}} {
			p, err := rtmp.EncodeAMF0(values...)
			c.Must(err, "encode")
			b = append(b, rtmp.EncodeChunks(3, &rtmp.Message{Type: rtmp.MsgCommandAMF0, Payload: p}, rtmp.DefaultChunkSize)...)
		}
		pub, err := dialBefore(c, rtmpURL(srs, "live/"+stream), nil, b)
		c.Must(err, "connect after createStream and publish")
		c.CheckEqual(pub.Connected.Code, "NetConnection.Connect.Success", "connect")
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err = pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish after connect")
		waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: early})), 0, "on_publish requests of the stream published before connect")
	})

	// PublishWithoutCreateStream sends publish on stream 0 with no
	// createStream before it: SRS ignores it, with no status and no
	// on_publish, and a publish in order on the same connection works.
	check.Case(t, "PublishWithoutCreateStream", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := dialRTMP(c, rtmpURL(srs, "live/"+stream), nil)
		c.Must(pub.Send("publish", stream, "live"), "send publish")
		c.CheckNoError(readNothing(pub, 100*time.Millisecond), "after publish without createStream")
		c.CheckEqual(statusList(pub.Statuses()), []string(nil), "statuses after publish without createStream")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 0, "on_publish requests before the publish in order")

		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish in order")
		waitHook(c, srs, "on_publish", stream)
	})

	// MediaBeforePublish sends a video frame before publish: SRS drops it,
	// and a player gets only the media published after.
	check.Case(t, "MediaBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		tags := readTags(c, media.H264HighAAC, 0, 400*time.Millisecond)
		player := playerOf(c, url)

		pub := dialRTMP(c, url, nil)
		gop := firstKeyframe(c, media.H264HighAAC)
		early := gop[len(gop)-1]
		c.Must(pub.WriteTag(&media.FLVTag{Type: early.Type, Timestamp: 5000, Data: early.Data}), "send a frame before publish")
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish")
		c.Must(writeTags(pub, tags), "publish the media")

		// A frame SRS kept would come first and shift every message after it.
		checkSameMedia(c, readMedia(c, player, tags), tags)
	})

	// PublishMissingApp publishes with a tcUrl that has no app: SRS puts
	// the stream on its default app, __defaultApp__, where a player finds
	// it.
	check.Case(t, "PublishMissingApp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		player := playerOf(c, rtmpURL(srs, defaultApp+"/"+stream))
		tags := readTags(c, media.H264HighAAC, 0, 400*time.Millisecond)
		pub := dialRTMP(c, rtmpURL(srs, "live/"+stream), noApp(srs))
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish without an app")
		c.Must(writeTags(pub, tags), "publish the media")
		c.CheckEqual(waitHook(c, srs, "on_publish", stream).Field("app"), defaultApp, "on_publish app")
		checkSameMedia(c, readMedia(c, player, tags), tags)
	})

	// PlayMissingApp plays with a tcUrl that has no app: SRS plays the
	// stream of its default app, __defaultApp__.
	check.Case(t, "PlayMissingApp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		player := dialRTMP(c, rtmpURL(srs, "live/"+stream), noApp(srs))
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Play(ctx)
		c.Must(err, "play without an app")
		c.CheckEqual(waitHook(c, srs, "on_play", stream).Field("app"), defaultApp, "on_play app")

		tags := readTags(c, media.H264HighAAC, 0, 400*time.Millisecond)
		publishTags(c, rtmpURL(srs, defaultApp+"/"+stream), tags)
		checkSameMedia(c, readMedia(c, player, tags), tags)
	})

	// PublisherCloseMidMessage closes a publisher after part of a video
	// message: SRS unpublishes the stream, removes it from the API, and the
	// stream can be published again. SRS refuses the new publish as
	// StreamDisposing until its HTTP mount is torn down, on a worker of its
	// own that may not have run yet, so the publish is tried again as a
	// client would (see republish).
	check.Case(t, "PublisherCloseMidMessage", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		url := rtmpURL(srs, "live/"+stream)
		pub := publishTags(c, url, readTags(c, media.H264HighAAC, 0, 200*time.Millisecond))
		h := waitHook(c, srs, "on_publish", stream)

		m := &rtmp.Message{Type: rtmp.MsgVideo, Timestamp: 200, StreamID: 1, Payload: make([]byte, 10000)}
		c.Must(pub.WriteRaw(rtmp.EncodeChunks(6, m, rtmp.DefaultChunkSize)[:5000]), "send part of a message")
		pub.Close()
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, "/api/v1/streams/"+h.Field("stream_id")), "the stream after its publisher closed")
		republish(c, url, 300*time.Millisecond)
	})

	// ConnectCloseMidMessage closes after part of the connect command: SRS
	// still serves the next client.
	check.Case(t, "ConnectCloseMidMessage", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		p, err := rtmp.EncodeAMF0("connect", 1.0, map[string]any{"app": "live", "tcUrl": "rtmp://" + srs.Addr(server.RTMP) + "/live", "pad": string(make([]byte, 300))})
		c.Must(err, "encode")
		b := rtmp.EncodeChunks(3, &rtmp.Message{Type: rtmp.MsgCommandAMF0, Payload: p}, 128)[:200]
		stop := errors.New("stop in the middle of connect")
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		cl, err := rtmp.Dial(ctx, rtmpURL(srs, "live/"+streamName(c)), &rtmp.Options{ChunkSize: 128, BeforeConnect: func(w io.Writer) error {
			if _, err := w.Write(b); err != nil {
				return err
			}
			return stop
		}})
		if err == nil {
			cl.Close()
		}
		c.Check(errors.Is(err, stop), "dial error %v, want %v", err, stop)
		checkRTMPServes(c, srs)
	})
}

// defaultApp is the app SRS gives a tcUrl that has none.
const defaultApp = "__defaultApp__"

// noApp is the options of a client whose tcUrl and connect have no app.
func noApp(srs *server.Server) *rtmp.Options {
	return &rtmp.Options{TcURL: "rtmp://" + srs.Addr(server.RTMP), Connect: map[string]any{"app": ""}}
}

// rawConn is a TCP connection to a server's RTMP port, closed when the case
// ends.
func rawConn(c *check.C, srs *server.Server) net.Conn {
	c.Helper()
	conn, err := net.DialTimeout("tcp", srs.Addr(server.RTMP), time.Second)
	c.Must(err, "dial")
	c.Cleanup(func() { conn.Close() })
	return conn
}

// c0c1 is a C0 of a version and a C1 for the simple handshake: a zero time
// and random bytes, which carry no complex-handshake digest.
func c0c1(version byte) []byte {
	b := make([]byte, 1+1536)
	b[0] = version
	rand.Read(b[9:])
	return b
}

// chunkHeader is a chunk's basic header and message header of a format, as
// RTMP lays them out: format 0 has every field, 1 no stream id, 2 only the
// timestamp, 3 nothing.
func chunkHeader(format byte, csid byte, ts, length uint32, typ uint8, streamID uint32) []byte {
	b := []byte{format<<6 | csid}
	if format <= 2 {
		b = append(b, byte(ts>>16), byte(ts>>8), byte(ts))
	}
	if format <= 1 {
		b = append(b, byte(length>>16), byte(length>>8), byte(length), typ)
	}
	if format == 0 {
		b = binary.LittleEndian.AppendUint32(b, streamID)
	}
	return b
}

// firstChunk is the first 128-byte chunk of a 300-byte data message on chunk
// stream 10.
func firstChunk() []byte {
	return append(chunkHeader(0, 10, 0, 300, rtmp.MsgDataAMF0, 0), make([]byte, 128)...)
}

// dialBefore connects to an RTMP URL, and writes b after the handshake and
// the chunk size, before connect. The client is closed when the case ends.
func dialBefore(c *check.C, url string, opts *rtmp.Options, b []byte) (*rtmp.Client, error) {
	c.Helper()
	if opts == nil {
		opts = &rtmp.Options{}
	}
	opts.BeforeConnect = func(w io.Writer) error {
		_, err := w.Write(b)
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	cl, err := rtmp.Dial(ctx, url, opts)
	if err == nil {
		c.Cleanup(func() { cl.Close() })
	}
	return cl, err
}

// checkClosed checks the server closes a connection within 1s without
// sending anything.
func checkClosed(c *check.C, conn net.Conn, what string) {
	c.Helper()
	conn.SetDeadline(time.Now().Add(time.Second))
	n, err := conn.Read(make([]byte, 1))
	c.Check(n == 0 && err != nil && !isTimeout(err), "%s: read %d bytes, %v, want the connection closed", what, n, err)
}

// checkDialClosed checks a client failed because the server closed the
// connection, not because it waited for an answer that never came.
func checkDialClosed(c *check.C, err error, format string, args ...any) {
	c.Helper()
	args = append(args, err)
	c.Check(err != nil && !isTimeout(err), format+": %v, want the connection closed", args...)
}

// checkSameMedia checks a player got the audio and video of tags, byte for
// byte, in order.
func checkSameMedia(c *check.C, got, tags []*media.FLVTag) {
	c.Helper()
	g, w := avTags(got), avTags(tags)
	if !c.CheckEqual(len(g), len(w), "audio and video messages") {
		return
	}
	for i := range w {
		if g[i].Type != w[i].Type || !bytes.Equal(g[i].Data, w[i].Data) {
			c.Errorf("message %d is %s, want %s", i, tagTimes(g[i:i+1]), tagTimes(w[i:i+1]))
			return
		}
	}
}
