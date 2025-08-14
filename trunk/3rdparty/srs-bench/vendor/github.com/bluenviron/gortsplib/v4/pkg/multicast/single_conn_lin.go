//go:build linux

package multicast

import (
	"fmt"
	"net"
	"os"
	"syscall"
	"time"
)

const (
	// same size as GStreamer's rtspsrc
	multicastTTL = 16
)

// https://cs.opensource.google/go/x/net/+/refs/tags/v0.15.0:ipv4/sys_asmreq.go;l=51
func setIPMreqInterface(mreq *syscall.IPMreq, ifi *net.Interface) error {
	if ifi == nil {
		return nil
	}
	ifat, err := ifi.Addrs()
	if err != nil {
		return err
	}
	for _, ifa := range ifat {
		switch ifa := ifa.(type) {
		case *net.IPAddr:
			if ip := ifa.IP.To4(); ip != nil {
				copy(mreq.Interface[:], ip)
				return nil
			}
		case *net.IPNet:
			if ip := ifa.IP.To4(); ip != nil {
				copy(mreq.Interface[:], ip)
				return nil
			}
		}
	}
	return fmt.Errorf("no such interface")
}

// SingleConn is a multicast connection
// that works on a single interface.
type SingleConn struct {
	addr *net.UDPAddr
	file *os.File
	conn net.PacketConn
}

// NewSingleConn allocates a SingleConn.
func NewSingleConn(
	intf *net.Interface,
	address string,
	_ func(network, address string) (net.PacketConn, error),
) (Conn, error) {
	addr, err := net.ResolveUDPAddr("udp4", address)
	if err != nil {
		return nil, err
	}

	sock, err := syscall.Socket(syscall.AF_INET, syscall.SOCK_DGRAM, syscall.IPPROTO_UDP)
	if err != nil {
		return nil, err
	}

	err = syscall.SetsockoptInt(sock, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
	if err != nil {
		syscall.Close(sock) //nolint:errcheck
		return nil, err
	}

	err = syscall.SetsockoptString(sock, syscall.SOL_SOCKET, syscall.SO_BINDTODEVICE, intf.Name)
	if err != nil {
		syscall.Close(sock) //nolint:errcheck
		return nil, err
	}

	var lsa syscall.SockaddrInet4
	lsa.Port = addr.Port
	copy(lsa.Addr[:], addr.IP.To4())
	err = syscall.Bind(sock, &lsa)
	if err != nil {
		syscall.Close(sock) //nolint:errcheck
		return nil, err
	}

	var mreq syscall.IPMreq
	copy(mreq.Multiaddr[:], addr.IP.To4())
	err = setIPMreqInterface(&mreq, intf)
	if err != nil {
		syscall.Close(sock) //nolint:errcheck
		return nil, err
	}

	err = syscall.SetsockoptIPMreq(sock, syscall.IPPROTO_IP, syscall.IP_ADD_MEMBERSHIP, &mreq)
	if err != nil {
		syscall.Close(sock) //nolint:errcheck
		return nil, err
	}

	var mreqn syscall.IPMreqn
	mreqn.Ifindex = int32(intf.Index)

	err = syscall.SetsockoptIPMreqn(sock, syscall.IPPROTO_IP, syscall.IP_MULTICAST_IF, &mreqn)
	if err != nil {
		syscall.Close(sock) //nolint:errcheck
		return nil, err
	}

	err = syscall.SetsockoptInt(sock, syscall.IPPROTO_IP, syscall.IP_MULTICAST_TTL, multicastTTL)
	if err != nil {
		syscall.Close(sock) //nolint:errcheck
		return nil, err
	}

	file := os.NewFile(uintptr(sock), "")
	conn, err := net.FilePacketConn(file)
	if err != nil {
		file.Close()
		return nil, err
	}

	return &SingleConn{
		addr: addr,
		file: file,
		conn: conn,
	}, nil
}

// Close implements Conn.
func (c *SingleConn) Close() error {
	c.conn.Close()
	c.file.Close()
	return nil
}

// SetReadBuffer implements Conn.
func (c *SingleConn) SetReadBuffer(bytes int) error {
	return syscall.SetsockoptInt(int(c.file.Fd()), syscall.SOL_SOCKET, syscall.SO_RCVBUF, bytes)
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
