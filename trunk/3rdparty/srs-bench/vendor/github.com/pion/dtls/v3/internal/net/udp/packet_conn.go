// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package udp implements DTLS specific UDP networking primitives.
// NOTE: this package is an adaption of pion/transport/udp that allows for
// routing datagrams based on identifiers other than the remote address. The
// primary use case for this functionality is routing based on DTLS connection
// IDs. In order to allow for consumers of this package to treat connections as
// generic net.PackageConn, routing and identitier establishment is based on
// custom introspecion of datagrams, rather than direct intervention by
// consumers. If possible, the updates made in this repository will be reflected
// back upstream. If not, it is likely that this will be moved to a public
// package in this repository.
//
// This package was migrated from pion/transport/udp at
// https://github.com/pion/transport/commit/6890c795c807a617c054149eee40a69d7fdfbfdb
package udp

import (
	"context"
	"errors"
	"net"
	"sync"
	"sync/atomic"
	"time"

	idtlsnet "github.com/pion/dtls/v3/internal/net"
	dtlsnet "github.com/pion/dtls/v3/pkg/net"
	"github.com/pion/transport/v3/deadline"
)

const (
	receiveMTU           = 8192
	defaultListenBacklog = 128 // same as Linux default
)

// Typed errors.
var (
	ErrClosedListener      = errors.New("udp: listener closed")
	ErrListenQueueExceeded = errors.New("udp: listen queue exceeded")
)

// listener augments a connection-oriented Listener over a UDP PacketConn.
type listener struct {
	pConn *net.UDPConn

	accepting      atomic.Value // bool
	acceptCh       chan *PacketConn
	doneCh         chan struct{}
	doneOnce       sync.Once
	acceptFilter   func([]byte) bool
	datagramRouter func([]byte) (string, bool)
	connIdentifier func([]byte) (string, bool)

	connLock sync.Mutex
	conns    map[string]*PacketConn
	connWG   sync.WaitGroup

	readWG   sync.WaitGroup
	errClose atomic.Value // error

	readDoneCh chan struct{}
	errRead    atomic.Value // error
}

// Accept waits for and returns the next connection to the listener.
func (l *listener) Accept() (net.PacketConn, net.Addr, error) {
	select {
	case c := <-l.acceptCh:
		l.connWG.Add(1)

		return c, c.raddr, nil

	case <-l.readDoneCh:
		err, _ := l.errRead.Load().(error)

		return nil, nil, err

	case <-l.doneCh:
		return nil, nil, ErrClosedListener
	}
}

// Close closes the listener.
// Any blocked Accept operations will be unblocked and return errors.
func (l *listener) Close() error {
	var err error
	l.doneOnce.Do(func() {
		l.accepting.Store(false)
		close(l.doneCh)

		l.connLock.Lock()
		// Close unaccepted connections
	lclose:
		for {
			select {
			case c := <-l.acceptCh:
				close(c.doneCh)
				// If we have an alternate identifier, remove it from the connection
				// map.
				if id := c.id.Load(); id != nil {
					delete(l.conns, id.(string)) //nolint:forcetypeassert
				}
				// If we haven't already removed the remote address, remove it
				// from the connection map.
				if c.rmraddr.Load() == nil {
					delete(l.conns, c.raddr.String())
					c.rmraddr.Store(true)
				}
			default:
				break lclose
			}
		}
		nConns := len(l.conns)
		l.connLock.Unlock()

		l.connWG.Done()

		if nConns == 0 {
			// Wait if this is the final connection.
			l.readWG.Wait()
			if errClose, ok := l.errClose.Load().(error); ok {
				err = errClose
			}
		} else {
			err = nil
		}
	})

	return err
}

// Addr returns the listener's network address.
func (l *listener) Addr() net.Addr {
	return l.pConn.LocalAddr()
}

// ListenConfig stores options for listening to an address.
type ListenConfig struct {
	// Backlog defines the maximum length of the queue of pending
	// connections. It is equivalent of the backlog argument of
	// POSIX listen function.
	// If a connection request arrives when the queue is full,
	// the request will be silently discarded, unlike TCP.
	// Set zero to use default value 128 which is same as Linux default.
	Backlog int

	// AcceptFilter determines whether the new conn should be made for
	// the incoming packet. If not set, any packet creates new conn.
	AcceptFilter func([]byte) bool

	// DatagramRouter routes an incoming datagram to a connection by extracting
	// an identifier from the its paylod
	DatagramRouter func([]byte) (string, bool)

	// ConnectionIdentifier extracts an identifier from an outgoing packet. If
	// the identifier is not already associated with the connection, it will be
	// added.
	ConnectionIdentifier func([]byte) (string, bool)
}

