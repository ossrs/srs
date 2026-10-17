// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"reflect"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/media"
)

// fakeServer is an RTMP server for the client tests. It answers connect,
// createStream, releaseStream, FCPublish, publish and play, and records every
// message it reads.
type fakeServer struct {
	t  *testing.T
	ln net.Listener

	// silent makes the server read and never answer a command.
	silent bool
	// window is the acknowledgement window it sends after connect, if set.
	window uint32
	// onPlay runs after the server answers play, to send the stream.
	onPlay func(s *fakeConn)
	// tls, when set, serves RTMP inside TLS.
	tls *tls.Config

	// rx and tx count the bytes the server read and wrote on the TCP
	// connection, under TLS when it serves TLS.
	rx, tx atomic.Uint64

	mu       sync.Mutex
	commands []fakeCommand
	media    []*Message
	controls []*Message
	done     chan struct{}
}

type fakeCommand struct {
	name     string
	streamID uint32
	args     []any
}

type fakeConn struct {
	s *fakeServer
	r *chunkReader
	w *chunkWriter
	// fmle is set by FCPublish: publish then answers onFCPublish too.
	fmle bool
}

func newFakeServer(t *testing.T) *fakeServer {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	s := &fakeServer{t: t, ln: ln, done: make(chan struct{})}
	t.Cleanup(func() { ln.Close(); <-s.done })
	return s
}

func (s *fakeServer) start() {
	go func() {
		defer close(s.done)
		conn, err := s.ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		conn = &countedConn{Conn: conn, rx: &s.rx, tx: &s.tx}
		if s.tls != nil {
			conn = tls.Server(conn, s.tls)
		}
		if _, _, err := serveHandshake(conn, fakeLikeSRS); err != nil {
			return
		}
		c := &fakeConn{s: s, r: newChunkReader(conn), w: newChunkWriter(conn)}
		c.serve()
	}()
}

// countedConn counts the bytes read and written on a connection.
type countedConn struct {
	net.Conn
	rx, tx *atomic.Uint64
}

func (c *countedConn) Read(b []byte) (int, error) {
	n, err := c.Conn.Read(b)
	c.rx.Add(uint64(n))
	return n, err
}

func (c *countedConn) Write(b []byte) (int, error) {
	n, err := c.Conn.Write(b)
	c.tx.Add(uint64(n))
	return n, err
}

func (s *fakeServer) url(stream string) string {
	return "rtmp://" + s.ln.Addr().String() + "/live/" + stream
}

func (c *fakeConn) serve() {
	for {
		m, err := c.r.ReadMessage()
		if err != nil {
			return
		}
		switch m.Type {
		case MsgSetChunkSize:
			c.r.SetChunkSize(int(binary.BigEndian.Uint32(m.Payload)))
			c.s.record(nil, nil, m)
		case MsgAudio, MsgVideo, MsgDataAMF0:
			c.s.record(nil, m, nil)
		case MsgCommandAMF0:
			values, err := DecodeAMF0(m.Payload)
			if err != nil || len(values) < 2 {
				c.s.t.Errorf("bad command: %v", err)
				return
			}
			name, _ := values[0].(string)
			c.s.record(&fakeCommand{name, m.StreamID, values[1:]}, nil, nil)
			if !c.s.silent {
				c.answer(name, m.StreamID, values[1:])
			}
		default:
			c.s.record(nil, nil, m)
		}
	}
}

