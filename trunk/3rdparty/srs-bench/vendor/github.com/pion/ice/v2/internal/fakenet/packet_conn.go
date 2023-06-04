// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package fakenet contains fake network abstractions
package fakenet

import (
	"net"
)

// Compile-time assertion
var _ net.PacketConn = (*PacketConn)(nil)

// PacketConn wraps a net.Conn and emulates net.PacketConn
type PacketConn struct {
	net.Conn
}

// ReadFrom reads a packet from the connection,
func (f *PacketConn) ReadFrom(p []byte) (n int, addr net.Addr, err error) {
	n, err = f.Conn.Read(p)
	addr = f.Conn.RemoteAddr()
	return
}

// WriteTo writes a packet with payload p to addr.
func (f *PacketConn) WriteTo(p []byte, _ net.Addr) (int, error) {
	return f.Conn.Write(p)
}
