// Package connctx wraps net.Conn using context.Context.
package connctx

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

// ConnCtx is a wrapper of net.Conn using context.Context.
type ConnCtx interface {
	Reader
	Writer
	io.Closer
	LocalAddr() net.Addr
	RemoteAddr() net.Addr
	Conn() net.Conn
}

type connCtx struct {
	nextConn  net.Conn
	closed    chan struct{}
	closeOnce sync.Once
	readMu    sync.Mutex
	writeMu   sync.Mutex
}

var veryOld = time.Unix(0, 1) //nolint:gochecknoglobals

// New creates a new ConnCtx wrapping given net.Conn.
func New(conn net.Conn) ConnCtx {
	c := &connCtx{
		nextConn: conn,
		closed:   make(chan struct{}),
	}
	return c
}

func (c *connCtx) ReadContext(ctx context.Context, b []byte) (int, error) {
	c.readMu.Lock()
	defer c.readMu.Unlock()

	select {
	case <-c.closed:
		return 0, io.EOF
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
	if err2 := errSetDeadline.Load(); err == nil && err2 != nil {
		err = err2.(error)
	}
	return n, err
}

func (c *connCtx) WriteContext(ctx context.Context, b []byte) (int, error) {
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
		wg.Done()
	}()

	n, err := c.nextConn.Write(b)

	close(done)
	wg.Wait()
	if e := ctx.Err(); e != nil && n == 0 {
		err = e
	}
	if err2 := errSetDeadline.Load(); err == nil && err2 != nil {
		err = err2.(error)
	}
	return n, err
}

func (c *connCtx) Close() error {
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

func (c *connCtx) LocalAddr() net.Addr {
	return c.nextConn.LocalAddr()
}

func (c *connCtx) RemoteAddr() net.Addr {
	return c.nextConn.RemoteAddr()
}

func (c *connCtx) Conn() net.Conn {
	return c.nextConn
}
