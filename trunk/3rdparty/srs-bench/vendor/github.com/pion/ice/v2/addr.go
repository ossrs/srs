// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"
)

func parseMulticastAnswerAddr(in net.Addr) (net.IP, bool) {
	switch addr := in.(type) {
	case *net.IPAddr:
		return addr.IP, true
	case *net.UDPAddr:
		return addr.IP, true
	case *net.TCPAddr:
		return addr.IP, true
	}
	return nil, false
}

func parseAddr(in net.Addr) (net.IP, int, NetworkType, bool) {
	switch addr := in.(type) {
	case *net.UDPAddr:
		return addr.IP, addr.Port, NetworkTypeUDP4, true
	case *net.TCPAddr:
		return addr.IP, addr.Port, NetworkTypeTCP4, true
	}
	return nil, 0, 0, false
}

func createAddr(network NetworkType, ip net.IP, port int) net.Addr {
	switch {
	case network.IsTCP():
		return &net.TCPAddr{IP: ip, Port: port}
	default:
		return &net.UDPAddr{IP: ip, Port: port}
	}
}

func addrEqual(a, b net.Addr) bool {
	aIP, aPort, aType, aOk := parseAddr(a)
	if !aOk {
		return false
	}

	bIP, bPort, bType, bOk := parseAddr(b)
	if !bOk {
		return false
	}

	return aType == bType && aIP.Equal(bIP) && aPort == bPort
}

// AddrPort is  an IP and a port number.
type AddrPort [18]byte

func toAddrPort(addr net.Addr) AddrPort {
	var ap AddrPort
	switch addr := addr.(type) {
	case *net.UDPAddr:
		copy(ap[:16], addr.IP.To16())
		ap[16] = uint8(addr.Port >> 8)
		ap[17] = uint8(addr.Port)
	case *net.TCPAddr:
		copy(ap[:16], addr.IP.To16())
		ap[16] = uint8(addr.Port >> 8)
		ap[17] = uint8(addr.Port)
	}
	return ap
}