func (c *fakeConn) answer(name string, streamID uint32, args []any) {
	tx := args[0]
	switch name {
	case "connect":
		if c.s.window != 0 {
			c.control(MsgWindowAckSize, be32(c.s.window))
		}
		c.control(MsgSetChunkSize, be32(4096))
		c.w.SetChunkSize(4096)
		c.command(0, "_result", tx, map[string]any{"fmsVer": "FMS/3,5,3,888"},
			map[string]any{"level": "status", "code": "NetConnection.Connect.Success", "description": "Connection succeeded"})
	case "releaseStream", "FCPublish":
		c.fmle = c.fmle || name == "FCPublish"
		c.command(0, "_result", tx, nil, Undefined{})
	case "createStream":
		c.command(0, "_result", tx, nil, float64(1))
	case "publish":
		stream, _ := args[2].(string)
		if stream == "busy" {
			c.status(streamID, "error", "NetStream.Publish.BadName", "Stream is busy")
			return
		}
		if c.fmle {
			c.command(streamID, "onFCPublish", float64(0), nil, map[string]any{"code": "NetStream.Publish.Start", "description": "Started publishing stream."})
		}
		c.status(streamID, "status", "NetStream.Publish.Start", "Started publishing stream.")
	case "FCUnpublish":
		// Like SRS: onFCUnpublish, the _result, then onStatus, on the
		// published stream.
		c.command(1, "onFCUnpublish", float64(0), nil, map[string]any{"code": "NetStream.Unpublish.Success", "description": "Stop publishing stream."})
		c.command(1, "_result", tx, nil, Undefined{})
		c.status(1, "status", "NetStream.Unpublish.Success", "Stream is now unpublished")
	case "play":
		c.control(MsgUserControl, []byte{0, 0, 0, 0, 0, 1})
		c.status(streamID, "status", "NetStream.Play.Reset", "Playing and resetting stream.")
		c.status(streamID, "status", "NetStream.Play.Start", "Started playing stream.")
		if c.s.onPlay != nil {
			c.s.onPlay(c)
		}
	case "pause":
		// Like SRS: a status on the played stream, no _result.
		if pause, _ := args[2].(bool); pause {
			c.status(streamID, "status", "NetStream.Pause.Notify", "Paused stream.")
		} else {
			c.status(streamID, "status", "NetStream.Unpause.Notify", "Unpaused stream.")
		}
	case "rejectMe":
		c.command(0, "_error", tx, nil, map[string]any{"level": "error", "code": "NetConnection.Call.Failed", "description": "rejected"})
	default:
		// Like SRS: any other call with a transaction id gets a _result of
		// nulls.
		if id, _ := tx.(float64); id > 0 {
			c.command(0, "_result", tx, nil, nil)
		}
	}
}

func (c *fakeConn) control(typ uint8, payload []byte) {
	c.w.WriteMessage(2, &Message{Type: typ, Payload: payload})
}

func (c *fakeConn) command(streamID uint32, values ...any) {
	b, err := EncodeAMF0(values...)
	if err != nil {
		c.s.t.Errorf("encode: %v", err)
	}
	c.w.WriteMessage(3, &Message{Type: MsgCommandAMF0, StreamID: streamID, Payload: b})
}

func (c *fakeConn) status(streamID uint32, level, code, desc string) {
	c.command(streamID, "onStatus", float64(0), nil, map[string]any{"level": level, "code": code, "description": desc})
}

func (c *fakeConn) send(typ uint8, ts uint32, payload []byte) {
	c.w.WriteMessage(6, &Message{Type: typ, Timestamp: ts, StreamID: 1, Payload: payload})
}

func (s *fakeServer) record(cmd *fakeCommand, media, control *Message) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if cmd != nil {
		s.commands = append(s.commands, *cmd)
	}
	if media != nil {
		s.media = append(s.media, media)
	}
	if control != nil {
		s.controls = append(s.controls, control)
	}
}

// waitFor waits until fn holds for what the server recorded.
func (s *fakeServer) waitFor(what string, fn func() bool) {
	s.t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		s.mu.Lock()
		ok := fn()
		s.mu.Unlock()
		if ok {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	s.t.Fatalf("timeout waiting for %v", what)
}

func (s *fakeServer) commandNames() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	var names []string
	for _, c := range s.commands {
		names = append(names, c.name)
	}
	return names
}

func (s *fakeServer) command(name string) *fakeCommand {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i := range s.commands {
		if s.commands[i].name == name {
			return &s.commands[i]
		}
	}
	return nil
}

func dial(t *testing.T, url string, opts *Options) *Client {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	c, err := Dial(ctx, url, opts)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	t.Cleanup(func() { c.Close() })
	return c
}

func TestClientConnect(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)

	if !c.Handshake.Complex {
		t.Errorf("handshake not complex, want the complex handshake by default")
	}
	connect := s.command("connect")
	if connect == nil {
		t.Fatalf("no connect, got %v", s.commandNames())
	}
	obj, _ := connect.args[1].(map[string]any)
	if obj["app"] != "live" || obj["tcUrl"] != "rtmp://"+s.ln.Addr().String()+"/live" {
		t.Errorf("connect object %v, want app live and the tcUrl of the URL", obj)
	}
	if _, ok := obj["flashVer"].(string); !ok {
		t.Errorf("connect object %v has no flashVer", obj)
	}

	// The default chunk size goes out before connect.
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(s.controls) == 0 || s.controls[0].Type != MsgSetChunkSize || binary.BigEndian.Uint32(s.controls[0].Payload) != DefaultChunkSize {
		t.Errorf("controls %+v, want SetChunkSize %v first", s.controls, DefaultChunkSize)
	}
}

