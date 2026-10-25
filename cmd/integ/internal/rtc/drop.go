// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"encoding/binary"
	"net"
	"sync"

	"github.com/pion/transport/v4"
)

// A Dropper loses RTP packets of a session in its UDP socket, as a network
// does: a packet the session sends has passed its interceptors and SRTP, and
// never leaves; one it receives never reaches SRTP. The RTP header isn't
// encrypted, so it picks packets by their SSRC and sequence number. Set it
// in Options.Dropper of one session, before it connects.
type Dropper struct {
	mu sync.Mutex
	// ssrc is the stream it drops, n how many more new packets of it, and
	// times how many times in all it loses each.
	ssrc     uint32
	n, times int
	left     map[uint16]int
	dropped  map[uint32][]uint16
}

// Drop loses the next n RTP packets of the stream ssrc, and each time one of
// them comes again, until it is lost times in all: 1 loses each once, 2 also
// its first resend. A stream the session sends, or receives, as its SSRC
// says. It replaces the earlier Drop.
func (d *Dropper) Drop(ssrc uint32, n, times int) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.ssrc, d.n, d.times, d.left = ssrc, n, times, map[uint16]int{}
}

// Dropped is the sequence numbers of the packets of the stream ssrc the
// dropper lost, in order, a resend again.
func (d *Dropper) Dropped(ssrc uint32) []uint16 {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]uint16(nil), d.dropped[ssrc]...)
}

// lose reports whether to lose the packet b, and logs it when it does.
func (d *Dropper) lose(b []byte) bool {
	// RTP and RTCP start with version 2; RTCP has a packet type of 192 to
	// 223 where RTP has its marker and payload type, RFC 5761.
	if len(b) < 12 || b[0]>>6 != 2 || (b[1] >= 192 && b[1] <= 223) {
		return false
	}
	seq, ssrc := binary.BigEndian.Uint16(b[2:]), binary.BigEndian.Uint32(b[8:])
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.left == nil || ssrc != d.ssrc {
		return false
	}
	left, ok := d.left[seq]
	if !ok {
		if d.n == 0 {
			return false
		}
		d.n--
		left = d.times
	}
	if left == 0 {
		return false
	}
	d.left[seq] = left - 1
	if d.dropped == nil {
		d.dropped = map[uint32][]uint16{}
	}
	d.dropped[ssrc] = append(d.dropped[ssrc], seq)
	return true
}

// dropNet is the network of a session with a Dropper: each UDP socket it
// opens loses what the Dropper says.
type dropNet struct {
	transport.Net
	d *Dropper
}

func (n *dropNet) ListenUDP(network string, laddr *net.UDPAddr) (transport.UDPConn, error) {
	c, err := n.Net.ListenUDP(network, laddr)
	if err != nil {
		return nil, err
	}
	return &dropConn{UDPConn: c, d: n.d}, nil
}

// A dropConn is a UDP socket that loses what its Dropper says. It embeds
// the interface, as a rebindConn does, so pion reads only by ReadFrom.
type dropConn struct {
	transport.UDPConn
	d *Dropper
}

func (c *dropConn) ReadFrom(b []byte) (int, net.Addr, error) {
	for {
		n, from, err := c.UDPConn.ReadFrom(b)
		if err != nil || !c.d.lose(b[:n]) {
			return n, from, err
		}
	}
}

func (c *dropConn) WriteTo(b []byte, to net.Addr) (int, error) {
	if c.d.lose(b) {
		return len(b), nil
	}
	return c.UDPConn.WriteTo(b, to)
}
