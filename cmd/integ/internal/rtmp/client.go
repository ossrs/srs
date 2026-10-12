// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package rtmp is a small RTMP client for the suite: the simple and complex
// handshakes, chunking, connect, publish and play in the FMLE and Flash
// styles, and FLV tags in and out, including enhanced RTMP. It is written for
// the suite rather than taken from a library, so a case controls every byte it
// sends, such as the handshake, the chunk size and each command.
package rtmp

import (
	"context"
	"crypto/tls"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"srs-integ/internal/media"
)

// DefaultChunkSize is the chunk size a client sends, unless Options says.
const DefaultChunkSize = 4096

const defaultTimeout = 5 * time.Second

// The chunk streams the client writes on.
const (
	csidControl = 2
	csidCommand = 3
	csidData    = 5
	csidVideo   = 6
	csidAudio   = 7
)

// Options changes how a client connects. The zero value, or nil, is a
// complex handshake, a 4096-byte chunk size and the tcUrl, app and stream of
// the URL.
type Options struct {
	Handshake Handshake
	// ChunkSize is the size of the chunks the client sends; it tells the
	// server before connect when it isn't the RTMP default of 128.
	ChunkSize int
	// TcURL, App and Stream replace those of the URL when set.
	TcURL, App, Stream string
	// Connect adds properties to the connect object, or replaces them.
	Connect map[string]any
	// Timeout bounds the dial, the handshake and each command's answer.
	Timeout time.Duration
	// BeforeConnect, when set, writes on the connection after the handshake
	// and the chunk size, before connect; an error stops the dial.
	BeforeConnect func(w io.Writer) error
	// TLS is the TLS config of an rtmps:// URL. Nil verifies the server
	// against the system roots; the server name is the URL's host unless
	// the config names one.
	TLS *tls.Config
}

// PublishMode is the commands a client publishes with.
type PublishMode int

const (
	// FMLE sends releaseStream, FCPublish, createStream and publish, like
	// OBS, FMLE and ffmpeg.
	FMLE PublishMode = iota
	// Flash sends createStream and publish, like a Flash NetStream.
	Flash
)

func (m PublishMode) String() string {
	if m == Flash {
		return "Flash"
	}
	return "FMLE"
}

// Status is an onStatus, onFCPublish or onFCUnpublish, or the information of
// a connect result.
type Status struct {
	// Command is the command that carried it, such as onStatus, onFCPublish,
	// onFCUnpublish, or _result for the connect result.
	Command                  string
	Level, Code, Description string
	// Ex is the status's ex object, such as the one of an RTMP 302 redirect;
	// zero when it has none.
	Ex StatusEx
}

// StatusEx is the ex object of a status: an RTMP 302 redirect has the code
// 302, the tcUrl to connect to as redirect, and the whole RTMP URL, with the
// stream, as redirect2.
type StatusEx struct {
	Code                float64
	Redirect, Redirect2 string
}

// StatusError is a status of level error, or a command answered with _error.
type StatusError struct {
	Status
}

func (e *StatusError) Error() string {
	return fmt.Sprintf("%v: %v", e.Code, e.Description)
}

// A Client is one RTMP connection: dialed and connected, then publishing or
// playing. ReadTag and WriteTag are not safe for concurrent use; Close is.
type Client struct {
	// URL is what the client connected with, after the Options.
	URL URL
	// Handshake is what the server's handshake showed.
	Handshake HandshakeResult
	// Connected is the information of the connect result.
	Connected Status

	conn    net.Conn
	wire    *wireConn
	timeout time.Duration
	r       *chunkReader
	wmu     sync.Mutex
	w       *chunkWriter

	tx        float64
	streamID  uint32
	mode      PublishMode
	ackWindow uint32
	lastAck   uint64
	pending   []*media.FLVTag

	smu      sync.Mutex
	statuses []Status
}

// Dial connects to an RTMP URL: it dials, handshakes, sends the chunk size
// and connect, and waits for the connect result.
func Dial(ctx context.Context, raw string, opts *Options) (*Client, error) {
	if opts == nil {
		opts = &Options{}
	}
	u, err := ParseURL(raw)
	if err != nil {
		return nil, err
	}
	if opts.TcURL != "" {
		u.TcURL = opts.TcURL
	}
	if opts.App != "" {
		u.App = opts.App
	}
	if opts.Stream != "" {
		u.Stream = opts.Stream
	}
	c := &Client{URL: u, timeout: opts.Timeout}
	if c.timeout == 0 {
		c.timeout = defaultTimeout
	}

	dctx, cancel := context.WithTimeout(ctx, c.timeout)
	defer cancel()
	var d net.Dialer
	tcp, err := d.DialContext(dctx, "tcp", u.Addr)
	if err != nil {
		return nil, err
	}
	c.wire = &wireConn{Conn: tcp}
	c.conn = c.wire
	if u.Scheme == "rtmps" {
		if c.conn, err = dialTLS(dctx, c.conn, u.Addr, opts.TLS); err != nil {
			return nil, err
		}
	}
	c.r, c.w = newChunkReader(c.conn), newChunkWriter(c.conn)

	if err := c.connect(ctx, opts); err != nil {
		c.conn.Close()
		return nil, err
	}
	return c, nil
}

