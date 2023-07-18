// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package handshake

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/pkg/crypto/hash"
	"github.com/pion/dtls/v2/pkg/crypto/signature"
)

// MessageCertificateVerify provide explicit verification of a
// client certificate.
//
// https://tools.ietf.org/html/rfc5246#section-7.4.8
type MessageCertificateVerify struct {
	HashAlgorithm      hash.Algorithm
	SignatureAlgorithm signature.Algorithm
	Signature          []byte
}

const handshakeMessageCertificateVerifyMinLength = 4

// Type returns the Handshake Type
func (m MessageCertificateVerify) Type() Type {
	return TypeCertificateVerify
}

// Marshal encodes the Handshake
func (m *MessageCertificateVerify) Marshal() ([]byte, error) {
	out := make([]byte, 1+1+2+len(m.Signature))

	out[0] = byte(m.HashAlgorithm)
	out[1] = byte(m.SignatureAlgorithm)
	binary.BigEndian.PutUint16(out[2:], uint16(len(m.Signature)))
	copy(out[4:], m.Signature)
	return out, nil
}

// Unmarshal populates the message from encoded data
func (m *MessageCertificateVerify) Unmarshal(data []byte) error {
	if len(data) < handshakeMessageCertificateVerifyMinLength {
		return errBufferTooSmall
	}

	m.HashAlgorithm = hash.Algorithm(data[0])
	if _, ok := hash.Algorithms()[m.HashAlgorithm]; !ok {
		return errInvalidHashAlgorithm
	}

	m.SignatureAlgorithm = signature.Algorithm(data[1])
	if _, ok := signature.Algorithms()[m.SignatureAlgorithm]; !ok {
		return errInvalidSignatureAlgorithm
	}

	signatureLength := int(binary.BigEndian.Uint16(data[2:]))
	if (signatureLength + 4) != len(data) {
		return errBufferTooSmall
	}

	m.Signature = append([]byte{}, data[4:]...)
	return nil
}
