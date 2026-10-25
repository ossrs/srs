// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"errors"
	"net"
	"sync"

	"github.com/pion/transport/v4"
	"github.com/pion/transport/v4/stdnet"
)

// A Rebinder moves the UDP socket of a session to another local port while
// the session runs, as a NAT rebinding or a phone that goes from Wi-Fi to 4G
// moves the address the server sees: pion keeps its ICE, DTLS and SRTP
// state, and only the source port of its packets changes. Set it in
// Options.Rebinder of one session, before it connects.
type Rebinder struct {
	mu   sync.Mutex
	conn *rebindConn
}

// Rebind opens a socket on another port of the same address and sends from
// it from now on. The earlier sockets stay open and read what still comes to
// them. It returns the index of the new socket; the first is 0.
func (r *Rebinder) Rebind() (int, error) {
	c, err := r.socket()
	if err != nil {
		return 0, err
	}
	local := c.UDPConn.LocalAddr().(*net.UDPAddr)
	s, err := net.ListenUDP("udp4", &net.UDPAddr{IP: local.IP})
	if err != nil {
		return 0, err
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	c.cur = c.add(s)
	return c.cur, nil
}

// Use sends from socket i again, such as the first one, 0.
func (r *Rebinder) Use(i int) error {
	c, err := r.socket()
	if err != nil {
		return err
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if i < 0 || i >= len(c.socks) {
		return errors.New("no such socket")
	}
	c.cur = i
	return nil
}

// HoldSTUN drops the STUN packets the session sends while hold is set, so a
// server that moves a session only on a STUN binding request gets its media
// from a new port before that.
func (r *Rebinder) HoldSTUN(hold bool) error {
	c, err := r.socket()
	if err != nil {
		return err
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	c.holdSTUN = hold
	return nil
}

// Media is how many RTP and RTCP packets came to socket i.
func (r *Rebinder) Media(i int) int {
	c, err := r.socket()
	if err != nil {
		return 0
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if i < 0 || i >= len(c.media) {
		return 0
	}
	return c.media[i]
}

func (r *Rebinder) socket() (*rebindConn, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.conn == nil {
		return nil, errors.New("the session has no UDP socket yet")
	}
	return r.conn, nil
}

// rebindNet is the network of a session with a Rebinder: its one UDP socket
// is the Rebinder's.
type rebindNet struct {
	*stdnet.Net
	r *Rebinder
}

func (n *rebindNet) ListenUDP(network string, laddr *net.UDPAddr) (transport.UDPConn, error) {
	s, err := net.ListenUDP(network, laddr)
	if err != nil {
		return nil, err
	}
	n.r.mu.Lock()
	defer n.r.mu.Unlock()
	if n.r.conn != nil {
		s.Close()
		return nil, errors.New("a Rebinder serves one UDP socket")
	}
	n.r.conn = newRebindConn(s)
	return n.r.conn, nil
}

// A rebindConn is pion's UDP socket: it reads from each of its sockets and
// writes from the current one. pion calls only ReadFrom, WriteTo, Close,
// LocalAddr and SetDeadline; the rest go to the first socket. It embeds the
// interface, not *net.UDPConn, so pion doesn't find the first socket's
// ReadFromAddrPort and read past ReadFrom.
type rebindConn struct {
	transport.UDPConn
	in   chan rebindPacket
	done chan struct{}
	once sync.Once

	mu       sync.Mutex
	socks    []*net.UDPConn
	media    []int
	cur      int
	holdSTUN bool
}

type rebindPacket struct {
	b    []byte
	from net.Addr
}

func newRebindConn(s *net.UDPConn) *rebindConn {
	c := &rebindConn{UDPConn: s, in: make(chan rebindPacket, 1024), done: make(chan struct{})}
	c.mu.Lock()
	defer c.mu.Unlock()
	c.add(s)
	return c
}

// add starts reading a socket, and returns its index. The caller holds mu.
func (c *rebindConn) add(s *net.UDPConn) int {
	c.socks = append(c.socks, s)
	c.media = append(c.media, 0)
	i := len(c.socks) - 1
	go c.read(i, s)
	return i
}

func (c *rebindConn) read(i int, s *net.UDPConn) {
	for {
		b := make([]byte, 1500)
		n, from, err := s.ReadFrom(b)
		if err != nil {
			return
		}
		if n > 0 && b[0] >= 128 && b[0] <= 191 {
			c.mu.Lock()
			c.media[i]++
			c.mu.Unlock()
		}
		select {
		case c.in <- rebindPacket{b[:n], from}:
		case <-c.done:
			return
		}
	}
}

func (c *rebindConn) ReadFrom(b []byte) (int, net.Addr, error) {
	select {
	case p := <-c.in:
		return copy(b, p.b), p.from, nil
	case <-c.done:
		return 0, nil, net.ErrClosed
	}
}

func (c *rebindConn) WriteTo(b []byte, to net.Addr) (int, error) {
	c.mu.Lock()
	s, hold := c.socks[c.cur], c.holdSTUN
	c.mu.Unlock()
	if hold && len(b) > 0 && b[0] <= 3 {
		return len(b), nil
	}
	return s.WriteTo(b, to)
}

func (c *rebindConn) Close() error {
	c.once.Do(func() {
		close(c.done)
		c.mu.Lock()
		defer c.mu.Unlock()
		for _, s := range c.socks {
			s.Close()
		}
	})
	return nil
}