func (c *Client) connect(ctx context.Context, opts *Options) error {
	c.setDeadline(ctx)
	defer c.conn.SetDeadline(time.Time{})

	var err error
	if c.Handshake, err = clientHandshake(c.conn, opts.Handshake); err != nil {
		return fmt.Errorf("handshake: %w", err)
	}

	size := opts.ChunkSize
	if size == 0 {
		size = DefaultChunkSize
	}
	if size != 128 {
		if err := c.writeMessage(csidControl, &Message{Type: MsgSetChunkSize, Payload: be32(uint32(size))}); err != nil {
			return fmt.Errorf("set chunk size: %w", err)
		}
		c.w.SetChunkSize(size)
	}
	if opts.BeforeConnect != nil {
		if err := opts.BeforeConnect(c.conn); err != nil {
			return fmt.Errorf("before connect: %w", err)
		}
	}

	obj := map[string]any{
		"app":            c.URL.App,
		"flashVer":       "LNX 9,0,124,2",
		"tcUrl":          c.URL.TcURL,
		"fpad":           false,
		"capabilities":   float64(15),
		"audioCodecs":    float64(4071),
		"videoCodecs":    float64(252),
		"videoFunction":  float64(1),
		"objectEncoding": float64(0),
	}
	for k, v := range opts.Connect {
		obj[k] = v
	}
	values, err := c.call(0, "connect", obj)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	if len(values) > 3 {
		c.Connected = statusOf("_result", values[3])
	}
	return nil
}

// setDeadline sets the connection deadline to the timeout, or to the
// context's deadline when that is sooner.
func (c *Client) setDeadline(ctx context.Context) {
	deadline := time.Now().Add(c.timeout)
	if d, ok := ctx.Deadline(); ok && d.Before(deadline) {
		deadline = d
	}
	c.conn.SetDeadline(deadline)
}

// call sends a command and waits for its _result, which it returns; an
// _error is a StatusError.
func (c *Client) call(streamID uint32, name string, args ...any) ([]any, error) {
	tx, err := c.send(streamID, name, args...)
	if err != nil {
		return nil, err
	}
	for {
		values, err := c.readCommand()
		if err != nil {
			return nil, err
		}
		if len(values) < 2 || values[1] != tx {
			continue
		}
		switch values[0] {
		case "_result":
			return values, nil
		case "_error":
			var st Status
			if len(values) > 3 {
				st = statusOf("_error", values[3])
			}
			return nil, &StatusError{st}
		}
	}
}

// send sends a command with the next transaction id, and returns the id.
func (c *Client) send(streamID uint32, name string, args ...any) (float64, error) {
	c.tx++
	values := []any{name, c.tx}
	// connect has the command object in place of the null.
	if name != "connect" {
		values = append(values, nil)
	}
	b, err := EncodeAMF0(append(values, args...)...)
	if err != nil {
		return 0, err
	}
	return c.tx, c.writeMessage(csidCommand, &Message{Type: MsgCommandAMF0, StreamID: streamID, Payload: b})
}

// waitStatus waits for an onStatus with the code, or one of level error.
func (c *Client) waitStatus(code string) (*Status, error) {
	for {
		values, err := c.readCommand()
		if err != nil {
			return nil, err
		}
		if len(values) < 4 || values[0] != "onStatus" {
			continue
		}
		st := statusOf("onStatus", values[3])
		if st.Level == "error" {
			return nil, &StatusError{st}
		}
		if st.Code == code {
			return &st, nil
		}
	}
}

// readCommand reads messages until a command, and records a status.
func (c *Client) readCommand() ([]any, error) {
	for {
		m, err := c.readMessage()
		if err != nil {
			return nil, err
		}
		if values, ok := c.command(m); ok {
			return values, nil
		}
	}
}

