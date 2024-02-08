// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

import (
	"strings"

	"golang.org/x/crypto/cryptobyte"
)

const serverNameTypeDNSHostName = 0

// ServerName allows the client to inform the server the specific
// name it wishes to contact. Useful if multiple DNS names resolve
// to one IP
//
// https://tools.ietf.org/html/rfc6066#section-3
type ServerName struct {
	ServerName string
}

// TypeValue returns the extension TypeValue
func (s ServerName) TypeValue() TypeValue {
	return ServerNameTypeValue
}

// Marshal encodes the extension
func (s *ServerName) Marshal() ([]byte, error) {
	var b cryptobyte.Builder
	b.AddUint16(uint16(s.TypeValue()))
	b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
		b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
			b.AddUint8(serverNameTypeDNSHostName)
			b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
				b.AddBytes([]byte(s.ServerName))
			})
		})
	})
	return b.Bytes()
}

// Unmarshal populates the extension from encoded data
func (s *ServerName) Unmarshal(data []byte) error {
	val := cryptobyte.String(data)
	var extension uint16
	val.ReadUint16(&extension)
	if TypeValue(extension) != s.TypeValue() {
		return errInvalidExtensionType
	}

	var extData cryptobyte.String
	val.ReadUint16LengthPrefixed(&extData)

	var nameList cryptobyte.String
	if !extData.ReadUint16LengthPrefixed(&nameList) || nameList.Empty() {
		return errInvalidSNIFormat
	}
	for !nameList.Empty() {
		var nameType uint8
		var serverName cryptobyte.String
		if !nameList.ReadUint8(&nameType) ||
			!nameList.ReadUint16LengthPrefixed(&serverName) ||
			serverName.Empty() {
			return errInvalidSNIFormat
		}
		if nameType != serverNameTypeDNSHostName {
			continue
		}
		if len(s.ServerName) != 0 {
			// Multiple names of the same name_type are prohibited.
			return errInvalidSNIFormat
		}
		s.ServerName = string(serverName)
		// An SNI value may not include a trailing dot.
		if strings.HasSuffix(s.ServerName, ".") {
			return errInvalidSNIFormat
		}
	}
	return nil
}
