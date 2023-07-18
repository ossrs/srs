// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

import "encoding/binary"

const (
	useExtendedMasterSecretHeaderSize = 4
)

// UseExtendedMasterSecret defines a TLS extension that contextually binds the
// master secret to a log of the full handshake that computes it, thus
// preventing MITM attacks.
type UseExtendedMasterSecret struct {
	Supported bool
}

// TypeValue returns the extension TypeValue
func (u UseExtendedMasterSecret) TypeValue() TypeValue {
	return UseExtendedMasterSecretTypeValue
}

// Marshal encodes the extension
func (u *UseExtendedMasterSecret) Marshal() ([]byte, error) {
	if !u.Supported {
		return []byte{}, nil
	}

	out := make([]byte, useExtendedMasterSecretHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(u.TypeValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(0)) // length
	return out, nil
}

// Unmarshal populates the extension from encoded data
func (u *UseExtendedMasterSecret) Unmarshal(data []byte) error {
	if len(data) < useExtendedMasterSecretHeaderSize {
		return errBufferTooSmall
	} else if TypeValue(binary.BigEndian.Uint16(data)) != u.TypeValue() {
		return errInvalidExtensionType
	}

	u.Supported = true

	return nil
}