// Listen creates a new listener based on the ListenConfig.
func (lc *ListenConfig) Listen(network string, laddr *net.UDPAddr) (dtlsnet.PacketListener, error) {
	if lc.Backlog == 0 {
		lc.Backlog = defaultListenBacklog
	}

	conn, err := net.ListenUDP(network, laddr)
	if err != nil {
		return nil, err
	}

	packetListener := &listener{
		pConn:          conn,
		acceptCh:       make(chan *PacketConn, lc.Backlog),
		conns:          make(map[string]*PacketConn),
		doneCh:         make(chan struct{}),
		acceptFilter:   lc.AcceptFilter,
		datagramRouter: lc.DatagramRouter,
		connIdentifier: lc.ConnectionIdentifier,
		readDoneCh:     make(chan struct{}),
	}

	packetListener.accepting.Store(true)
	packetListener.connWG.Add(1)
	packetListener.readWG.Add(2) // wait readLoop and Close execution routine

	go packetListener.readLoop()
	go func() {
		packetListener.connWG.Wait()
		if err := packetListener.pConn.Close(); err != nil {
			packetListener.errClose.Store(err)
		}
		packetListener.readWG.Done()
	}()

	return packetListener, nil
}

// Listen creates a new listener using default ListenConfig.
func Listen(network string, laddr *net.UDPAddr) (dtlsnet.PacketListener, error) {
	return (&ListenConfig{}).Listen(network, laddr)
}

// readLoop dispatches packets to the proper connection, creating a new one if
// necessary, until all connections are closed.
func (l *listener) readLoop() {
	defer l.readWG.Done()
	defer close(l.readDoneCh)

	buf := make([]byte, receiveMTU)

	for {
		n, raddr, err := l.pConn.ReadFrom(buf)
		if err != nil {
			l.errRead.Store(err)

			return
		}
		conn, ok, err := l.getConn(raddr, buf[:n])
		if err != nil {
			continue
		}
		if ok {
			_, _ = conn.buffer.WriteTo(buf[:n], raddr)
		}
	}
}

// getConn gets an existing connection or creates a new one.
func (l *listener) getConn(raddr net.Addr, buf []byte) (*PacketConn, bool, error) { //nolint:cyclop
	l.connLock.Lock()
	defer l.connLock.Unlock()
	// If we have a custom resolver, use it.
	if l.datagramRouter != nil {
		if id, ok := l.datagramRouter(buf); ok {
			if conn, ok := l.conns[id]; ok {
				return conn, true, nil
			}
		}
	}

	// If we don't have a custom resolver, or we were unable to find an
	// associated connection, fall back to remote address.
	conn, ok := l.conns[raddr.String()]
	if !ok {
		if isAccepting, ok := l.accepting.Load().(bool); !isAccepting || !ok {
			return nil, false, ErrClosedListener
		}
		if l.acceptFilter != nil {
			if !l.acceptFilter(buf) {
				return nil, false, nil
			}
		}
		conn = l.newPacketConn(raddr)
		select {
		case l.acceptCh <- conn:
			l.conns[raddr.String()] = conn
		default:
			return nil, false, ErrListenQueueExceeded
		}
	}

	return conn, true, nil
}

// PacketConn is a net.PacketConn implementation that is able to dictate its
// routing ID via an alternate identifier from its remote address. Internal
// buffering is performed for reads, and writes are passed through to the
// underlying net.PacketConn.
type PacketConn struct {
	listener *listener

	raddr   net.Addr
	rmraddr atomic.Value // bool
	id      atomic.Value // string

	buffer *idtlsnet.PacketBuffer

	doneCh   chan struct{}
	doneOnce sync.Once

	writeDeadline *deadline.Deadline
}

