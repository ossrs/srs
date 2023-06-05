// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/pkg/crypto/hash"
	"github.com/pion/dtls/v2/pkg/crypto/signature"
	"github.com/pion/dtls/v2/pkg/crypto/signaturehash"
)

const (
	supportedSignatureAlgorithmsHeaderSize = 6
)

// SupportedSignatureAlgorithms allows a Client/Server to
// negotiate what SignatureHash Algorithms they both support
//
// https://tools.ietf.org/html/rfc5246#section-7.4.1.4.1
type SupportedSignatureAlgorithms struct {
	SignatureHashAlgorithms []signaturehash.Algorithm
}

// TypeValue returns the extension TypeValue
func (s SupportedSignatureAlgorithms) TypeValue() TypeValue {
	return SupportedSignatureAlgorithmsTypeValue
}

// Marshal encodes the extension
func (s *SupportedSignatureAlgorithms) Marshal() ([]byte, error) {
	out := make([]byte, supportedSignatureAlgorithmsHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(s.TypeValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(2+(len(s.SignatureHashAlgorithms)*2)))
	binary.BigEndian.PutUint16(out[4:], uint16(len(s.SignatureHashAlgorithms)*2))
	for _, v := range s.SignatureHashAlgorithms {
		out = append(out, []byte{0x00, 0x00}...)
		out[len(out)-2] = byte(v.Hash)
		out[len(out)-1] = byte(v.Signature)
	}

	return out, nil
}

// Unmarshal populates the extension from encoded data
func (s *SupportedSignatureAlgorithms) Unmarshal(data []byte) error {
	if len(data) <= supportedSignatureAlgorithmsHeaderSize {
		return errBufferTooSmall
	} else if TypeValue(binary.BigEndian.Uint16(data)) != s.TypeValue() {
		return errInvalidExtensionType
	}

	algorithmCount := int(binary.BigEndian.Uint16(data[4:]) / 2)
	if supportedSignatureAlgorithmsHeaderSize+(algorithmCount*2) > len(data) {
		return errLengthMismatch
	}
	for i := 0; i < algorithmCount; i++ {
		supportedHashAlgorithm := hash.Algorithm(data[supportedSignatureAlgorithmsHeaderSize+(i*2)])
		supportedSignatureAlgorithm := signature.Algorithm(data[supportedSignatureAlgorithmsHeaderSize+(i*2)+1])
		if _, ok := hash.Algorithms()[supportedHashAlgorithm]; ok {
			if _, ok := signature.Algorithms()[supportedSignatureAlgorithm]; ok {
				s.SignatureHashAlgorithms = append(s.SignatureHashAlgorithms, signaturehash.Algorithm{
					Hash:      supportedHashAlgorithm,
					Signature: supportedSignatureAlgorithm,
				})
			}
		}
	}

	return nil
}
