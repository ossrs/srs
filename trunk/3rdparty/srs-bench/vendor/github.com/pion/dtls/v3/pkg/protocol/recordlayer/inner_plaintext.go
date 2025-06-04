// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package recordlayer

import (
	"github.com/pion/dtls/v3/pkg/protocol"
	"golang.org/x/crypto/cryptobyte"
)

// InnerPlaintext implements DTLSInnerPlaintext
//
// https://datatracker.ietf.org/doc/html/rfc9146#name-record-layer-extensions
type InnerPlaintext struct {
	Content  []byte
	RealType protocol.ContentType
	Zeros    uint
}

// Marshal encodes a DTLS InnerPlaintext to binary.
func (p *InnerPlaintext) Marshal() ([]byte, error) {
	var out cryptobyte.Builder
	out.AddBytes(p.Content)
	out.AddUint8(uint8(p.RealType))
	out.AddBytes(make([]byte, p.Zeros))

	return out.Bytes()
}

// Unmarshal populates a DTLS InnerPlaintext from binary.
func (p *InnerPlaintext) Unmarshal(data []byte) error {
	// Process in reverse
	i := len(data) - 1
	for i >= 0 {
		if data[i] != 0 {
			p.Zeros = uint(len(data) - 1 - i) //nolint:gosec // G115

			break
		}
		i--
	}
	if i == 0 {
		return errBufferTooSmall
	}
	p.RealType = protocol.ContentType(data[i])
	p.Content = append([]byte{}, data[:i]...)

	return nil
}
