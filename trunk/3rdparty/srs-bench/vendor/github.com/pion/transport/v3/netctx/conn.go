// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package netctx wraps common net interfaces using context.Context.
package netctx

import (
	"context"
	"errors"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

// ErrClosing is returned on Write to closed connection.
var ErrClosing = errors.New("use of closed network connection")

// Reader is an interface for context controlled reader.
type Reader interface {
	ReadContext(context.Context, []byte) (int, error)
}

// Writer is an interface for context controlled writer.
type Writer interface {
	WriteContext(context.Context, []byte) (int, error)
}

// ReadWriter is a composite of ReadWriter.
type ReadWriter interface {
	Reader
	Writer
}

// Conn is a wrapper of net.Conn using context.Context.
type Conn interface {
	Reader
	Writer
	io.Closer
	LocalAddr() net.Addr
	RemoteAddr() net.Addr
	Conn() net.Conn
}

type conn struct {
	nextConn  net.Conn
	closed    chan struct{}
	closeOnce sync.Once
	readMu    sync.Mutex
	writeMu   sync.Mutex
}

var veryOld = time.Unix(0, 1) //nolint:gochecknoglobals

// NewConn creates a new Conn wrapping given net.Conn.
func NewConn(netConn net.Conn) Conn {
	c := &conn{
		nextConn: netConn,
		closed:   make(chan struct{}),
	}
	return c
}

// ReadContext reads data from the connection.
// Unlike net.Conn.Read(), the provided context is used to control timeout.
func (c *conn) ReadContext(ctx context.Context, b []byte) (int, error) {
	c.readMu.Lock()
	defer c.readMu.Unlock()

	select {
	case <-c.closed:
		return 0, net.ErrClosed
	default:
	}

	done := make(chan struct{})
	var wg sync.WaitGroup
	var errSetDeadline atomic.Value
	wg.Add(1)
	go func() {
		defer wg.Done()
		select {
		case <-ctx.Done():
			// context canceled
			if err := c.nextConn.SetReadDeadline(veryOld); err != nil {
				errSetDeadline.Store(err)
				return
			}
			<-done
			if err := c.nextConn.SetReadDeadline(time.Time{}); err != nil {
				errSetDeadline.Store(err)
			}
		case <-done:
		}
	}()

	n, err := c.nextConn.Read(b)

	close(done)
	wg.Wait()
	if e := ctx.Err(); e != nil && n == 0 {
		err = e
	}
	if err2, ok := errSetDeadline.Load().(error); ok && err == nil && err2 != nil {
		err = err2
	}
	return n, err
}

// WriteContext writes data to the connection.
// Unlike net.Conn.Write(), the provided context is used to control timeout.
func (c *conn) WriteContext(ctx context.Context, b []byte) (int, error) {
	c.writeMu.Lock()
	defer c.writeMu.Unlock()

	select {
	case <-c.closed:
		return 0, ErrClosing
	default:
	}

	done := make(chan struct{})
	var wg sync.WaitGroup
	var errSetDeadline atomic.Value
	wg.Add(1)
	go func() {
		defer wg.Done()
		select {
		case <-ctx.Done():
			// context canceled
			if err := c.nextConn.SetWriteDeadline(veryOld); err != nil {
				errSetDeadline.Store(err)
				return
			}
			<-done
			if err := c.nextConn.SetWriteDeadline(time.Time{}); err != nil {
				errSetDeadline.Store(err)
			}
		case <-done:
		}
	}()

	n, err := c.nextConn.Write(b)

	close(done)
	wg.Wait()
	if e := ctx.Err(); e != nil && n == 0 {
		err = e
	}
	if err2, ok := errSetDeadline.Load().(error); ok && err == nil && err2 != nil {
		err = err2
	}
	return n, err
}

// Close closes the connection.
// Any blocked ReadContext or WriteContext operations will be unblocked and
// return errors.
func (c *conn) Close() error {
	err := c.nextConn.Close()
	c.closeOnce.Do(func() {
		c.writeMu.Lock()
		c.readMu.Lock()
		close(c.closed)
		c.readMu.Unlock()
		c.writeMu.Unlock()
	})
	return err
}

// LocalAddr returns the local network address, if known.
func (c *conn) LocalAddr() net.Addr {
	return c.nextConn.LocalAddr()
}

// LocalAddr returns the local network address, if known.
func (c *conn) RemoteAddr() net.Addr {
	return c.nextConn.RemoteAddr()
}

// Conn returns the underlying net.Conn.
func (c *conn) Conn() net.Conn {
	return c.nextConn
}
