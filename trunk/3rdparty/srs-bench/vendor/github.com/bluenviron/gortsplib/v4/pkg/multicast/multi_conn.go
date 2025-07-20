//go:build !linux

package multicast

import (
	"fmt"
	"net"
	"strconv"
	"time"

	"golang.org/x/net/ipv4"
)

// MultiConn is a multicast connection
// that works in parallel on all interfaces.
type MultiConn struct {
	addr         *net.UDPAddr
	readConn     *net.UDPConn
	readConnIP   *ipv4.PacketConn
	writeConns   []*net.UDPConn
	writeConnIPs []*ipv4.PacketConn
}

// NewMultiConn allocates a MultiConn.
func NewMultiConn(
	address string,
	readOnly bool,
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
	readConn := tmp.(*net.UDPConn)

	intfs, err := net.Interfaces()
	if err != nil {
		readConn.Close() //nolint:errcheck
		return nil, err
	}

	readConnIP := ipv4.NewPacketConn(readConn)

	var enabledInterfaces []*net.Interface //nolint:prealloc

	for _, intf := range intfs {
		if (intf.Flags & net.FlagMulticast) == 0 {
			continue
		}
		cintf := intf

		err = readConnIP.JoinGroup(&cintf, &net.UDPAddr{IP: addr.IP})
		if err != nil {
			continue
		}

		enabledInterfaces = append(enabledInterfaces, &cintf)
	}

	if enabledInterfaces == nil {
		readConn.Close() //nolint:errcheck
		return nil, fmt.Errorf("no multicast-capable interfaces found")
	}

	var writeConns []*net.UDPConn
	var writeConnIPs []*ipv4.PacketConn

	if !readOnly {
		writeConns = make([]*net.UDPConn, len(enabledInterfaces))
		writeConnIPs = make([]*ipv4.PacketConn, len(enabledInterfaces))

		for i, intf := range enabledInterfaces {
			tmp, err := listenPacket("udp4", "224.0.0.0:"+strconv.FormatInt(int64(addr.Port), 10))
			if err != nil {
				for j := 0; j < i; j++ {
					writeConns[j].Close() //nolint:errcheck
				}
				readConn.Close() //nolint:errcheck
				return nil, err
			}
			writeConn := tmp.(*net.UDPConn)

			writeConnIP := ipv4.NewPacketConn(writeConn)

			err = writeConnIP.SetMulticastInterface(intf)
			if err != nil {
				for j := 0; j < i; j++ {
					writeConns[j].Close() //nolint:errcheck
				}
				readConn.Close() //nolint:errcheck
				return nil, err
			}

			err = writeConnIP.SetMulticastTTL(multicastTTL)
			if err != nil {
				for j := 0; j < i; j++ {
					writeConns[j].Close() //nolint:errcheck
				}
				readConn.Close() //nolint:errcheck
				return nil, err
			}

			writeConns[i] = writeConn
			writeConnIPs[i] = writeConnIP
		}
	}

	return &MultiConn{
		addr:         addr,
		readConn:     readConn,
		readConnIP:   readConnIP,
		writeConns:   writeConns,
		writeConnIPs: writeConnIPs,
	}, nil
}

// Close implements Conn.
func (c *MultiConn) Close() error {
	for _, c := range c.writeConns {
		c.Close() //nolint:errcheck
	}
	c.readConn.Close() //nolint:errcheck
	return nil
}

// SetReadBuffer implements Conn.
func (c *MultiConn) SetReadBuffer(bytes int) error {
	return c.readConn.SetReadBuffer(bytes)
}

// LocalAddr implements Conn.
func (c *MultiConn) LocalAddr() net.Addr {
	return c.readConn.LocalAddr()
}

// SetDeadline implements Conn.
func (c *MultiConn) SetDeadline(_ time.Time) error {
	panic("unimplemented")
}

// SetReadDeadline implements Conn.
func (c *MultiConn) SetReadDeadline(t time.Time) error {
	return c.readConn.SetReadDeadline(t)
}

// SetWriteDeadline implements Conn.
func (c *MultiConn) SetWriteDeadline(t time.Time) error {
	var err error
	for _, c := range c.writeConns {
		err2 := c.SetWriteDeadline(t)
		if err == nil {
			err = err2
		}
	}
	return err
}

// WriteTo implements Conn.
func (c *MultiConn) WriteTo(b []byte, addr net.Addr) (int, error) {
	var n int
	var err error
	for _, c := range c.writeConns {
		var err2 error
		n, err2 = c.WriteTo(b, addr)
		if err == nil {
			err = err2
		}
	}
	return n, err
}

// ReadFrom implements Conn.
func (c *MultiConn) ReadFrom(b []byte) (int, net.Addr, error) {
	return c.readConn.ReadFrom(b)
}
