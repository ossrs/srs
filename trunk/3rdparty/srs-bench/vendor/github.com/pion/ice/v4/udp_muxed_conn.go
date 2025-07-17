// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"io"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
)

type udpMuxedConnState int

const (
	udpMuxedConnOpen udpMuxedConnState = iota
	udpMuxedConnWaiting
	udpMuxedConnClosed
)

type udpMuxedConnParams struct {
	Mux       *UDPMuxDefault
	AddrPool  *sync.Pool
	Key       string
	LocalAddr net.Addr
	Logger    logging.LeveledLogger
}

// udpMuxedConn represents a logical packet conn for a single remote as identified by ufrag.
type udpMuxedConn struct {
	params *udpMuxedConnParams
	// Remote addresses that we have sent to on this conn
	addresses []ipPort

	// FIFO queue holding incoming packets
	bufHead, bufTail *bufferHolder
	notify           chan struct{}
	closedChan       chan struct{}
	state            udpMuxedConnState
	mu               sync.Mutex
}

func newUDPMuxedConn(params *udpMuxedConnParams) *udpMuxedConn {
	return &udpMuxedConn{
		params:     params,
		notify:     make(chan struct{}, 1),
		closedChan: make(chan struct{}),
	}
}

func (c *udpMuxedConn) ReadFrom(b []byte) (n int, rAddr net.Addr, err error) {
	for {
		c.mu.Lock()
		if c.bufTail != nil {
			pkt := c.bufTail
			c.bufTail = pkt.next

			if pkt == c.bufHead {
				c.bufHead = nil
			}
			c.mu.Unlock()

			if len(b) < len(pkt.buf) {
				err = io.ErrShortBuffer
			} else {
				n = copy(b, pkt.buf)
				rAddr = pkt.addr
			}

			pkt.reset()
			c.params.AddrPool.Put(pkt)

			return n, rAddr, err
		}

		if c.state == udpMuxedConnClosed {
			c.mu.Unlock()

			return 0, nil, io.EOF
		}

		c.state = udpMuxedConnWaiting
		c.mu.Unlock()

		select {
		case <-c.notify:
		case <-c.closedChan:
			return 0, nil, io.EOF
		}
	}
}

func (c *udpMuxedConn) WriteTo(buf []byte, rAddr net.Addr) (n int, err error) {
	if c.isClosed() {
		return 0, io.ErrClosedPipe
	}
	// Each time we write to a new address, we'll register it with the mux
	netUDPAddr, ok := rAddr.(*net.UDPAddr)
	if !ok {
		return 0, errFailedToCastUDPAddr
	}

	//nolint:gosec // TODO add port validation G115
	ipAndPort, err := newIPPort(netUDPAddr.IP, netUDPAddr.Zone, uint16(netUDPAddr.Port))
	if err != nil {
		return 0, err
	}
	if !c.containsAddress(ipAndPort) {
		c.addAddress(ipAndPort)
	}

	return c.params.Mux.writeTo(buf, rAddr)
}

func (c *udpMuxedConn) LocalAddr() net.Addr {
	return c.params.LocalAddr
}

func (c *udpMuxedConn) SetDeadline(time.Time) error {
	return nil
}

func (c *udpMuxedConn) SetReadDeadline(time.Time) error {
	return nil
}

func (c *udpMuxedConn) SetWriteDeadline(time.Time) error {
	return nil
}

func (c *udpMuxedConn) CloseChannel() <-chan struct{} {
	return c.closedChan
}

func (c *udpMuxedConn) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state != udpMuxedConnClosed {
		for pkt := c.bufTail; pkt != nil; {
			next := pkt.next

			pkt.reset()
			c.params.AddrPool.Put(pkt)

			pkt = next
		}
		c.bufHead = nil
		c.bufTail = nil

		c.state = udpMuxedConnClosed
		close(c.closedChan)
	}

	return nil
}

func (c *udpMuxedConn) isClosed() bool {
	c.mu.Lock()
	defer c.mu.Unlock()

	return c.state == udpMuxedConnClosed
}

func (c *udpMuxedConn) getAddresses() []ipPort {
	c.mu.Lock()
	defer c.mu.Unlock()
	addresses := make([]ipPort, len(c.addresses))
	copy(addresses, c.addresses)

	return addresses
}

func (c *udpMuxedConn) addAddress(addr ipPort) {
	c.mu.Lock()
	c.addresses = append(c.addresses, addr)
	c.mu.Unlock()

	// Map it on mux
	c.params.Mux.registerConnForAddress(c, addr)
}

func (c *udpMuxedConn) removeAddress(addr ipPort) {
	c.mu.Lock()
	defer c.mu.Unlock()

	newAddresses := make([]ipPort, 0, len(c.addresses))
	for _, a := range c.addresses {
		if a != addr {
			newAddresses = append(newAddresses, a)
		}
	}

	c.addresses = newAddresses
}

func (c *udpMuxedConn) containsAddress(addr ipPort) bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	for _, a := range c.addresses {
		if addr == a {
			return true
		}
	}

	return false
}

func (c *udpMuxedConn) writePacket(data []byte, addr *net.UDPAddr) error {
	pkt := c.params.AddrPool.Get().(*bufferHolder) //nolint:forcetypeassert
	if cap(pkt.buf) < len(data) {
		c.params.AddrPool.Put(pkt)

		return io.ErrShortBuffer
	}

	pkt.buf = append(pkt.buf[:0], data...)
	pkt.addr = addr

	c.mu.Lock()
	if c.state == udpMuxedConnClosed {
		c.mu.Unlock()

		pkt.reset()
		c.params.AddrPool.Put(pkt)

		return io.ErrClosedPipe
	}

	if c.bufHead != nil {
		c.bufHead.next = pkt
	}
	c.bufHead = pkt

	if c.bufTail == nil {
		c.bufTail = pkt
	}

	state := c.state
	c.state = udpMuxedConnOpen
	c.mu.Unlock()

	if state == udpMuxedConnWaiting {
		select {
		case c.notify <- struct{}{}:
		default:
		}
	}

	return nil
}
