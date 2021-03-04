package proto

import (
	"fmt"
	"net"
)

// Addr is ip:port.
type Addr struct {
	IP   net.IP
	Port int
}

// Network implements net.Addr.
func (Addr) Network() string { return "turn" }

// FromUDPAddr sets addr to UDPAddr.
func (a *Addr) FromUDPAddr(n *net.UDPAddr) {
	a.IP = n.IP
	a.Port = n.Port
}

// Equal returns true if b == a.
func (a Addr) Equal(b Addr) bool {
	if a.Port != b.Port {
		return false
	}
	return a.IP.Equal(b.IP)
}

// EqualIP returns true if a and b have equal IP addresses.
func (a Addr) EqualIP(b Addr) bool {
	return a.IP.Equal(b.IP)
}

func (a Addr) String() string {
	return fmt.Sprintf("%s:%d", a.IP, a.Port)
}

// FiveTuple represents 5-TUPLE value.
type FiveTuple struct {
	Client Addr
	Server Addr
	Proto  Protocol
}

func (t FiveTuple) String() string {
	return fmt.Sprintf("%s->%s (%s)",
		t.Client, t.Server, t.Proto,
	)
}

// Equal returns true if b == t.
func (t FiveTuple) Equal(b FiveTuple) bool {
	if t.Proto != b.Proto {
		return false
	}
	if !t.Client.Equal(b.Client) {
		return false
	}
	if !t.Server.Equal(b.Server) {
		return false
	}
	return true
}
