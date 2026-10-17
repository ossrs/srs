// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtsp

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"strconv"
	"syscall"
	"time"

	"github.com/bluenviron/gortsplib/v5/pkg/base"
	"github.com/bluenviron/gortsplib/v5/pkg/conn"
)

// ErrClosed is the error of a read when the server closed the connection.
var ErrClosed = errors.New("rtsp: the server closed the connection")

// A Conn is an RTSP connection that sends requests as they are given, for
// cases that need requests no client sends, and counts the interleaved
// frames the server sends.
type Conn struct {
	nc     net.Conn
	conn   *conn.Conn
	cseq   int
	frames int
}

// Dial connects to an RTSP server at addr, host:port.
func Dial(ctx context.Context, addr string) (*Conn, error) {
	var d net.Dialer
	nc, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, err
	}
	return &Conn{nc: nc, conn: conn.NewConn(bufio.NewReader(nc), nc)}, nil
}

// Do sends a request with the next CSeq and the header, and reads its
// answer for at most timeout.
func (c *Conn) Do(method base.Method, url string, header base.Header, timeout time.Duration) (*base.Response, error) {
	u, err := base.ParseURL(url)
	if err != nil {
		return nil, err
	}
	h := base.Header{}
	for k, v := range header {
		h[k] = v
	}
	c.cseq++
	h["CSeq"] = base.HeaderValue{strconv.Itoa(c.cseq)}
	c.nc.SetWriteDeadline(time.Now().Add(timeout))
	if err := c.conn.WriteRequest(&base.Request{Method: method, URL: u, Header: h}); err != nil {
		return nil, c.readError(err)
	}
	return c.Read(timeout)
}

// Write sends raw bytes.
func (c *Conn) Write(b []byte) error {
	c.nc.SetWriteDeadline(time.Now().Add(time.Second))
	_, err := c.nc.Write(b)
	return c.readError(err)
}

// Read reads the next answer for at most timeout, past the interleaved
// frames before it.
func (c *Conn) Read(timeout time.Duration) (*base.Response, error) {
	c.nc.SetReadDeadline(time.Now().Add(timeout))
	for {
		v, err := c.conn.Read()
		if err != nil {
			return nil, c.readError(err)
		}
		switch v := v.(type) {
		case *base.InterleavedFrame:
			c.frames++
		case *base.Response:
			return v, nil
		default:
			return nil, fmt.Errorf("rtsp: the server sent a %T, want an answer", v)
		}
	}
}

// ReadFrames reads interleaved frames for d, and returns how many came; an
// error only when the connection fails, ErrClosed when the server closes it,
// or the server sends something else.
func (c *Conn) ReadFrames(d time.Duration) (int, error) {
	return c.readFrames(-1, d)
}

// readFrames reads interleaved frames until limit came, for at most d; a
// negative limit reads for all of d.
func (c *Conn) readFrames(limit int, d time.Duration) (int, error) {
	c.nc.SetReadDeadline(time.Now().Add(d))
	n := 0
	for n != limit {
		v, err := c.conn.Read()
		if errors.Is(err, os.ErrDeadlineExceeded) {
			return n, nil
		}
		if err != nil {
			return n, c.readError(err)
		}
		if _, ok := v.(*base.InterleavedFrame); !ok {
			return n, fmt.Errorf("rtsp: the server sent a %T, want interleaved frames", v)
		}
		c.frames++
		n++
	}
	return n, nil
}

// Frames is how many interleaved frames the connection read.
func (c *Conn) Frames() int { return c.frames }

// WaitClosed reads until the server closes the connection, for at most d,
// past any bytes it sends.
func (c *Conn) WaitClosed(d time.Duration) error {
	c.nc.SetReadDeadline(time.Now().Add(d))
	_, err := io.Copy(io.Discard, c.nc)
	if err == nil || errors.Is(err, syscall.ECONNRESET) {
		return nil
	}
	return fmt.Errorf("rtsp: the server keeps the connection: %w", err)
}

// readError is ErrClosed for an end or a reset of the connection.
func (c *Conn) readError(err error) error {
	if errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) || errors.Is(err, syscall.ECONNRESET) || errors.Is(err, syscall.EPIPE) {
		return fmt.Errorf("%w: %v", ErrClosed, err)
	}
	return err
}

// Close closes the connection.
func (c *Conn) Close() error { return c.nc.Close() }

// WaitFrames reads interleaved frames until n came, for at most timeout, and
// returns how many came; an error only as ReadFrames.
func (c *Conn) WaitFrames(n int, timeout time.Duration) (int, error) {
	return c.readFrames(n, timeout)
}
