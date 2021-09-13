package handshake

import (
	"encoding/binary"
)

// MessageClientKeyExchange is a DTLS Handshake Message
// With this message, the premaster secret is set, either by direct
// transmission of the RSA-encrypted secret or by the transmission of
// Diffie-Hellman parameters that will allow each side to agree upon
// the same premaster secret.
//
// https://tools.ietf.org/html/rfc5246#section-7.4.7
type MessageClientKeyExchange struct {
	IdentityHint []byte
	PublicKey    []byte
}

// Type returns the Handshake Type
func (m MessageClientKeyExchange) Type() Type {
	return TypeClientKeyExchange
}

// Marshal encodes the Handshake
func (m *MessageClientKeyExchange) Marshal() ([]byte, error) {
	switch {
	case (m.IdentityHint != nil && m.PublicKey != nil) || (m.IdentityHint == nil && m.PublicKey == nil):
		return nil, errInvalidClientKeyExchange
	case m.PublicKey != nil:
		return append([]byte{byte(len(m.PublicKey))}, m.PublicKey...), nil
	default:
		out := append([]byte{0x00, 0x00}, m.IdentityHint...)
		binary.BigEndian.PutUint16(out, uint16(len(out)-2))
		return out, nil
	}
}

// Unmarshal populates the message from encoded data
func (m *MessageClientKeyExchange) Unmarshal(data []byte) error {
	if len(data) < 2 {
		return errBufferTooSmall
	}

	// If parsed as PSK return early and only populate PSK Identity Hint
	if pskLength := binary.BigEndian.Uint16(data); len(data) == int(pskLength+2) {
		m.IdentityHint = append([]byte{}, data[2:]...)
		return nil
	}

	if publicKeyLength := int(data[0]); len(data) != publicKeyLength+1 {
		return errBufferTooSmall
	}

	m.PublicKey = append([]byte{}, data[1:]...)
	return nil
}
