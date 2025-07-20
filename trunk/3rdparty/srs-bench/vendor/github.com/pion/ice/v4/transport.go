// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"context"
	"net"
	"sync/atomic"
	"time"

	"github.com/pion/stun/v3"
)

// Dial connects to the remote agent, acting as the controlling ice agent.
// Dial blocks until at least one ice candidate pair has successfully connected.
func (a *Agent) Dial(ctx context.Context, remoteUfrag, remotePwd string) (*Conn, error) {
	return a.connect(ctx, true, remoteUfrag, remotePwd)
}

// Accept connects to the remote agent, acting as the controlled ice agent.
// Accept blocks until at least one ice candidate pair has successfully connected.
func (a *Agent) Accept(ctx context.Context, remoteUfrag, remotePwd string) (*Conn, error) {
	return a.connect(ctx, false, remoteUfrag, remotePwd)
}

// Conn represents the ICE connection.
// At the moment the lifetime of the Conn is equal to the Agent.
type Conn struct {
	bytesReceived uint64
	bytesSent     uint64
	agent         *Agent
}

// BytesSent returns the number of bytes sent.
func (c *Conn) BytesSent() uint64 {
	return atomic.LoadUint64(&c.bytesSent)
}

// BytesReceived returns the number of bytes received.
func (c *Conn) BytesReceived() uint64 {
	return atomic.LoadUint64(&c.bytesReceived)
}

func (a *Agent) connect(ctx context.Context, isControlling bool, remoteUfrag, remotePwd string) (*Conn, error) {
	err := a.loop.Err()
	if err != nil {
		return nil, err
	}
	err = a.startConnectivityChecks(isControlling, remoteUfrag, remotePwd) //nolint:contextcheck
	if err != nil {
		return nil, err
	}

	// Block until pair selected
	select {
	case <-a.loop.Done():
		return nil, a.loop.Err()
	case <-ctx.Done():
		return nil, ErrCanceledByCaller
	case <-a.onConnected:
	}

	return &Conn{
		agent: a,
	}, nil
}

// Read implements the Conn Read method.
func (c *Conn) Read(p []byte) (int, error) {
	err := c.agent.loop.Err()
	if err != nil {
		return 0, err
	}

	n, err := c.agent.buf.Read(p)
	atomic.AddUint64(&c.bytesReceived, uint64(n)) //nolint:gosec // G115

	return n, err
}

// Write implements the Conn Write method.
func (c *Conn) Write(packet []byte) (int, error) {
	err := c.agent.loop.Err()
	if err != nil {
		return 0, err
	}

	if stun.IsMessage(packet) {
		return 0, errWriteSTUNMessageToIceConn
	}

	pair := c.agent.getSelectedPair()
	if pair == nil {
		if err = c.agent.loop.Run(c.agent.loop, func(_ context.Context) {
			pair = c.agent.getBestValidCandidatePair()
		}); err != nil {
			return 0, err
		}

		if pair == nil {
			return 0, err
		}
	}

	atomic.AddUint64(&c.bytesSent, uint64(len(packet)))

	return pair.Write(packet)
}

// Close implements the Conn Close method. It is used to close
// the connection. Any calls to Read and Write will be unblocked and return an error.
func (c *Conn) Close() error {
	return c.agent.Close()
}

// LocalAddr returns the local address of the current selected pair or nil if there is none.
func (c *Conn) LocalAddr() net.Addr {
	pair := c.agent.getSelectedPair()
	if pair == nil {
		return nil
	}

	return pair.Local.addr()
}

// RemoteAddr returns the remote address of the current selected pair or nil if there is none.
func (c *Conn) RemoteAddr() net.Addr {
	pair := c.agent.getSelectedPair()
	if pair == nil {
		return nil
	}

	return pair.Remote.addr()
}

// SetDeadline is a stub.
func (c *Conn) SetDeadline(time.Time) error {
	return nil
}

// SetReadDeadline is a stub.
func (c *Conn) SetReadDeadline(time.Time) error {
	return nil
}

// SetWriteDeadline is a stub.
func (c *Conn) SetWriteDeadline(time.Time) error {
	return nil
}