// command decodes a command message, and records it if it is a status.
func (c *Client) command(m *Message) ([]any, bool) {
	payload := m.Payload
	switch m.Type {
	case MsgCommandAMF0:
	case MsgCommandAMF3:
		if len(payload) > 0 {
			payload = payload[1:]
		}
	default:
		return nil, false
	}
	values, err := DecodeAMF0(payload)
	if err != nil || len(values) == 0 {
		return nil, false
	}
	if name, _ := values[0].(string); statusCommands[name] && len(values) > 3 {
		c.smu.Lock()
		c.statuses = append(c.statuses, statusOf(name, values[3]))
		c.smu.Unlock()
	}
	return values, true
}

// statusCommands are the commands Statuses records.
var statusCommands = map[string]bool{"onStatus": true, "onFCPublish": true, "onFCUnpublish": true}

func statusOf(command string, v any) Status {
	m, _ := v.(map[string]any)
	s := func(k string) string { v, _ := m[k].(string); return v }
	st := Status{Command: command, Level: s("level"), Code: s("code"), Description: s("description")}
	if ex, ok := m["ex"].(map[string]any); ok {
		st.Ex.Code, _ = ex["code"].(float64)
		st.Ex.Redirect, _ = ex["redirect"].(string)
		st.Ex.Redirect2, _ = ex["redirect2"].(string)
	}
	return st
}

// createStream creates a stream and keeps its id.
func (c *Client) createStream() error {
	values, err := c.call(0, "createStream")
	if err != nil {
		return fmt.Errorf("createStream: %w", err)
	}
	id, ok := values[len(values)-1].(float64)
	if !ok {
		return fmt.Errorf("createStream result %v has no stream id", values)
	}
	c.streamID = uint32(id)
	return nil
}

// Publish publishes the stream and waits for NetStream.Publish.Start. A
// status of level error is a StatusError.
func (c *Client) Publish(ctx context.Context, mode PublishMode) (*Status, error) {
	c.setDeadline(ctx)
	defer c.conn.SetDeadline(time.Time{})

	c.mode = mode
	if mode == FMLE {
		if _, err := c.send(0, "releaseStream", c.URL.Stream); err != nil {
			return nil, err
		}
		if _, err := c.send(0, "FCPublish", c.URL.Stream); err != nil {
			return nil, err
		}
	}
	if err := c.createStream(); err != nil {
		return nil, err
	}
	if _, err := c.send(c.streamID, "publish", c.URL.Stream, "live"); err != nil {
		return nil, err
	}
	return c.waitStatus("NetStream.Publish.Start")
}

// Play plays the stream and waits for NetStream.Play.Start. A status of
// level error is a StatusError.
func (c *Client) Play(ctx context.Context) (*Status, error) {
	c.setDeadline(ctx)
	defer c.conn.SetDeadline(time.Time{})

	if err := c.createStream(); err != nil {
		return nil, err
	}
	if _, err := c.send(c.streamID, "play", c.URL.Stream); err != nil {
		return nil, err
	}
	// The buffer length, 3s: user control event 3.
	b := []byte{0, 3}
	b = binary.BigEndian.AppendUint32(b, c.streamID)
	b = binary.BigEndian.AppendUint32(b, 3000)
	if err := c.writeMessage(csidControl, &Message{Type: MsgUserControl, Payload: b}); err != nil {
		return nil, err
	}
	return c.waitStatus("NetStream.Play.Start")
}

// WaitStatus waits for an onStatus with the code, such as a play's
// NetStream.Play.UnpublishNotify, until ctx's deadline; it drops the media it
// reads before. A status of level error is a StatusError.
func (c *Client) WaitStatus(ctx context.Context, code string) (*Status, error) {
	c.setDeadline(ctx)
	defer c.conn.SetDeadline(time.Time{})
	return c.waitStatus(code)
}

// Statuses is every onStatus, onFCPublish and onFCUnpublish the client has
// read, in order.
func (c *Client) Statuses() []Status {
	c.smu.Lock()
	defer c.smu.Unlock()
	return append([]Status(nil), c.statuses...)
}

// WriteTag sends an FLV tag on the published stream. A script tag goes out
// as @setDataFrame, like ffmpeg sends onMetaData.
func (c *Client) WriteTag(tag *media.FLVTag) error {
	m := &Message{Type: tag.Type, Timestamp: tag.Timestamp, StreamID: c.streamID, Payload: tag.Data}
	csid := uint32(csidData)
	switch tag.Type {
	case media.TagAudio:
		csid = csidAudio
	case media.TagVideo:
		csid = csidVideo
	case media.TagScript:
		m.Type = MsgDataAMF0
		prefix, _ := EncodeAMF0("@setDataFrame")
		m.Payload = append(prefix, tag.Data...)
	default:
		return fmt.Errorf("unknown FLV tag type %v", tag.Type)
	}
	return c.writeMessage(csid, m)
}

