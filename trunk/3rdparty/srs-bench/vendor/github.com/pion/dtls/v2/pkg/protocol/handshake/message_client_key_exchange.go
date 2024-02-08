// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package handshake

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/internal/ciphersuite/types"
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

	// for unmarshaling
	KeyExchangeAlgorithm types.KeyExchangeAlgorithm
}

// Type returns the Handshake Type
func (m MessageClientKeyExchange) Type() Type {
	return TypeClientKeyExchange
}

// Marshal encodes the Handshake
func (m *MessageClientKeyExchange) Marshal() (out []byte, err error) {
	if m.IdentityHint == nil && m.PublicKey == nil {
		return nil, errInvalidClientKeyExchange
	}

	if m.IdentityHint != nil {
		out = append([]byte{0x00, 0x00}, m.IdentityHint...)
		binary.BigEndian.PutUint16(out, uint16(len(out)-2))
	}

	if m.PublicKey != nil {
		out = append(out, byte(len(m.PublicKey)))
		out = append(out, m.PublicKey...)
	}

	return out, nil
}

// Unmarshal populates the message from encoded data
func (m *MessageClientKeyExchange) Unmarshal(data []byte) error {
	switch {
	case len(data) < 2:
		return errBufferTooSmall
	case m.KeyExchangeAlgorithm == types.KeyExchangeAlgorithmNone:
		return errCipherSuiteUnset
	}

	offset := 0
	if m.KeyExchangeAlgorithm.Has(types.KeyExchangeAlgorithmPsk) {
		pskLength := int(binary.BigEndian.Uint16(data))
		if pskLength > len(data)-2 {
			return errBufferTooSmall
		}

		m.IdentityHint = append([]byte{}, data[2:pskLength+2]...)
		offset += pskLength + 2
	}

	if m.KeyExchangeAlgorithm.Has(types.KeyExchangeAlgorithmEcdhe) {
		publicKeyLength := int(data[offset])
		if publicKeyLength > len(data)-1-offset {
			return errBufferTooSmall
		}

		m.PublicKey = append([]byte{}, data[offset+1:]...)
	}

	return nil
}
