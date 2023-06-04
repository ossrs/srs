// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package handshake

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/internal/ciphersuite/types"
	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
	"github.com/pion/dtls/v2/pkg/crypto/hash"
	"github.com/pion/dtls/v2/pkg/crypto/signature"
)

// MessageServerKeyExchange supports ECDH and PSK
type MessageServerKeyExchange struct {
	IdentityHint []byte

	EllipticCurveType  elliptic.CurveType
	NamedCurve         elliptic.Curve
	PublicKey          []byte
	HashAlgorithm      hash.Algorithm
	SignatureAlgorithm signature.Algorithm
	Signature          []byte

	// for unmarshaling
	KeyExchangeAlgorithm types.KeyExchangeAlgorithm
}

// Type returns the Handshake Type
func (m MessageServerKeyExchange) Type() Type {
	return TypeServerKeyExchange
}

// Marshal encodes the Handshake
func (m *MessageServerKeyExchange) Marshal() ([]byte, error) {
	var out []byte
	if m.IdentityHint != nil {
		out = append([]byte{0x00, 0x00}, m.IdentityHint...)
		binary.BigEndian.PutUint16(out, uint16(len(out)-2))
	}

	if m.EllipticCurveType == 0 || len(m.PublicKey) == 0 {
		return out, nil
	}
	out = append(out, byte(m.EllipticCurveType), 0x00, 0x00)
	binary.BigEndian.PutUint16(out[len(out)-2:], uint16(m.NamedCurve))

	out = append(out, byte(len(m.PublicKey)))
	out = append(out, m.PublicKey...)
	switch {
	case m.HashAlgorithm != hash.None && len(m.Signature) == 0:
		return nil, errInvalidHashAlgorithm
	case m.HashAlgorithm == hash.None && len(m.Signature) > 0:
		return nil, errInvalidHashAlgorithm
	case m.SignatureAlgorithm == signature.Anonymous && (m.HashAlgorithm != hash.None || len(m.Signature) > 0):
		return nil, errInvalidSignatureAlgorithm
	case m.SignatureAlgorithm == signature.Anonymous:
		return out, nil
	}

	out = append(out, []byte{byte(m.HashAlgorithm), byte(m.SignatureAlgorithm), 0x00, 0x00}...)
	binary.BigEndian.PutUint16(out[len(out)-2:], uint16(len(m.Signature)))
	out = append(out, m.Signature...)

	return out, nil
}

// Unmarshal populates the message from encoded data
func (m *MessageServerKeyExchange) Unmarshal(data []byte) error {
	switch {
	case len(data) < 2:
		return errBufferTooSmall
	case m.KeyExchangeAlgorithm == types.KeyExchangeAlgorithmNone:
		return errCipherSuiteUnset
	}

	hintLength := binary.BigEndian.Uint16(data)
	if int(hintLength) <= len(data)-2 && m.KeyExchangeAlgorithm.Has(types.KeyExchangeAlgorithmPsk) {
		m.IdentityHint = append([]byte{}, data[2:2+hintLength]...)
		data = data[2+hintLength:]
	}
	if m.KeyExchangeAlgorithm == types.KeyExchangeAlgorithmPsk {
		if len(data) == 0 {
			return nil
		}
		return errLengthMismatch
	}

	if !m.KeyExchangeAlgorithm.Has(types.KeyExchangeAlgorithmEcdhe) {
		return errLengthMismatch
	}

	if _, ok := elliptic.CurveTypes()[elliptic.CurveType(data[0])]; ok {
		m.EllipticCurveType = elliptic.CurveType(data[0])
	} else {
		return errInvalidEllipticCurveType
	}

	if len(data[1:]) < 2 {
		return errBufferTooSmall
	}
	m.NamedCurve = elliptic.Curve(binary.BigEndian.Uint16(data[1:3]))
	if _, ok := elliptic.Curves()[m.NamedCurve]; !ok {
		return errInvalidNamedCurve
	}
	if len(data) < 4 {
		return errBufferTooSmall
	}

	publicKeyLength := int(data[3])
	offset := 4 + publicKeyLength
	if len(data) < offset {
		return errBufferTooSmall
	}
	m.PublicKey = append([]byte{}, data[4:offset]...)

	// Anon connection doesn't contains hashAlgorithm, signatureAlgorithm, signature
	if len(data) == offset {
		return nil
	} else if len(data) <= offset {
		return errBufferTooSmall
	}

	m.HashAlgorithm = hash.Algorithm(data[offset])
	if _, ok := hash.Algorithms()[m.HashAlgorithm]; !ok {
		return errInvalidHashAlgorithm
	}
	offset++
	if len(data) <= offset {
		return errBufferTooSmall
	}
	m.SignatureAlgorithm = signature.Algorithm(data[offset])
	if _, ok := signature.Algorithms()[m.SignatureAlgorithm]; !ok {
		return errInvalidSignatureAlgorithm
	}
	offset++
	if len(data) < offset+2 {
		return errBufferTooSmall
	}
	signatureLength := int(binary.BigEndian.Uint16(data[offset:]))
	offset += 2
	if len(data) < offset+signatureLength {
		return errBufferTooSmall
	}
	m.Signature = append([]byte{}, data[offset:offset+signatureLength]...)
	return nil
}