// ReadTag returns the next audio, video or script tag of the played stream.
// It splits aggregate messages, and records the onStatus it reads.
func (c *Client) ReadTag() (*media.FLVTag, error) {
	for {
		if len(c.pending) > 0 {
			tag := c.pending[0]
			c.pending = c.pending[1:]
			return tag, nil
		}
		m, err := c.readMessage()
		if err != nil {
			return nil, err
		}
		switch m.Type {
		case MsgAudio, MsgVideo:
			return &media.FLVTag{Type: m.Type, Timestamp: m.Timestamp, Data: m.Payload}, nil
		case MsgDataAMF0:
			return &media.FLVTag{Type: media.TagScript, Timestamp: m.Timestamp, Data: m.Payload}, nil
		case MsgDataAMF3:
			if len(m.Payload) > 0 {
				return &media.FLVTag{Type: media.TagScript, Timestamp: m.Timestamp, Data: m.Payload[1:]}, nil
			}
		case MsgAggregate:
			if c.pending, err = splitAggregate(m); err != nil {
				return nil, err
			}
		default:
			c.command(m)
		}
	}
}

// splitAggregate returns the tags of an aggregate message, their timestamps
// moved so the first one is the message's.
func splitAggregate(m *Message) ([]*media.FLVTag, error) {
	var tags []*media.FLVTag
	var base uint32
	for b := m.Payload; len(b) > 0; {
		if len(b) < 11 {
			return nil, errors.New("short aggregate sub-tag header")
		}
		n := int(b[1])<<16 | int(b[2])<<8 | int(b[3])
		ts := uint32(b[7])<<24 | uint32(b[4])<<16 | uint32(b[5])<<8 | uint32(b[6])
		if len(b) < 11+n+4 {
			return nil, errors.New("short aggregate sub-tag")
		}
		if len(tags) == 0 {
			base = ts
		}
		tags = append(tags, &media.FLVTag{Type: b[0] & 0x1f, Timestamp: m.Timestamp + ts - base, Data: b[11 : 11+n]})
		b = b[11+n+4:]
	}
	return tags, nil
}

// readMessage reads the next message and handles the protocol control
// messages: the chunk size, the acknowledgement window and pings.
func (c *Client) readMessage() (*Message, error) {
	for {
		m, err := c.r.ReadMessage()
		if err != nil {
			return nil, err
		}
		if err := c.acknowledge(); err != nil {
			return nil, err
		}
		switch m.Type {
		case MsgSetChunkSize:
			if len(m.Payload) < 4 {
				return nil, errors.New("short set chunk size")
			}
			c.r.SetChunkSize(int(binary.BigEndian.Uint32(m.Payload) & 0x7fffffff))
		case MsgWindowAckSize:
			if len(m.Payload) < 4 {
				return nil, errors.New("short window acknowledgement size")
			}
			c.ackWindow = binary.BigEndian.Uint32(m.Payload)
		case MsgUserControl:
			// A ping request, event 6, gets a ping response, event 7.
			if len(m.Payload) >= 6 && m.Payload[0] == 0 && m.Payload[1] == 6 {
				b := append([]byte{0, 7}, m.Payload[2:6]...)
				if err := c.writeMessage(csidControl, &Message{Type: MsgUserControl, Payload: b}); err != nil {
					return nil, err
				}
			}
		case MsgAbort, MsgAck, MsgSetPeerBW:
		default:
			return m, nil
		}
	}
}

// acknowledge sends an acknowledgement once the bytes read since the last
// one pass the server's window.
func (c *Client) acknowledge() error {
	n := c.r.BytesRead()
	if c.ackWindow == 0 || n-c.lastAck < uint64(c.ackWindow) {
		return nil
	}
	c.lastAck = n
	return c.writeMessage(csidControl, &Message{Type: MsgAck, Payload: be32(uint32(n))})
}

func (c *Client) writeMessage(csid uint32, m *Message) error {
	c.wmu.Lock()
	defer c.wmu.Unlock()
	return c.w.WriteMessage(csid, m)
}

// SetDeadline sets the read and write deadline of the connection.
func (c *Client) SetDeadline(t time.Time) error {
	return c.conn.SetDeadline(t)
}

