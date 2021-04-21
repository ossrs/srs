package proto

import (
	"net"

	"github.com/pion/stun"
)

// RelayedAddress implements XOR-RELAYED-ADDRESS attribute.
//
// It specifies the address and port that the server allocated to the
// client. It is encoded in the same way as XOR-MAPPED-ADDRESS.
//
// RFC 5766 Section 14.5
type RelayedAddress struct {
	IP   net.IP
	Port int
}

func (a RelayedAddress) String() string {
	return stun.XORMappedAddress(a).String()
}

// AddTo adds XOR-PEER-ADDRESS to message.
func (a RelayedAddress) AddTo(m *stun.Message) error {
	return stun.XORMappedAddress(a).AddToAs(m, stun.AttrXORRelayedAddress)
}

// GetFrom decodes XOR-PEER-ADDRESS from message.
func (a *RelayedAddress) GetFrom(m *stun.Message) error {
	return (*stun.XORMappedAddress)(a).GetFromAs(m, stun.AttrXORRelayedAddress)
}

// XORRelayedAddress implements XOR-RELAYED-ADDRESS attribute.
//
// It specifies the address and port that the server allocated to the
// client. It is encoded in the same way as XOR-MAPPED-ADDRESS.
//
// RFC 5766 Section 14.5
type XORRelayedAddress = RelayedAddress