func TestClientConnectOptions(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	dial(t, s.url("a"), &Options{
		Handshake: Simple,
		TcURL:     "rtmp://example.com/other?vhost=v",
		App:       "other",
		Connect:   map[string]any{"flashVer": "Test/1.0", "extra": "x"},
	})
	connect := s.command("connect")
	if connect == nil {
		t.Fatalf("no connect, got %v", s.commandNames())
	}
	obj, _ := connect.args[1].(map[string]any)
	want := map[string]any{"app": "other", "tcUrl": "rtmp://example.com/other?vhost=v", "flashVer": "Test/1.0", "extra": "x"}
	for k, v := range want {
		if obj[k] != v {
			t.Errorf("connect %v is %v, want %v", k, obj[k], v)
		}
	}
}

// TestClientChunkSize checks a large chunk size is announced, so the server
// reads a 10000-byte message in one chunk.
func TestClientChunkSize(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), &Options{ChunkSize: 60000})
	if _, err := c.Publish(context.Background(), FMLE); err != nil {
		t.Fatalf("publish: %v", err)
	}
	if err := c.WriteTag(&media.FLVTag{Type: media.TagVideo, Timestamp: 1, Data: payload(10000)}); err != nil {
		t.Fatalf("write: %v", err)
	}
	s.waitFor("the video", func() bool { return len(s.media) == 1 })
	s.mu.Lock()
	defer s.mu.Unlock()
	if !bytes.Equal(s.media[0].Payload, payload(10000)) {
		t.Errorf("video of %v bytes, want the 10000 bytes sent", len(s.media[0].Payload))
	}
	if binary.BigEndian.Uint32(s.controls[0].Payload) != 60000 {
		t.Errorf("chunk size % x, want 60000", s.controls[0].Payload)
	}
}

func TestClientPublish(t *testing.T) {
	cases := []struct {
		mode PublishMode
		want []string
	}{
		{FMLE, []string{"connect", "releaseStream", "FCPublish", "createStream", "publish"}},
		{Flash, []string{"connect", "createStream", "publish"}},
	}
	for _, tc := range cases {
		t.Run(tc.mode.String(), func(t *testing.T) {
			s := newFakeServer(t)
			s.start()
			c := dial(t, s.url("livestream?token=x"), nil)
			st, err := c.Publish(context.Background(), tc.mode)
			if err != nil {
				t.Fatalf("publish: %v", err)
			}
			if st.Code != "NetStream.Publish.Start" {
				t.Errorf("status %+v, want NetStream.Publish.Start", st)
			}
			if got := s.commandNames(); !reflect.DeepEqual(got, tc.want) {
				t.Errorf("commands %v, want %v", got, tc.want)
			}
			p := s.command("publish")
			if p.streamID != 1 || p.args[2] != "livestream?token=x" || p.args[3] != "live" {
				t.Errorf("publish on stream %v with %v, want stream 1 with the name and live", p.streamID, p.args)
			}
		})
	}
}

func TestClientPublishRejected(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("busy"), nil)
	_, err := c.Publish(context.Background(), FMLE)
	var se *StatusError
	if !errors.As(err, &se) || se.Code != "NetStream.Publish.BadName" {
		t.Errorf("got %v, want a StatusError with NetStream.Publish.BadName", err)
	}
}

func TestClientWriteTag(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Publish(context.Background(), Flash); err != nil {
		t.Fatalf("publish: %v", err)
	}
	meta, _ := EncodeAMF0("onMetaData", ECMAArray{"duration": float64(30)})
	tags := []*media.FLVTag{
		{Type: media.TagScript, Timestamp: 0, Data: meta},
		{Type: media.TagVideo, Timestamp: 0, Data: []byte{0x17, 0, 0, 0, 0}},
		{Type: media.TagAudio, Timestamp: 23, Data: []byte{0xaf, 0, 0x12, 0x10}},
	}
	for _, tag := range tags {
		if err := c.WriteTag(tag); err != nil {
			t.Fatalf("write: %v", err)
		}
	}
	s.waitFor("three messages", func() bool { return len(s.media) == 3 })

	s.mu.Lock()
	defer s.mu.Unlock()
	wantTypes := []uint8{MsgDataAMF0, MsgVideo, MsgAudio}
	for i, m := range s.media {
		if m.Type != wantTypes[i] || m.StreamID != 1 || m.Timestamp != tags[i].Timestamp {
			t.Errorf("message %v: type %v stream %v ts %v, want type %v stream 1 ts %v", i, m.Type, m.StreamID, m.Timestamp, wantTypes[i], tags[i].Timestamp)
		}
	}
	values, err := DecodeAMF0(s.media[0].Payload)
	if err != nil || len(values) != 3 || values[0] != "@setDataFrame" || values[1] != "onMetaData" {
		t.Errorf("metadata %v, %v; want @setDataFrame, onMetaData and the array", values, err)
	}
	if !bytes.Equal(s.media[1].Payload, tags[1].Data) || !bytes.Equal(s.media[2].Payload, tags[2].Data) {
		t.Errorf("media payloads changed")
	}
}

