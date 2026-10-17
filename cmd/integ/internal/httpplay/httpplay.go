// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package httpplay plays an HTTP stream, such as HTTP-FLV, HTTP-TS or raw
// audio, over a connection of its own, and hands the units a decoder reads
// from it to a case, each with a deadline.
package httpplay

import (
	"bufio"
	"context"
	"crypto/tls"
	"fmt"
	"io"
	"net"
	"net/http"
	neturl "net/url"
	"os"
	"sync"
	"time"
)

// Options are how a Player asks for a stream.
type Options struct {
	// Method is the request method; empty is GET.
	Method string
	// Host replaces the Host header, which is the URL's host otherwise.
	Host string
	// Header holds more request headers, such as X-Forwarded-For or Origin.
	Header http.Header
	// TLS is the client config of an https:// URL; nil verifies against the
	// system roots.
	TLS *tls.Config
}

// A StatusError is an HTTP answer other than 200 OK.
type StatusError struct {
	Code   int
	Status string
}

func (e *StatusError) Error() string {
	return fmt.Sprintf("HTTP status %v", e.Status)
}

// A Decoder reads the units of a 200 OK body and emits each, until the body
// ends or emit returns false, when the player is closed. It returns io.EOF
// when the body ends cleanly.
type Decoder[T any] func(body io.Reader, emit func(T) bool) error

// A Player plays a stream over a connection of its own. It sends the request
// when it dials, and reads the answer and the units as they come, so a case
// can wait for each with a deadline, also before the stream is published. A
// read in the background keeps the stream, so a wait that times out loses
// nothing and the next one goes on.
type Player[T any] struct {
	conn net.Conn

	resDone chan struct{}
	res     *http.Response
	resErr  error

	units chan T
	ended chan struct{} // Closed after end is set, when the read stops.
	end   error
	done  chan struct{} // Closed by Close, so the read stops.
	once  sync.Once

	mu       sync.Mutex
	deadline time.Time
}

// Dial connects to an http:// or https:// URL and sends the request. It
// doesn't wait for the answer. The decoder reads the body of a 200 OK answer.
func Dial[T any](ctx context.Context, url string, opts *Options, decode Decoder[T]) (*Player[T], error) {
	if opts == nil {
		opts = &Options{}
	}
	u, err := neturl.Parse(url)
	if err != nil {
		return nil, err
	}
	port := map[string]string{"http": "80", "https": "443"}[u.Scheme]
	if port == "" {
		return nil, fmt.Errorf("not an http:// or https:// URL: %v", url)
	}
	addr := u.Host
	if u.Port() == "" {
		addr = net.JoinHostPort(u.Hostname(), port)
	}

	method := opts.Method
	if method == "" {
		method = http.MethodGet
	}
	req, err := http.NewRequest(method, url, nil)
	if err != nil {
		return nil, err
	}
	for k, v := range opts.Header {
		req.Header[k] = v
	}
	if opts.Host != "" {
		req.Host = opts.Host
	}

	var d net.Dialer
	conn, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, err
	}
	if u.Scheme == "https" {
		cfg := &tls.Config{}
		if opts.TLS != nil {
			cfg = opts.TLS.Clone()
		}
		if cfg.ServerName == "" {
			cfg.ServerName = u.Hostname()
		}
		tc := tls.Client(conn, cfg)
		if err := tc.HandshakeContext(ctx); err != nil {
			conn.Close()
			return nil, err
		}
		conn = tc
	}
	if dl, ok := ctx.Deadline(); ok {
		conn.SetWriteDeadline(dl)
	}
	if err := req.Write(conn); err != nil {
		conn.Close()
		return nil, err
	}
	conn.SetWriteDeadline(time.Time{})

	p := &Player[T]{
		conn:    conn,
		resDone: make(chan struct{}),
		units:   make(chan T, 4096),
		ended:   make(chan struct{}),
		done:    make(chan struct{}),
	}
	go p.read(req, decode)
	return p, nil
}

// read reads the answer, then decodes the units until the stream ends or
// fails, or the player is closed.
func (p *Player[T]) read(req *http.Request, decode Decoder[T]) {
	defer func() {
		close(p.ended)
		close(p.units)
	}()
	res, err := http.ReadResponse(bufio.NewReader(p.conn), req)
	p.res, p.resErr = res, err
	close(p.resDone)
	if err == nil && res.StatusCode != http.StatusOK {
		err = &StatusError{Code: res.StatusCode, Status: res.Status}
	}
	if err != nil {
		p.end = err
		return
	}

	closed := false
	err = decode(res.Body, func(unit T) bool {
		select {
		case p.units <- unit:
			return true
		case <-p.done:
			closed = true
			return false
		}
	})
	if closed {
		err = net.ErrClosed
	}
	p.end = err
}

// SetDeadline sets when the waits of Response, Wait and Read fail with a
// timeout; the zero time waits forever.
func (p *Player[T]) SetDeadline(t time.Time) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.deadline = t
	return nil
}

// timer is a channel that fires at the deadline, never for the zero one.
func (p *Player[T]) timer() (<-chan time.Time, func()) {
	p.mu.Lock()
	dl := p.deadline
	p.mu.Unlock()
	if dl.IsZero() {
		return nil, func() {}
	}
	t := time.NewTimer(time.Until(dl))
	return t.C, func() { t.Stop() }
}

// Response waits for the status line and headers of the answer.
func (p *Player[T]) Response() (*http.Response, error) {
	select {
	case <-p.resDone:
		return p.res, p.resErr
	default:
	}
	fire, stop := p.timer()
	defer stop()
	select {
	case <-p.resDone:
		return p.res, p.resErr
	case <-fire:
		return nil, os.ErrDeadlineExceeded
	}
}

// Wait waits until done is closed, such as by the decoder once it read a
// header, and returns nil. It returns the stream's end, a *StatusError for
// an answer other than 200 OK, when the stream ends first.
func (p *Player[T]) Wait(done <-chan struct{}) error {
	// What is done wins over an end or a deadline that has passed.
	select {
	case <-done:
		return nil
	default:
	}
	fire, stop := p.timer()
	defer stop()
	select {
	case <-done:
		return nil
	case <-p.ended:
		select {
		case <-done:
			return nil
		default:
			return p.end
		}
	case <-fire:
		return os.ErrDeadlineExceeded
	}
}

// Read waits for the next unit. It returns io.EOF when the stream ends
// cleanly, and a *StatusError for an answer other than 200 OK.
func (p *Player[T]) Read() (T, error) {
	var zero T
	// A unit that is there wins over a deadline that has passed.
	select {
	case unit, ok := <-p.units:
		if !ok {
			return zero, p.end
		}
		return unit, nil
	default:
	}
	fire, stop := p.timer()
	defer stop()
	select {
	case unit, ok := <-p.units:
		if !ok {
			return zero, p.end
		}
		return unit, nil
	case <-fire:
		return zero, os.ErrDeadlineExceeded
	}
}

// TLS returns the state of the TLS connection of an https:// player, and
// nil for http://.
func (p *Player[T]) TLS() *tls.ConnectionState {
	if tc, ok := p.conn.(*tls.Conn); ok {
		st := tc.ConnectionState()
		return &st
	}
	return nil
}

// Close closes the connection.
func (p *Player[T]) Close() error {
	p.once.Do(func() { close(p.done) })
	return p.conn.Close()
}
