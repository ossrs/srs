//go:build linux

package multicast

import (
	"fmt"
	"net"
	"os"
	"syscall"
	"time"
)

// MultiConn is a multicast connection
// that works in parallel on all interfaces.
type MultiConn struct {
	addr       *net.UDPAddr
	readFile   *os.File
	readConn   net.PacketConn
	writeFiles []*os.File
	writeConns []net.PacketConn
}

// NewMultiConn allocates a MultiConn.
func NewMultiConn(
	address string,
	readOnly bool,
	_ func(network, address string) (net.PacketConn, error),
) (Conn, error) {
	addr, err := net.ResolveUDPAddr("udp4", address)
	if err != nil {
		return nil, err
	}

	readSock, err := syscall.Socket(syscall.AF_INET, syscall.SOCK_DGRAM, syscall.IPPROTO_UDP)
	if err != nil {
		return nil, err
	}

	err = syscall.SetsockoptInt(readSock, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
	if err != nil {
		syscall.Close(readSock) //nolint:errcheck
		return nil, err
	}

	var lsa syscall.SockaddrInet4
	lsa.Port = addr.Port
	copy(lsa.Addr[:], addr.IP.To4())
	err = syscall.Bind(readSock, &lsa)
	if err != nil {
		syscall.Close(readSock) //nolint:errcheck
		return nil, err
	}

	intfs, err := net.Interfaces()
	if err != nil {
		syscall.Close(readSock) //nolint:errcheck
		return nil, err
	}

	var enabledInterfaces []*net.Interface //nolint:prealloc
	for _, intf := range intfs {
		if (intf.Flags & net.FlagMulticast) == 0 {
			continue
		}
		cintf := intf

		var mreq syscall.IPMreq
		copy(mreq.Multiaddr[:], addr.IP.To4())
		err = setIPMreqInterface(&mreq, &cintf)
		if err != nil {
			continue
		}

		err = syscall.SetsockoptIPMreq(readSock, syscall.IPPROTO_IP, syscall.IP_ADD_MEMBERSHIP, &mreq)
		if err != nil {
			continue
		}

		enabledInterfaces = append(enabledInterfaces, &cintf)
	}

	if enabledInterfaces == nil {
		syscall.Close(readSock) //nolint:errcheck
		return nil, fmt.Errorf("no multicast-capable interfaces found")
	}

	var writeFiles []*os.File
	var writeConns []net.PacketConn

	if !readOnly {
		writeSocks := make([]int, len(enabledInterfaces))

		for i, intf := range enabledInterfaces {
			writeSock, err := syscall.Socket(syscall.AF_INET, syscall.SOCK_DGRAM, syscall.IPPROTO_UDP)
			if err != nil {
				for j := 0; j < i; j++ {
					syscall.Close(writeSocks[j]) //nolint:errcheck
				}
				syscall.Close(readSock) //nolint:errcheck
				return nil, err
			}

			err = syscall.SetsockoptInt(writeSock, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
			if err != nil {
				syscall.Close(writeSock) //nolint:errcheck
				for j := 0; j < i; j++ {
					syscall.Close(writeSocks[j]) //nolint:errcheck
				}
				syscall.Close(readSock) //nolint:errcheck
				return nil, err
			}

			var lsa syscall.SockaddrInet4
			lsa.Port = addr.Port
			copy(lsa.Addr[:], addr.IP.To4())
			err = syscall.Bind(writeSock, &lsa)
			if err != nil {
				syscall.Close(writeSock) //nolint:errcheck
				for j := 0; j < i; j++ {
					syscall.Close(writeSocks[j]) //nolint:errcheck
				}
				syscall.Close(readSock) //nolint:errcheck
				return nil, err
			}

			var mreqn syscall.IPMreqn
			mreqn.Ifindex = int32(intf.Index)

			err = syscall.SetsockoptIPMreqn(writeSock, syscall.IPPROTO_IP, syscall.IP_MULTICAST_IF, &mreqn)
			if err != nil {
				syscall.Close(writeSock) //nolint:errcheck
				for j := 0; j < i; j++ {
					syscall.Close(writeSocks[j]) //nolint:errcheck
				}
				syscall.Close(readSock) //nolint:errcheck
				return nil, err
			}

			err = syscall.SetsockoptInt(writeSock, syscall.IPPROTO_IP, syscall.IP_MULTICAST_TTL, multicastTTL)
			if err != nil {
				syscall.Close(writeSock) //nolint:errcheck
				for j := 0; j < i; j++ {
					syscall.Close(writeSocks[j]) //nolint:errcheck
				}
				syscall.Close(readSock) //nolint:errcheck
				return nil, err
			}

			writeSocks[i] = writeSock
		}

		writeFiles = make([]*os.File, len(writeSocks))
		writeConns = make([]net.PacketConn, len(writeSocks))

		for i, writeSock := range writeSocks {
			writeFiles[i] = os.NewFile(uintptr(writeSock), "")
			writeConns[i], _ = net.FilePacketConn(writeFiles[i])
		}
	}

	readFile := os.NewFile(uintptr(readSock), "")
	readConn, _ := net.FilePacketConn(readFile)

	return &MultiConn{
		addr:       addr,
		readFile:   readFile,
		readConn:   readConn,
		writeFiles: writeFiles,
		writeConns: writeConns,
	}, nil
}

// Close implements Conn.
func (c *MultiConn) Close() error {
	for i, writeConn := range c.writeConns {
		writeConn.Close()
		c.writeFiles[i].Close()
	}
	c.readConn.Close()
	c.readFile.Close()
	return nil
}

// SetReadBuffer implements Conn.
func (c *MultiConn) SetReadBuffer(bytes int) error {
	return syscall.SetsockoptInt(int(c.readFile.Fd()), syscall.SOL_SOCKET, syscall.SO_RCVBUF, bytes)
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
