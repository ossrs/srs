// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"math/rand/v2"
	"net"
	"sync"
	"time"
)

// A FaultDirection is the way a datagram goes: in from the server, or out
// to it.
type FaultDirection int

const (
	FaultIn FaultDirection = iota
	FaultOut
)

// A FaultKind is the kind of datagram a fault loses, by its first byte as
// RFC 7983 says.
type FaultKind int

const (
	FaultSTUN FaultKind = iota
	FaultDTLS
)

// Faults loses and delays the datagrams of a raw session's socket, as a
// network does: the next STUN or DTLS datagrams each way, as many as Drop
// asks, or each at random at the rate of Loss; every datagram, media too,
// is delayed by Delay each way. Set Delay and Loss before the session
// connects; Drop can be called at any time.
type Faults struct {
	// Delay is the one-way delay, the same each way, so the round trip is
	// twice it. Datagrams keep their order.
	Delay time.Duration
	// Loss is the chance, from 0 to 1, that each STUN or DTLS datagram is
	// lost, each way. Media is never lost.
	Loss float64
	// Lose, when set, loses each STUN or DTLS datagram it returns true for,
	// either way, such as the check that embeds a given DTLS message. Set
	// it before the session connects.
	Lose func(dir FaultDirection, b []byte) bool
	// Seen, when set, is told of each datagram that is not lost, media too,
	// in the order each way goes, such as to count the media that comes
	// before a given DTLS datagram. Set it before the session connects.
	Seen func(dir FaultDirection, b []byte)

	mu      sync.Mutex
	drops   [2][2]int
	dropped [2][2]int
}

// Drop loses the next n datagrams of a kind that go a way.
func (f *Faults) Drop(dir FaultDirection, kind FaultKind, n int) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.drops[dir][kind] += n
}

// Dropped is how many datagrams of a kind that went a way were lost, by Drop
// or at random.
func (f *Faults) Dropped(dir FaultDirection, kind FaultKind) int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.dropped[dir][kind]
}

// lose tells whether a datagram going a way is lost, and tells Seen of it
// when it is not.
func (f *Faults) lose(dir FaultDirection, b []byte) bool {
	if f.drop(dir, b) {
		return true
	}
	if f.Seen != nil {
		f.Seen(dir, b)
	}
	return false
}

// drop tells whether a datagram going a way is lost.
func (f *Faults) drop(dir FaultDirection, b []byte) bool {
	var kind FaultKind
	switch {
	case len(b) > 0 && b[0] <= 3:
		kind = FaultSTUN
	case len(b) > 0 && b[0] >= 20 && b[0] <= 63:
		kind = FaultDTLS
	default:
		return false
	}
	// Lose is the caller's code, so it runs without mu.
	picked := f.Lose != nil && f.Lose(dir, b)
	f.mu.Lock()
	defer f.mu.Unlock()
	if picked {
		f.dropped[dir][kind]++
		return true
	}
	if f.drops[dir][kind] > 0 {
		f.drops[dir][kind]--
		f.dropped[dir][kind]++
		return true
	}
	if f.Loss > 0 && rand.Float64() < f.Loss {
		f.dropped[dir][kind]++
		return true
	}
	return false
}

// wrap returns c with the faults on what it reads and writes. c is one
// datagram per Read and per Write, as a UDP socket or a framedConn is.
func (f *Faults) wrap(c net.Conn) net.Conn {
	fc := &faultConn{Conn: c, f: f, in: make(chan faultPacket, 1024), out: make(chan faultPacket, 1024),
		closing: make(chan struct{})}
	go fc.read()
	if f.Delay > 0 {
		go fc.write()
	}
	return fc
}

// A faultPacket is a datagram and the time it is due.
type faultPacket struct {
	at time.Time
	b  []byte
}

// A faultConn is a connection with Faults: it reads the datagrams in the
// background, drops and delays them, and drops and delays what it writes.
type faultConn struct {
	net.Conn
	f       *Faults
	in, out chan faultPacket
	// err is the read error, set before in closes.
	err     error
	once    sync.Once
	closing chan struct{}
}

func (c *faultConn) read() {
	defer close(c.in)
	for {
		b := make([]byte, 1500)
		n, err := c.Conn.Read(b)
		if err != nil {
			c.err = err
			return
		}
		if c.f.lose(FaultIn, b[:n]) {
			continue
		}
		select {
		case c.in <- faultPacket{time.Now().Add(c.f.Delay), b[:n]}:
		case <-c.closing:
			c.err = net.ErrClosed
			return
		}
	}
}

// Read returns the next datagram once it is due.
func (c *faultConn) Read(b []byte) (int, error) {
	p, ok := <-c.in
	if !ok {
		return 0, c.err
	}
	if d := time.Until(p.at); d > 0 {
		time.Sleep(d)
	}
	return copy(b, p.b), nil
}

// Write sends a datagram, or drops it, or queues it until it is due.
func (c *faultConn) Write(b []byte) (int, error) {
	if c.f.lose(FaultOut, b) {
		return len(b), nil
	}
	if c.f.Delay <= 0 {
		return c.Conn.Write(b)
	}
	select {
	case c.out <- faultPacket{time.Now().Add(c.f.Delay), append([]byte(nil), b...)}:
		return len(b), nil
	case <-c.closing:
		return 0, net.ErrClosed
	}
}

func (c *faultConn) write() {
	for {
		select {
		case p := <-c.out:
			if d := time.Until(p.at); d > 0 {
				time.Sleep(d)
			}
			c.Conn.Write(p.b)
		case <-c.closing:
			return
		}
	}
}

func (c *faultConn) Close() error {
	c.once.Do(func() { close(c.closing) })
	return c.Conn.Close()
}

func (c *faultConn) unwrap() net.Conn { return c.Conn }