func TestClientPlay(t *testing.T) {
	s := newFakeServer(t)
	meta, _ := EncodeAMF0("onMetaData", ECMAArray{"width": float64(768)})
	s.onPlay = func(c *fakeConn) {
		c.send(MsgDataAMF0, 0, meta)
		c.send(MsgVideo, 0, append([]byte{0x17, 0, 0, 0, 0}, payload(5000)...))
		c.send(MsgAudio, 10, []byte{0xaf, 1, 2, 3})
		c.status(1, "status", "NetStream.Play.UnpublishNotify", "stream is now unpublished.")
		c.send(MsgAudio, 33, []byte{0xaf, 1, 4, 5})
	}
	s.start()
	c := dial(t, s.url("a"), nil)
	st, err := c.Play(context.Background())
	if err != nil {
		t.Fatalf("play: %v", err)
	}
	if st.Code != "NetStream.Play.Start" {
		t.Errorf("status %+v, want NetStream.Play.Start", st)
	}

	want := []struct {
		typ uint8
		ts  uint32
		n   int
	}{{media.TagScript, 0, len(meta)}, {media.TagVideo, 0, 5005}, {media.TagAudio, 10, 4}, {media.TagAudio, 33, 4}}
	for i, w := range want {
		tag, err := c.ReadTag()
		if err != nil {
			t.Fatalf("tag %v: %v", i, err)
		}
		if tag.Type != w.typ || tag.Timestamp != w.ts || len(tag.Data) != w.n {
			t.Errorf("tag %v: type %v ts %v size %v, want %+v", i, tag.Type, tag.Timestamp, len(tag.Data), w)
		}
	}

	var codes []string
	for _, st := range c.Statuses() {
		codes = append(codes, st.Code)
	}
	if want := []string{"NetStream.Play.Reset", "NetStream.Play.Start", "NetStream.Play.UnpublishNotify"}; !reflect.DeepEqual(codes, want) {
		t.Errorf("statuses %v, want %v", codes, want)
	}
	if got := s.commandNames(); !reflect.DeepEqual(got, []string{"connect", "createStream", "play"}) {
		t.Errorf("commands %v, want connect, createStream, play", got)
	}

	// The buffer length goes out with play: user control event 3 for stream 1.
	s.waitFor("the buffer length", func() bool {
		for _, m := range s.controls {
			if m.Type == MsgUserControl && len(m.Payload) == 10 && m.Payload[1] == 3 {
				return true
			}
		}
		return false
	})
}

// TestClientPlayAggregate reads an aggregate message as its tags, with the
// timestamps moved to the message's timestamp.
func TestClientPlayAggregate(t *testing.T) {
	s := newFakeServer(t)
	s.onPlay = func(c *fakeConn) {
		var b []byte
		for _, sub := range []struct {
			typ  uint8
			ts   uint32
			data []byte
		}{{media.TagAudio, 1000, []byte{0xaf, 1, 1}}, {media.TagVideo, 1040, []byte{0x27, 1, 0, 0, 0, 9}}} {
			n := len(sub.data)
			b = append(b, sub.typ, byte(n>>16), byte(n>>8), byte(n), byte(sub.ts>>16), byte(sub.ts>>8), byte(sub.ts), byte(sub.ts>>24), 0, 0, 0)
			b = append(b, sub.data...)
			b = append(b, be32(uint32(11+n))...)
		}
		c.send(MsgAggregate, 500, b)
	}
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Play(context.Background()); err != nil {
		t.Fatalf("play: %v", err)
	}
	for _, w := range []struct {
		typ uint8
		ts  uint32
	}{{media.TagAudio, 500}, {media.TagVideo, 540}} {
		tag, err := c.ReadTag()
		if err != nil {
			t.Fatalf("read: %v", err)
		}
		if tag.Type != w.typ || tag.Timestamp != w.ts {
			t.Errorf("tag type %v ts %v, want %+v", tag.Type, tag.Timestamp, w)
		}
	}
}

