package allocation

import (
	"fmt"
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

// Fingerprint is the identity of a FiveTuple
func (f *FiveTuple) Fingerprint() string {
	return fmt.Sprintf("%d_%s_%s", f.Protocol, f.SrcAddr.String(), f.DstAddr.String())
}