// newPacketConn constructs a new PacketConn.
func (l *listener) newPacketConn(raddr net.Addr) *PacketConn {
	return &PacketConn{
		listener:      l,
		raddr:         raddr,
		buffer:        idtlsnet.NewPacketBuffer(),
		doneCh:        make(chan struct{}),
		writeDeadline: deadline.New(),
	}
}

// ReadFrom reads a single packet payload and its associated remote address from
// the underlying buffer.
func (c *PacketConn) ReadFrom(buff []byte) (int, net.Addr, error) {
	return c.buffer.ReadFrom(buff)
}

// WriteTo writes len(payload) bytes from payload to the specified address.
func (c *PacketConn) WriteTo(payload []byte, addr net.Addr) (n int, err error) {
	// If we have a connection identifier, check to see if the outgoing packet
	// sets it.
	if c.listener.connIdentifier != nil {
		id := c.id.Load()
		// Only update establish identifier if we haven't already done so.
		if id == nil {
			candidate, ok := c.listener.connIdentifier(payload)
			// If we have an identifier, add entry to connection map.
			if ok {
				c.listener.connLock.Lock()
				c.listener.conns[candidate] = c
				c.listener.connLock.Unlock()
				c.id.Store(candidate)
			}
		}
		// If we are writing to a remote address that differs from the initial,
		// we have an alternate identifier established, and we haven't already
		// freed the remote address, free the remote address to be used by
		// another connection.
		// Note: this strategy results in holding onto a remote address after it
		// is potentially no longer in use by the client. However, releasing
		// earlier means that we could miss some packets that should have been
		// routed to this connection. Ideally, we would drop the connection
		// entry for the remote address as soon as the client starts sending
		// using an alternate identifier, but in practice this proves
		// challenging because any client could spoof a connection identifier,
		// resulting in the remote address entry being dropped prior to the
		// "real" client transitioning to sending using the alternate
		// identifier.
		if id != nil && c.rmraddr.Load() == nil && addr.String() != c.raddr.String() {
			c.listener.connLock.Lock()
			delete(c.listener.conns, c.raddr.String())
			c.rmraddr.Store(true)
			c.listener.connLock.Unlock()
		}
	}

	select {
	case <-c.writeDeadline.Done():
		return 0, context.DeadlineExceeded
	default:
	}

	return c.listener.pConn.WriteTo(payload, addr)
}

// Close closes the conn and releases any Read calls.
func (c *PacketConn) Close() error {
	var err error
	c.doneOnce.Do(func() {
		c.listener.connWG.Done()
		close(c.doneCh)
		c.listener.connLock.Lock()
		// If we have an alternate identifier, remove it from the connection
		// map.
		if id := c.id.Load(); id != nil {
			delete(c.listener.conns, id.(string)) //nolint:forcetypeassert
		}
		// If we haven't already removed the remote address, remove it from the
		// connection map.
		if c.rmraddr.Load() == nil {
			delete(c.listener.conns, c.raddr.String())
			c.rmraddr.Store(true)
		}
		nConns := len(c.listener.conns)
		c.listener.connLock.Unlock()

		if isAccepting, ok := c.listener.accepting.Load().(bool); nConns == 0 && !isAccepting && ok {
			// Wait if this is the final connection
			c.listener.readWG.Wait()
			if errClose, ok := c.listener.errClose.Load().(error); ok {
				err = errClose
			}
		} else {
			err = nil
		}

		if errBuf := c.buffer.Close(); errBuf != nil && err == nil {
			err = errBuf
		}
	})

	return err
}

// LocalAddr implements net.PacketConn.LocalAddr.
func (c *PacketConn) LocalAddr() net.Addr {
	return c.listener.pConn.LocalAddr()
}

// SetDeadline implements net.PacketConn.SetDeadline.
func (c *PacketConn) SetDeadline(t time.Time) error {
	c.writeDeadline.Set(t)

	return c.SetReadDeadline(t)
}

// SetReadDeadline implements net.PacketConn.SetReadDeadline.
func (c *PacketConn) SetReadDeadline(t time.Time) error {
	return c.buffer.SetReadDeadline(t)
}

// SetWriteDeadline implements net.PacketConn.SetWriteDeadline.
func (c *PacketConn) SetWriteDeadline(t time.Time) error {
	c.writeDeadline.Set(t)
	// Write deadline of underlying connection should not be changed
	// since the connection can be shared.
	return nil
}
