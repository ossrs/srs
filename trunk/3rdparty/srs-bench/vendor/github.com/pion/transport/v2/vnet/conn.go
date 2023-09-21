// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"sync"
	"time"

	"github.com/pion/transport/v2"
)

const (
	maxReadQueueSize = 1024
)

var (
	errObsCannotBeNil       = errors.New("obs cannot be nil")
	errUseClosedNetworkConn = errors.New("use of closed network connection")
	errAddrNotUDPAddr       = errors.New("addr is not a net.UDPAddr")
	errLocAddr              = errors.New("something went wrong with locAddr")
	errAlreadyClosed        = errors.New("already closed")
	errNoRemAddr            = errors.New("no remAddr defined")
)

// vNet implements this
type connObserver interface {
	write(c Chunk) error
	onClosed(addr net.Addr)
	determineSourceIP(locIP, dstIP net.IP) net.IP
}

// UDPConn is the implementation of the Conn and PacketConn interfaces for UDP network connections.
// compatible with net.PacketConn and net.Conn
type UDPConn struct {
	locAddr   *net.UDPAddr // read-only
	remAddr   *net.UDPAddr // read-only
	obs       connObserver // read-only
	readCh    chan Chunk   // thread-safe
	closed    bool         // requires mutex
	mu        sync.Mutex   // to mutex closed flag
	readTimer *time.Timer  // thread-safe
}

var _ transport.UDPConn = &UDPConn{}

func newUDPConn(locAddr, remAddr *net.UDPAddr, obs connObserver) (*UDPConn, error) {
	if obs == nil {
		return nil, errObsCannotBeNil
	}

	return &UDPConn{
		locAddr:   locAddr,
		remAddr:   remAddr,
		obs:       obs,
		readCh:    make(chan Chunk, maxReadQueueSize),
		readTimer: time.NewTimer(time.Duration(math.MaxInt64)),
	}, nil
}

// Close closes the connection.
// Any blocked ReadFrom or WriteTo operations will be unblocked and return errors.
func (c *UDPConn) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.closed {
		return errAlreadyClosed
	}
	c.closed = true
	close(c.readCh)

	c.obs.onClosed(c.locAddr)
	return nil
}

// LocalAddr returns the local network address.
func (c *UDPConn) LocalAddr() net.Addr {
	return c.locAddr
}

// RemoteAddr returns the remote network address.
func (c *UDPConn) RemoteAddr() net.Addr {
	return c.remAddr
}

// SetDeadline sets the read and write deadlines associated
// with the connection. It is equivalent to calling both
// SetReadDeadline and SetWriteDeadline.
//
// A deadline is an absolute time after which I/O operations
// fail with a timeout (see type Error) instead of
// blocking. The deadline applies to all future and pending
// I/O, not just the immediately following call to ReadFrom or
// WriteTo. After a deadline has been exceeded, the connection
// can be refreshed by setting a deadline in the future.
//
// An idle timeout can be implemented by repeatedly extending
// the deadline after successful ReadFrom or WriteTo calls.
//
// A zero value for t means I/O operations will not time out.
func (c *UDPConn) SetDeadline(t time.Time) error {
	return c.SetReadDeadline(t)
}

// SetReadDeadline sets the deadline for future ReadFrom calls
// and any currently-blocked ReadFrom call.
// A zero value for t means ReadFrom will not time out.
func (c *UDPConn) SetReadDeadline(t time.Time) error {
	var d time.Duration
	var noDeadline time.Time
	if t == noDeadline {
		d = time.Duration(math.MaxInt64)
	} else {
		d = time.Until(t)
	}
	c.readTimer.Reset(d)
	return nil
}

// SetWriteDeadline sets the deadline for future WriteTo calls
// and any currently-blocked WriteTo call.
// Even if write times out, it may return n > 0, indicating that
// some of the data was successfully written.
// A zero value for t means WriteTo will not time out.
func (c *UDPConn) SetWriteDeadline(time.Time) error {
	// Write never blocks.
	return nil
}

// Read reads data from the connection.
// Read can be made to time out and return an Error with Timeout() == true
// after a fixed time limit; see SetDeadline and SetReadDeadline.
func (c *UDPConn) Read(b []byte) (int, error) {
	n, _, err := c.ReadFrom(b)
	return n, err
}

