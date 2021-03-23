package proto

import (
	"strconv"

	"github.com/pion/stun"
)

// Protocol is IANA assigned protocol number.
type Protocol byte

const (
	// ProtoUDP is IANA assigned protocol number for UDP.
	ProtoUDP Protocol = 17
)

func (p Protocol) String() string {
	switch p {
	case ProtoUDP:
		return "UDP"
	default:
		return strconv.Itoa(int(p))
	}
}

// RequestedTransport represents REQUESTED-TRANSPORT attribute.
//
// This attribute is used by the client to request a specific transport
// protocol for the allocated transport address. RFC 5766 only allows the use of
// codepoint 17 (User Datagram Protocol).
//
// RFC 5766 Section 14.7
type RequestedTransport struct {
	Protocol Protocol
}

func (t RequestedTransport) String() string {
	return "protocol: " + t.Protocol.String()
}

const requestedTransportSize = 4

// AddTo adds REQUESTED-TRANSPORT to message.
func (t RequestedTransport) AddTo(m *stun.Message) error {
	v := make([]byte, requestedTransportSize)
	v[0] = byte(t.Protocol)
	// b[1:4] is RFFU = 0.
	// The RFFU field MUST be set to zero on transmission and MUST be
	// ignored on reception. It is reserved for future uses.
	m.Add(stun.AttrRequestedTransport, v)
	return nil
}

// GetFrom decodes REQUESTED-TRANSPORT from message.
func (t *RequestedTransport) GetFrom(m *stun.Message) error {
	v, err := m.Get(stun.AttrRequestedTransport)
	if err != nil {
		return err
	}
	if err = stun.CheckSize(stun.AttrRequestedTransport, len(v), requestedTransportSize); err != nil {
		return err
	}
	t.Protocol = Protocol(v[0])
	return nil
}
