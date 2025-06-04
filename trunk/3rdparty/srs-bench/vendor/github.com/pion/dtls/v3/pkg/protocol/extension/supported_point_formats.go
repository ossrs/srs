// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

import (
	"encoding/binary"

	"github.com/pion/dtls/v3/pkg/crypto/elliptic"
)

const (
	supportedPointFormatsSize = 5
)

// SupportedPointFormats allows a Client/Server to negotiate
// the EllipticCurvePointFormats
//
// https://tools.ietf.org/html/rfc4492#section-5.1.2
type SupportedPointFormats struct {
	PointFormats []elliptic.CurvePointFormat
}

// TypeValue returns the extension TypeValue.
func (s SupportedPointFormats) TypeValue() TypeValue {
	return SupportedPointFormatsTypeValue
}

// Marshal encodes the extension.
func (s *SupportedPointFormats) Marshal() ([]byte, error) {
	out := make([]byte, supportedPointFormatsSize)

	binary.BigEndian.PutUint16(out, uint16(s.TypeValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(1+(len(s.PointFormats)))) //nolint:gosec // G115
	out[4] = byte(len(s.PointFormats))

	for _, v := range s.PointFormats {
		out = append(out, byte(v)) //nolint:makezero // todo: fix
	}

	return out, nil
}

// Unmarshal populates the extension from encoded data.
func (s *SupportedPointFormats) Unmarshal(data []byte) error {
	if len(data) <= supportedPointFormatsSize {
		return errBufferTooSmall
	}

	if TypeValue(binary.BigEndian.Uint16(data)) != s.TypeValue() {
		return errInvalidExtensionType
	}

	pointFormatCount := int(data[4])
	if supportedPointFormatsSize+pointFormatCount > len(data) {
		return errLengthMismatch
	}

	for i := 0; i < pointFormatCount; i++ {
		p := elliptic.CurvePointFormat(data[supportedPointFormatsSize+i])
		switch p {
		case elliptic.CurvePointFormatUncompressed:
			s.PointFormats = append(s.PointFormats, p)
		default:
		}
	}

	return nil
}