// TestClientAcknowledges checks the player acknowledges the bytes it read once
// they pass the server's window.
func TestClientAcknowledges(t *testing.T) {
	s := newFakeServer(t)
	s.window = 2000
	s.onPlay = func(c *fakeConn) {
		for i := 0; i < 4; i++ {
			c.send(MsgVideo, uint32(i*40), append([]byte{0x27, 1, 0, 0, 0}, payload(1000)...))
		}
	}
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Play(context.Background()); err != nil {
		t.Fatalf("play: %v", err)
	}
	for i := 0; i < 4; i++ {
		if _, err := c.ReadTag(); err != nil {
			t.Fatalf("read: %v", err)
		}
	}
	s.waitFor("an acknowledgement", func() bool {
		for _, m := range s.controls {
			if m.Type == MsgAck && binary.BigEndian.Uint32(m.Payload) >= 2000 {
				return true
			}
		}
		return false
	})
}

func TestClientAnswersPing(t *testing.T) {
	s := newFakeServer(t)
	s.onPlay = func(c *fakeConn) {
		c.control(MsgUserControl, []byte{0, 6, 0, 0, 0x30, 0x39})
		c.send(MsgAudio, 0, []byte{0xaf, 1, 2})
	}
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Play(context.Background()); err != nil {
		t.Fatalf("play: %v", err)
	}
	if _, err := c.ReadTag(); err != nil {
		t.Fatalf("read: %v", err)
	}
	s.waitFor("a ping response", func() bool {
		for _, m := range s.controls {
			if m.Type == MsgUserControl && bytes.Equal(m.Payload, []byte{0, 7, 0, 0, 0x30, 0x39}) {
				return true
			}
		}
		return false
	})
}

func TestClientConnectTimeout(t *testing.T) {
	s := newFakeServer(t)
	s.silent = true
	s.start()
	start := time.Now()
	_, err := Dial(context.Background(), s.url("a"), &Options{Timeout: 200 * time.Millisecond})
	if err == nil || !strings.Contains(err.Error(), "connect") {
		t.Errorf("got %v, want a connect error", err)
	}
	if d := time.Since(start); d > time.Second {
		t.Errorf("took %v, want about the 200ms timeout", d)
	}
}

func TestClientDialRefused(t *testing.T) {
	ln, _ := net.Listen("tcp", "127.0.0.1:0")
	addr := ln.Addr().String()
	ln.Close()
	if _, err := Dial(context.Background(), "rtmp://"+addr+"/live/a", nil); err == nil {
		t.Errorf("dial to a closed port: no error")
	}
}

// TestClientStatusCommands checks Statuses records onFCPublish beside
// onStatus, with the command each came in, in order.
func TestClientStatusCommands(t *testing.T) {
	cases := []struct {
		mode PublishMode
		want []Status
	}{
		{FMLE, []Status{
			{Command: "onFCPublish", Code: "NetStream.Publish.Start", Description: "Started publishing stream."},
			{Command: "onStatus", Level: "status", Code: "NetStream.Publish.Start", Description: "Started publishing stream."},
		}},
		{Flash, []Status{
			{Command: "onStatus", Level: "status", Code: "NetStream.Publish.Start", Description: "Started publishing stream."},
		}},
	}
	for _, tc := range cases {
		t.Run(tc.mode.String(), func(t *testing.T) {
			s := newFakeServer(t)
			s.start()
			c := dial(t, s.url("a"), nil)
			st, err := c.Publish(context.Background(), tc.mode)
			if err != nil {
				t.Fatalf("publish: %v", err)
			}
			if st.Command != "onStatus" {
				t.Errorf("publish returned %+v, want the onStatus", st)
			}
			if got := c.Statuses(); !reflect.DeepEqual(got, tc.want) {
				t.Errorf("statuses %+v, want %+v", got, tc.want)
			}
		})
	}
}

// TestClientConnected checks the connect result is kept as a status from the
// _result.
func TestClientConnected(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	want := Status{Command: "_result", Level: "status", Code: "NetConnection.Connect.Success", Description: "Connection succeeded"}
	if c.Connected != want {
		t.Errorf("connected %+v, want %+v", c.Connected, want)
	}
}

