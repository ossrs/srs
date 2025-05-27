// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

import (
	"encoding/binary"
)

const (
	useSRTPHeaderSize = 6
)

// UseSRTP allows a Client/Server to negotiate what SRTPProtectionProfiles
// they both support
//
// https://tools.ietf.org/html/rfc8422
type UseSRTP struct {
	ProtectionProfiles  []SRTPProtectionProfile
	MasterKeyIdentifier []byte
}

// TypeValue returns the extension TypeValue.
func (u UseSRTP) TypeValue() TypeValue {
	return UseSRTPTypeValue
}

// Marshal encodes the extension.
func (u *UseSRTP) Marshal() ([]byte, error) {
	out := make([]byte, useSRTPHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(u.TypeValue()))
	//nolint:gosec // G115
	binary.BigEndian.PutUint16(
		out[2:],
		uint16(2+(len(u.ProtectionProfiles)*2)+ /* MKI Length */ 1+len(u.MasterKeyIdentifier)),
	)
	binary.BigEndian.PutUint16(out[4:], uint16(len(u.ProtectionProfiles)*2)) //nolint:gosec // G115

	for _, v := range u.ProtectionProfiles {
		out = append(out, []byte{0x00, 0x00}...) //nolint:makezero // todo: fix
		binary.BigEndian.PutUint16(out[len(out)-2:], uint16(v))
	}
	if len(u.MasterKeyIdentifier) > 255 {
		return nil, errMasterKeyIdentifierTooLarge
	}

	out = append(out, byte(len(u.MasterKeyIdentifier))) //nolint:makezero // todo: fix
	out = append(out, u.MasterKeyIdentifier...)         //nolint:makezero // todo: fix

	return out, nil
}

// Unmarshal populates the extension from encoded data.
func (u *UseSRTP) Unmarshal(data []byte) error {
	if len(data) <= useSRTPHeaderSize {
		return errBufferTooSmall
	} else if TypeValue(binary.BigEndian.Uint16(data)) != u.TypeValue() {
		return errInvalidExtensionType
	}

	profileCount := int(binary.BigEndian.Uint16(data[4:]) / 2)
	masterKeyIdentifierIndex := supportedGroupsHeaderSize + (profileCount * 2)
	if masterKeyIdentifierIndex+1 > len(data) {
		return errLengthMismatch
	}

	for i := 0; i < profileCount; i++ {
		supportedProfile := SRTPProtectionProfile(binary.BigEndian.Uint16(data[(useSRTPHeaderSize + (i * 2)):]))
		if _, ok := srtpProtectionProfiles()[supportedProfile]; ok {
			u.ProtectionProfiles = append(u.ProtectionProfiles, supportedProfile)
		}
	}

	masterKeyIdentifierLen := int(data[masterKeyIdentifierIndex])
	if masterKeyIdentifierIndex+masterKeyIdentifierLen >= len(data) {
		return errLengthMismatch
	}

	u.MasterKeyIdentifier = append(
		[]byte{},
		data[masterKeyIdentifierIndex+1:masterKeyIdentifierIndex+1+masterKeyIdentifierLen]...,
	)

	return nil
}
