// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package ipnet contains helper functions around net and IP
package ipnet

import (
	"errors"
	"net"
)

var errFailedToCastAddr = errors.New("failed to cast net.Addr to *net.UDPAddr or *net.TCPAddr")

// AddrIPPort extracts the IP and Port from a net.Addr
func AddrIPPort(a net.Addr) (net.IP, int, error) {
	aUDP, ok := a.(*net.UDPAddr)
	if ok {
		return aUDP.IP, aUDP.Port, nil
	}

	aTCP, ok := a.(*net.TCPAddr)
	if ok {
		return aTCP.IP, aTCP.Port, nil
	}

	return nil, 0, errFailedToCastAddr
}

// AddrEqual asserts that two net.Addrs are equal
// Currently only supports UDP but will be extended in the future to support others
func AddrEqual(a, b net.Addr) bool {
	aUDP, ok := a.(*net.UDPAddr)
	if !ok {
		return false
	}

	bUDP, ok := b.(*net.UDPAddr)
	if !ok {
		return false
	}

	return aUDP.IP.Equal(bUDP.IP) && aUDP.Port == bUDP.Port
}

// FingerprintAddr generates a fingerprint from net.UDPAddr or net.TCPAddr's
// which can be used for indexing maps.
func FingerprintAddr(addr net.Addr) string {
	switch a := addr.(type) {
	case *net.UDPAddr:
		return a.IP.String()
	case *net.TCPAddr: // Do we really need this case?
		return a.IP.String()
	}
	return "" // Should never happen
}