// TestClientUnpublish checks the commands each publish style unpublishes
// with, and the status an FMLE unpublish waits for.
func TestClientUnpublish(t *testing.T) {
	t.Run("FMLE", func(t *testing.T) {
		s := newFakeServer(t)
		s.start()
		c := dial(t, s.url("a?k=v"), nil)
		if _, err := c.Publish(context.Background(), FMLE); err != nil {
			t.Fatalf("publish: %v", err)
		}
		st, err := c.Unpublish(context.Background())
		if err != nil {
			t.Fatalf("unpublish: %v", err)
		}
		if st == nil || st.Command != "onStatus" || st.Code != "NetStream.Unpublish.Success" {
			t.Errorf("unpublish returned %+v, want onStatus NetStream.Unpublish.Success", st)
		}
		var got []string
		for _, st := range c.Statuses()[2:] {
			got = append(got, st.Command+" "+st.Code)
		}
		if want := []string{"onFCUnpublish NetStream.Unpublish.Success", "onStatus NetStream.Unpublish.Success"}; !reflect.DeepEqual(got, want) {
			t.Errorf("statuses after unpublish %v, want %v", got, want)
		}
		s.waitFor("deleteStream", func() bool { return len(s.commands) == 7 })
		if got := s.commandNames()[5:]; !reflect.DeepEqual(got, []string{"FCUnpublish", "deleteStream"}) {
			t.Errorf("commands %v, want FCUnpublish then deleteStream", got)
		}
		if u := s.command("FCUnpublish"); u.args[2] != "a?k=v" {
			t.Errorf("FCUnpublish %v, want the stream name", u.args)
		}
		if d := s.command("deleteStream"); d.args[2] != float64(1) {
			t.Errorf("deleteStream %v, want stream 1", d.args)
		}
	})

	t.Run("Flash", func(t *testing.T) {
		s := newFakeServer(t)
		s.start()
		c := dial(t, s.url("a"), nil)
		if _, err := c.Publish(context.Background(), Flash); err != nil {
			t.Fatalf("publish: %v", err)
		}
		st, err := c.Unpublish(context.Background())
		if err != nil || st != nil {
			t.Errorf("unpublish returned %+v, %v; want no status and no error", st, err)
		}
		s.waitFor("closeStream", func() bool { return len(s.commands) == 4 })
		if c := s.command("closeStream"); c == nil || c.streamID != 1 {
			t.Errorf("commands %v, want closeStream on stream 1", s.commandNames())
		}
	})
}

// TestClientServerChunkSize checks the client reports the chunk size the
// server set.
func TestClientServerChunkSize(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	if got := c.ServerChunkSize(); got != 4096 {
		t.Errorf("server chunk size %v, want the 4096 the server set", got)
	}
}

// TestClientConnectNullProperty checks a nil connect property goes out as
// AMF0 null, so a case can send a connect without a string tcUrl.
func TestClientConnectNullProperty(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	dial(t, s.url("a"), &Options{Connect: map[string]any{"tcUrl": nil}})
	obj, _ := s.command("connect").args[1].(map[string]any)
	if v, ok := obj["tcUrl"]; !ok || v != nil {
		t.Errorf("connect tcUrl %v (present %v), want null", v, ok)
	}
}

// TestClientPlayGap checks Play waits the gap between the play command and
// the buffer length, and still sends the buffer length.
func TestClientPlayGap(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), &Options{PlayGap: 100 * time.Millisecond})
	start := time.Now()
	if _, err := c.Play(context.Background()); err != nil {
		t.Fatalf("play: %v", err)
	}
	if d := time.Since(start); d < 100*time.Millisecond {
		t.Errorf("play took %v, want at least the 100ms gap", d)
	}
	s.waitFor("the buffer length", func() bool {
		for _, m := range s.controls {
			if m.Type == MsgUserControl && len(m.Payload) == 10 && m.Payload[1] == 3 {
				return true
			}
		}
		return false
	})
}

