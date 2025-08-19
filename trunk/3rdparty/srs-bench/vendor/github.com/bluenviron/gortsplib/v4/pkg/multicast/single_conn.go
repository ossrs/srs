//go:build !linux

package multicast

import (
	"net"
	"strconv"
	"time"

	"golang.org/x/net/ipv4"
)

const (
	// same size as GStreamer's rtspsrc
	multicastTTL = 16
)

// SingleConn is a multicast connection
// that works on a single interface.
type SingleConn struct {
	addr   *net.UDPAddr
	conn   *net.UDPConn
	connIP *ipv4.PacketConn
}

// NewSingleConn allocates a SingleConn.
func NewSingleConn(
	intf *net.Interface,
	address string,
	listenPacket func(network, address string) (net.PacketConn, error),
) (Conn, error) {
	addr, err := net.ResolveUDPAddr("udp4", address)
	if err != nil {
		return nil, err
	}

	tmp, err := listenPacket("udp4", "224.0.0.0:"+strconv.FormatInt(int64(addr.Port), 10))
	if err != nil {
		return nil, err
	}
	conn := tmp.(*net.UDPConn)

	connIP := ipv4.NewPacketConn(conn)

	err = connIP.JoinGroup(intf, &net.UDPAddr{IP: addr.IP})
	if err != nil {
		conn.Close() //nolint:errcheck
		return nil, err
	}

	err = connIP.SetMulticastInterface(intf)
	if err != nil {
		conn.Close() //nolint:errcheck
		return nil, err
	}

	err = connIP.SetMulticastTTL(multicastTTL)
	if err != nil {
		conn.Close() //nolint:errcheck
		return nil, err
	}

	return &SingleConn{
		addr:   addr,
		conn:   conn,
		connIP: connIP,
	}, nil
}

// Close implements Conn.
func (c *SingleConn) Close() error {
	return c.conn.Close()
}

// SetReadBuffer implements Conn.
func (c *SingleConn) SetReadBuffer(bytes int) error {
	return c.conn.SetReadBuffer(bytes)
}

// LocalAddr implements Conn.
func (c *SingleConn) LocalAddr() net.Addr {
	return c.conn.LocalAddr()
}

// SetDeadline implements Conn.
func (c *SingleConn) SetDeadline(_ time.Time) error {
	panic("unimplemented")
}

// SetReadDeadline implements Conn.
func (c *SingleConn) SetReadDeadline(t time.Time) error {
	return c.conn.SetReadDeadline(t)
}

// SetWriteDeadline implements Conn.
func (c *SingleConn) SetWriteDeadline(t time.Time) error {
	return c.conn.SetWriteDeadline(t)
}

// WriteTo implements Conn.
func (c *SingleConn) WriteTo(b []byte, addr net.Addr) (int, error) {
	return c.conn.WriteTo(b, addr)
}

// ReadFrom implements Conn.
func (c *SingleConn) ReadFrom(b []byte) (int, net.Addr, error) {
	return c.conn.ReadFrom(b)
}