// dialTLS runs the TLS handshake of an rtmps:// connection, with a copy of
// cfg whose server name is the host of addr unless cfg names one. It closes
// conn when the handshake fails.
func dialTLS(ctx context.Context, conn net.Conn, addr string, cfg *tls.Config) (net.Conn, error) {
	if cfg == nil {
		cfg = &tls.Config{}
	}
	cfg = cfg.Clone()
	if cfg.ServerName == "" {
		host, _, err := net.SplitHostPort(addr)
		if err != nil {
			conn.Close()
			return nil, err
		}
		cfg.ServerName = host
	}
	tc := tls.Client(conn, cfg)
	if err := tc.HandshakeContext(ctx); err != nil {
		conn.Close()
		return nil, fmt.Errorf("tls handshake: %w", err)
	}
	return tc, nil
}

// TLS returns the state of an rtmps:// connection, or nil for rtmp://.
func (c *Client) TLS() *tls.ConnectionState {
	tc, ok := c.conn.(*tls.Conn)
	if !ok {
		return nil
	}
	st := tc.ConnectionState()
	return &st
}

// Close closes the connection.
func (c *Client) Close() error {
	return c.conn.Close()
}

func be32(v uint32) []byte {
	return binary.BigEndian.AppendUint32(nil, v)
}

// Unpublish stops publishing in the style the client published with. FMLE
// sends FCUnpublish, waits for NetStream.Unpublish.Success, which it returns,
// then sends deleteStream. Flash sends closeStream, which has no answer, so it
// returns no status.
func (c *Client) Unpublish(ctx context.Context) (*Status, error) {
	c.setDeadline(ctx)
	defer c.conn.SetDeadline(time.Time{})

	if c.mode == Flash {
		_, err := c.send(c.streamID, "closeStream")
		return nil, err
	}
	if _, err := c.send(0, "FCUnpublish", c.URL.Stream); err != nil {
		return nil, err
	}
	st, err := c.waitStatus("NetStream.Unpublish.Success")
	if err != nil {
		return nil, err
	}
	if _, err := c.send(0, "deleteStream", float64(c.streamID)); err != nil {
		return nil, err
	}
	return st, nil
}

// ServerChunkSize is the chunk size the server sends with, the RTMP default
// of 128 until it sets one.
func (c *Client) ServerChunkSize() int {
	return c.r.size
}

// Pause pauses or unpauses the played stream, and waits for
// NetStream.Pause.Notify or NetStream.Unpause.Notify, which it returns. It
// drops the media it reads before the status.
func (c *Client) Pause(ctx context.Context, pause bool) (*Status, error) {
	c.setDeadline(ctx)
	defer c.conn.SetDeadline(time.Time{})

	if _, err := c.send(c.streamID, "pause", pause, float64(0)); err != nil {
		return nil, err
	}
	if pause {
		return c.waitStatus("NetStream.Pause.Notify")
	}
	return c.waitStatus("NetStream.Unpause.Notify")
}

// Call sends a command on the connection, stream 0, and waits for its
// _result, which it returns with the command name and the transaction id
// first; an _error is a StatusError. It drops the media it reads before.
func (c *Client) Call(ctx context.Context, name string, args ...any) ([]any, error) {
	c.setDeadline(ctx)
	defer c.conn.SetDeadline(time.Time{})
	return c.call(0, name, args...)
}

// Send sends a command on the client's stream, the one it created or 0
// before createStream, and doesn't wait for an answer.
func (c *Client) Send(name string, args ...any) error {
	_, err := c.send(c.streamID, name, args...)
	return err
}

// WriteRaw writes bytes on the connection as they are, such as part of a
// message or a broken chunk.
func (c *Client) WriteRaw(b []byte) error {
	c.wmu.Lock()
	defer c.wmu.Unlock()
	_, err := c.conn.Write(b)
	return err
}

// BytesSent is how many bytes the client wrote on the TCP connection: the
// handshake included, and the TLS records of an rtmps:// connection.
func (c *Client) BytesSent() uint64 { return c.wire.tx.Load() }

// BytesReceived is how many bytes the client read from the TCP connection,
// counted like BytesSent. The client reads ahead, so it may count bytes it
// hasn't parsed yet.
func (c *Client) BytesReceived() uint64 { return c.wire.rx.Load() }

// wireConn counts the bytes read and written on the TCP connection.
type wireConn struct {
	net.Conn
	rx, tx atomic.Uint64
}

func (c *wireConn) Read(b []byte) (int, error) {
	n, err := c.Conn.Read(b)
	c.rx.Add(uint64(n))
	return n, err
}

func (c *wireConn) Write(b []byte) (int, error) {
	n, err := c.Conn.Write(b)
	c.tx.Add(uint64(n))
	return n, err
}