// TestClientPause pauses and unpauses a played stream: the pause command
// carries the flag and the time, and each waits for its status.
func TestClientPause(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Play(context.Background()); err != nil {
		t.Fatalf("play: %v", err)
	}
	for _, tc := range []struct {
		pause bool
		code  string
	}{{true, "NetStream.Pause.Notify"}, {false, "NetStream.Unpause.Notify"}} {
		st, err := c.Pause(context.Background(), tc.pause)
		if err != nil {
			t.Fatalf("pause %v: %v", tc.pause, err)
		}
		if st == nil || st.Code != tc.code {
			t.Errorf("pause %v returned %+v, want %v", tc.pause, st, tc.code)
		}
	}
	s.waitFor("both pauses", func() bool { return len(s.commands) == 5 })
	if got := s.commandNames()[3:]; !reflect.DeepEqual(got, []string{"pause", "pause"}) {
		t.Fatalf("commands %v, want pause twice", got)
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	for i, want := range []bool{true, false} {
		p := s.commands[3+i]
		if p.streamID != 1 || len(p.args) != 4 || p.args[2] != want || p.args[3] != float64(0) {
			t.Errorf("pause %v: stream %v args %v, want stream 1, null, %v, 0", i, p.streamID, p.args, want)
		}
	}
}

// TestClientBeforeConnect writes a message of its own after the handshake and
// the chunk size, and the server reads it before connect.
func TestClientBeforeConnect(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	dial(t, s.url("a"), &Options{BeforeConnect: func(w io.Writer) error {
		_, err := w.Write(EncodeChunks(10, &Message{Type: MsgAck, Payload: be32(7)}, DefaultChunkSize))
		return err
	}})
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(s.controls) < 2 || s.controls[0].Type != MsgSetChunkSize || s.controls[1].Type != MsgAck {
		t.Fatalf("controls %+v, want SetChunkSize then the Ack", s.controls)
	}
	if len(s.commands) == 0 || s.commands[0].name != "connect" {
		t.Errorf("commands %v, want connect after the Ack", s.commands)
	}
}

// TestClientBeforeConnectError stops the dial with the error BeforeConnect
// returns, before connect.
func TestClientBeforeConnectError(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	stop := errors.New("stop here")
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	c, err := Dial(ctx, s.url("a"), &Options{BeforeConnect: func(io.Writer) error { return stop }})
	if err == nil {
		c.Close()
	}
	if !errors.Is(err, stop) {
		t.Fatalf("dial error %v, want %v", err, stop)
	}
	if s.command("connect") != nil {
		t.Errorf("the server got connect, want none")
	}
}

// TestClientCall sends a command the server doesn't know and gets its
// _result; a rejected one is a StatusError.
func TestClientCall(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	values, err := c.Call(ctx, "fooBar", "x")
	if err != nil {
		t.Fatalf("call: %v", err)
	}
	if len(values) < 2 || values[0] != "_result" {
		t.Fatalf("values %v, want a _result", values)
	}
	cmd := s.command("fooBar")
	if cmd == nil || cmd.streamID != 0 || len(cmd.args) < 3 || cmd.args[0] != values[1] || cmd.args[2] != "x" {
		t.Errorf("the server got %+v, want fooBar on stream 0 with the result's transaction id and x", cmd)
	}

	_, err = c.Call(ctx, "rejectMe")
	var se *StatusError
	if !errors.As(err, &se) || se.Code != "NetConnection.Call.Failed" {
		t.Errorf("rejected call error %v, want a StatusError NetConnection.Call.Failed", err)
	}
}

// TestClientSend sends a command on the client's stream without waiting:
// stream 0 before createStream, then the created one.
func TestClientSend(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	if err := c.Send("early", "x"); err != nil {
		t.Fatalf("send: %v", err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := c.Publish(ctx, Flash); err != nil {
		t.Fatalf("publish: %v", err)
	}
	if err := c.Send("late"); err != nil {
		t.Fatalf("send: %v", err)
	}
	s.waitFor("late", func() bool {
		for _, cmd := range s.commands {
			if cmd.name == "late" {
				return true
			}
		}
		return false
	})
	early, late := s.command("early"), s.command("late")
	if early == nil || early.streamID != 0 || len(early.args) < 3 || early.args[2] != "x" {
		t.Errorf("early %+v, want on stream 0 with x", early)
	}
	if late == nil || late.streamID != 1 {
		t.Errorf("late %+v, want on stream 1", late)
	}
}

// TestClientWriteRaw writes a message's chunks by hand, which the server
// reads like any message.
func TestClientWriteRaw(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	m := &Message{Type: MsgVideo, Timestamp: 40, StreamID: 1, Payload: payload(5000)}
	if err := c.WriteRaw(EncodeChunks(20, m, DefaultChunkSize)); err != nil {
		t.Fatalf("write raw: %v", err)
	}
	s.waitFor("the video", func() bool { return len(s.media) > 0 })
	s.mu.Lock()
	defer s.mu.Unlock()
	if !reflect.DeepEqual(s.media[0], m) {
		t.Errorf("the server got %+v, want %+v", s.media[0], m)
	}
}

// TestClientCountsBytes checks the client counts every byte it sends and
// reads on the TCP connection, the handshake included, and the TLS records
// of rtmps://, as the server does.
func TestClientCountsBytes(t *testing.T) {
	for _, scheme := range []string{"rtmp", "rtmps"} {
		t.Run(scheme, func(t *testing.T) {
			var s *fakeServer
			var opts *Options
			url := ""
			if scheme == "rtmp" {
				s = newFakeServer(t)
				s.start()
				url = s.url("a")
			} else {
				var pool *x509.CertPool
				s, pool = newTLSFakeServer(t)
				url = strings.Replace(s.url("a"), "rtmp://", "rtmps://", 1)
				opts = &Options{TLS: &tls.Config{RootCAs: pool}}
			}
			c := dial(t, url, opts)
			if _, err := c.Publish(context.Background(), FMLE); err != nil {
				t.Fatalf("publish: %v", err)
			}
			if err := c.WriteTag(&media.FLVTag{Type: media.TagVideo, Timestamp: 0, Data: make([]byte, 5000)}); err != nil {
				t.Fatalf("write: %v", err)
			}
			s.waitFor("the tag", func() bool { return len(s.media) == 1 })
			if got, want := c.BytesReceived(), s.tx.Load(); got != want {
				t.Errorf("received %d bytes, the server wrote %d", got, want)
			}
			if got, want := c.BytesSent(), s.rx.Load(); got != want {
				t.Errorf("sent %d bytes, the server read %d", got, want)
			}
			if c.BytesSent() < 1+1536+1536+5000 {
				t.Errorf("sent %d bytes, want the handshake and the tag at least", c.BytesSent())
			}
		})
	}
}

// TestClientRedirect reads SRS's RTMP 302 redirect after the play starts: a
// status of level error with its ex object, which WaitStatus returns as a
// StatusError and Statuses keeps.
func TestClientRedirect(t *testing.T) {
	s := newFakeServer(t)
	s.onPlay = func(c *fakeConn) {
		c.send(MsgAudio, 0, []byte{0xaf, 1, 2, 3})
		c.command(1, "onStatus", float64(0), nil, map[string]any{
			"level": "error", "code": "NetConnection.Connect.Rejected", "description": "RTMP 302 Redirect",
			"ex": map[string]any{"code": float64(302), "redirect": "rtmp://127.0.0.1:1935/live", "redirect2": "rtmp://127.0.0.1:1935/live/a?k=v"},
		})
	}
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Play(context.Background()); err != nil {
		t.Fatalf("play: %v", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := c.WaitStatus(ctx, "NetStream.Play.UnpublishNotify")
	var se *StatusError
	if !errors.As(err, &se) {
		t.Fatalf("wait: %v, want a StatusError", err)
	}
	want := Status{
		Command: "onStatus", Level: "error", Code: "NetConnection.Connect.Rejected", Description: "RTMP 302 Redirect",
		Ex: StatusEx{Code: 302, Redirect: "rtmp://127.0.0.1:1935/live", Redirect2: "rtmp://127.0.0.1:1935/live/a?k=v"},
	}
	if se.Status != want {
		t.Errorf("status %+v, want %+v", se.Status, want)
	}
	sts := c.Statuses()
	if got := sts[len(sts)-1]; got != want {
		t.Errorf("last status %+v, want %+v", got, want)
	}
	if sts[0].Ex != (StatusEx{}) {
		t.Errorf("status %+v, want no ex", sts[0])
	}
}

// TestClientWaitStatus waits for a status by its code, and fails at the
// context's deadline when none comes.
func TestClientWaitStatus(t *testing.T) {
	s := newFakeServer(t)
	s.onPlay = func(c *fakeConn) {
		c.status(1, "status", "NetStream.Play.UnpublishNotify", "stream is now unpublished.")
	}
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Play(context.Background()); err != nil {
		t.Fatalf("play: %v", err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if st, err := c.WaitStatus(ctx, "NetStream.Play.UnpublishNotify"); err != nil || st.Code != "NetStream.Play.UnpublishNotify" {
		t.Fatalf("wait: %+v, %v", st, err)
	}

	ctx, cancel = context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	start := time.Now()
	if _, err := c.WaitStatus(ctx, "NetStream.Play.Stop"); err == nil {
		t.Fatal("wait: no error, want the deadline")
	}
	if d := time.Since(start); d > 500*time.Millisecond {
		t.Errorf("waited %v, want the 50ms deadline", d)
	}
}
