// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package client

import (
	"errors"
	"net"

	"github.com/pion/transport/v3"
	"github.com/pion/turn/v4/internal/proto"
)

var (
	errInvalidTURNFrame    = errors.New("data is not a valid TURN frame, no STUN or ChannelData found")
	errIncompleteTURNFrame = errors.New("data contains incomplete STUN or TURN frame")
)

const (
	stunHeaderSize = 20
)

var _ transport.TCPConn = (*TCPConn)(nil) // Includes type check for net.Conn

// TCPConn wraps a transport.TCPConn and returns the allocations relayed
// transport address in response to TCPConn.LocalAddress()
type TCPConn struct {
	transport.TCPConn
	remoteAddress *net.TCPAddr
	allocation    *TCPAllocation
	ConnectionID  proto.ConnectionID
}

type connectionAttempt struct {
	from *net.TCPAddr
	cid  proto.ConnectionID
}

// LocalAddr returns the local network address.
// The Addr returned is shared by all invocations of LocalAddr, so do not modify it.
func (c *TCPConn) LocalAddr() net.Addr {
	return c.allocation.Addr()
}

// RemoteAddr returns the remote network address.
// The Addr returned is shared by all invocations of RemoteAddr, so do not modify it.
func (c *TCPConn) RemoteAddr() net.Addr {
	return c.remoteAddress
}