// ReadFrom reads a packet from the connection,
// copying the payload into p. It returns the number of
// bytes copied into p and the return address that
// was on the packet.
// It returns the number of bytes read (0 <= n <= len(p))
// and any error encountered. Callers should always process
// the n > 0 bytes returned before considering the error err.
// ReadFrom can be made to time out and return
// an Error with Timeout() == true after a fixed time limit;
// see SetDeadline and SetReadDeadline.
func (c *UDPConn) ReadFrom(p []byte) (n int, addr net.Addr, err error) {
loop:
	for {
		select {
		case chunk, ok := <-c.readCh:
			if !ok {
				break loop
			}
			var err error
			n := copy(p, chunk.UserData())
			addr := chunk.SourceAddr()
			if n < len(chunk.UserData()) {
				err = io.ErrShortBuffer
			}

			if c.remAddr != nil {
				if addr.String() != c.remAddr.String() {
					break // discard (shouldn't happen)
				}
			}
			return n, addr, err

		case <-c.readTimer.C:
			return 0, nil, &net.OpError{
				Op:   "read",
				Net:  c.locAddr.Network(),
				Addr: c.locAddr,
				Err:  newTimeoutError("i/o timeout"),
			}
		}
	}

	return 0, nil, &net.OpError{
		Op:   "read",
		Net:  c.locAddr.Network(),
		Addr: c.locAddr,
		Err:  errUseClosedNetworkConn,
	}
}

// ReadFromUDP acts like ReadFrom but returns a UDPAddr.
func (c *UDPConn) ReadFromUDP(b []byte) (int, *net.UDPAddr, error) {
	n, addr, err := c.ReadFrom(b)

	udpAddr, ok := addr.(*net.UDPAddr)
	if !ok {
		return -1, nil, fmt.Errorf("%w: %s", transport.ErrNotUDPAddress, addr)
	}

	return n, udpAddr, err
}

// ReadMsgUDP reads a message from c, copying the payload into b and
// the associated out-of-band data into oob. It returns the number of
// bytes copied into b, the number of bytes copied into oob, the flags
// that were set on the message and the source address of the message.
//
// The packages golang.org/x/net/ipv4 and golang.org/x/net/ipv6 can be
// used to manipulate IP-level socket options in oob.
func (c *UDPConn) ReadMsgUDP([]byte, []byte) (n, oobn, flags int, addr *net.UDPAddr, err error) {
	return -1, -1, -1, nil, transport.ErrNotSupported
}

// Write writes data to the connection.
// Write can be made to time out and return an Error with Timeout() == true
// after a fixed time limit; see SetDeadline and SetWriteDeadline.
func (c *UDPConn) Write(b []byte) (int, error) {
	if c.remAddr == nil {
		return 0, errNoRemAddr
	}

	return c.WriteTo(b, c.remAddr)
}

// WriteTo writes a packet with payload p to addr.
// WriteTo can be made to time out and return
// an Error with Timeout() == true after a fixed time limit;
// see SetDeadline and SetWriteDeadline.
// On packet-oriented connections, write timeouts are rare.
func (c *UDPConn) WriteTo(p []byte, addr net.Addr) (n int, err error) {
	dstAddr, ok := addr.(*net.UDPAddr)
	if !ok {
		return 0, errAddrNotUDPAddr
	}

	srcIP := c.obs.determineSourceIP(c.locAddr.IP, dstAddr.IP)
	if srcIP == nil {
		return 0, errLocAddr
	}
	srcAddr := &net.UDPAddr{
		IP:   srcIP,
		Port: c.locAddr.Port,
	}

	chunk := newChunkUDP(srcAddr, dstAddr)
	chunk.userData = make([]byte, len(p))
	copy(chunk.userData, p)
	if err := c.obs.write(chunk); err != nil {
		return 0, err
	}
	return len(p), nil
}

// WriteToUDP acts like WriteTo but takes a UDPAddr.
func (c *UDPConn) WriteToUDP(b []byte, addr *net.UDPAddr) (int, error) {
	return c.WriteTo(b, addr)
}

// WriteMsgUDP writes a message to addr via c if c isn't connected, or
// to c's remote address if c is connected (in which case addr must be
// nil). The payload is copied from b and the associated out-of-band
// data is copied from oob. It returns the number of payload and
// out-of-band bytes written.
//
// The packages golang.org/x/net/ipv4 and golang.org/x/net/ipv6 can be
// used to manipulate IP-level socket options in oob.
func (c *UDPConn) WriteMsgUDP([]byte, []byte, *net.UDPAddr) (n, oobn int, err error) {
	return -1, -1, transport.ErrNotSupported
}

// SetReadBuffer sets the size of the operating system's
// receive buffer associated with the connection.
func (c *UDPConn) SetReadBuffer(int) error {
	return transport.ErrNotSupported
}

// SetWriteBuffer sets the size of the operating system's
// transmit buffer associated with the connection.
func (c *UDPConn) SetWriteBuffer(int) error {
	return transport.ErrNotSupported
}

func (c *UDPConn) onInboundChunk(chunk Chunk) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.closed {
		return
	}

	select {
	case c.readCh <- chunk:
	default:
	}
}
