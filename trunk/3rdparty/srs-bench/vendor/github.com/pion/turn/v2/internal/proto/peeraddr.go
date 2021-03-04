package proto

import (
	"net"

	"github.com/pion/stun"
)

// PeerAddress implements XOR-PEER-ADDRESS attribute.
//
// The XOR-PEER-ADDRESS specifies the address and port of the peer as
// seen from the TURN server. (For example, the peer's server-reflexive
// transport address if the peer is behind a NAT.)
//
// RFC 5766 Section 14.3
type PeerAddress struct {
	IP   net.IP
	Port int
}

func (a PeerAddress) String() string {
	return stun.XORMappedAddress(a).String()
}

// AddTo adds XOR-PEER-ADDRESS to message.
func (a PeerAddress) AddTo(m *stun.Message) error {
	return stun.XORMappedAddress(a).AddToAs(m, stun.AttrXORPeerAddress)
}

// GetFrom decodes XOR-PEER-ADDRESS from message.
func (a *PeerAddress) GetFrom(m *stun.Message) error {
	return (*stun.XORMappedAddress)(a).GetFromAs(m, stun.AttrXORPeerAddress)
}

// XORPeerAddress implements XOR-PEER-ADDRESS attribute.
//
// The XOR-PEER-ADDRESS specifies the address and port of the peer as
// seen from the TURN server. (For example, the peer's server-reflexive
// transport address if the peer is behind a NAT.)
//
// RFC 5766 Section 14.3
type XORPeerAddress = PeerAddress
