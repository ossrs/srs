// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
)

const (
	supportedGroupsHeaderSize = 6
)

// SupportedEllipticCurves allows a Client/Server to communicate
// what curves they both support
//
// https://tools.ietf.org/html/rfc8422#section-5.1.1
type SupportedEllipticCurves struct {
	EllipticCurves []elliptic.Curve
}

// TypeValue returns the extension TypeValue
func (s SupportedEllipticCurves) TypeValue() TypeValue {
	return SupportedEllipticCurvesTypeValue
}

// Marshal encodes the extension
func (s *SupportedEllipticCurves) Marshal() ([]byte, error) {
	out := make([]byte, supportedGroupsHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(s.TypeValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(2+(len(s.EllipticCurves)*2)))
	binary.BigEndian.PutUint16(out[4:], uint16(len(s.EllipticCurves)*2))

	for _, v := range s.EllipticCurves {
		out = append(out, []byte{0x00, 0x00}...)
		binary.BigEndian.PutUint16(out[len(out)-2:], uint16(v))
	}

	return out, nil
}

// Unmarshal populates the extension from encoded data
func (s *SupportedEllipticCurves) Unmarshal(data []byte) error {
	if len(data) <= supportedGroupsHeaderSize {
		return errBufferTooSmall
	} else if TypeValue(binary.BigEndian.Uint16(data)) != s.TypeValue() {
		return errInvalidExtensionType
	}

	groupCount := int(binary.BigEndian.Uint16(data[4:]) / 2)
	if supportedGroupsHeaderSize+(groupCount*2) > len(data) {
		return errLengthMismatch
	}

	for i := 0; i < groupCount; i++ {
		supportedGroupID := elliptic.Curve(binary.BigEndian.Uint16(data[(supportedGroupsHeaderSize + (i * 2)):]))
		if _, ok := elliptic.Curves()[supportedGroupID]; ok {
			s.EllipticCurves = append(s.EllipticCurves, supportedGroupID)
		}
	}
	return nil
}
