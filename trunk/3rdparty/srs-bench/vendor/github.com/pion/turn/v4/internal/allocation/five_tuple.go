// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package allocation

import (
	"net"
)

// Protocol is an enum for relay protocol
type Protocol uint8

// Network protocols for relay
const (
	UDP Protocol = iota
	TCP
)

// FiveTuple is the combination (client IP address and port, server IP
// address and port, and transport protocol (currently one of UDP,
// TCP, or TLS)) used to communicate between the client and the
// server.  The 5-tuple uniquely identifies this communication
// stream.  The 5-tuple also uniquely identifies the Allocation on
// the server.
type FiveTuple struct {
	Protocol
	SrcAddr, DstAddr net.Addr
}

// Equal asserts if two FiveTuples are equal
func (f *FiveTuple) Equal(b *FiveTuple) bool {
	return f.Fingerprint() == b.Fingerprint()
}

// FiveTupleFingerprint is a comparable representation of a FiveTuple
type FiveTupleFingerprint struct {
	srcIP, dstIP     [16]byte
	srcPort, dstPort uint16
	protocol         Protocol
}

// Fingerprint is the identity of a FiveTuple
func (f *FiveTuple) Fingerprint() (fp FiveTupleFingerprint) {
	srcIP, srcPort := netAddrIPAndPort(f.SrcAddr)
	copy(fp.srcIP[:], srcIP)
	fp.srcPort = srcPort
	dstIP, dstPort := netAddrIPAndPort(f.DstAddr)
	copy(fp.dstIP[:], dstIP)
	fp.dstPort = dstPort
	fp.protocol = f.Protocol
	return
}

func netAddrIPAndPort(addr net.Addr) (net.IP, uint16) {
	switch a := addr.(type) {
	case *net.UDPAddr:
		return a.IP.To16(), uint16(a.Port)
	case *net.TCPAddr:
		return a.IP.To16(), uint16(a.Port)
	default:
		return nil, 0
	}
}
