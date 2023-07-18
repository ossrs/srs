// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

import (
	"golang.org/x/crypto/cryptobyte"
)

// ALPN is a TLS extension for application-layer protocol negotiation within
// the TLS handshake.
//
// https://tools.ietf.org/html/rfc7301
type ALPN struct {
	ProtocolNameList []string
}

// TypeValue returns the extension TypeValue
func (a ALPN) TypeValue() TypeValue {
	return ALPNTypeValue
}

// Marshal encodes the extension
func (a *ALPN) Marshal() ([]byte, error) {
	var b cryptobyte.Builder
	b.AddUint16(uint16(a.TypeValue()))
	b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
		b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
			for _, proto := range a.ProtocolNameList {
				p := proto // Satisfy range scope lint
				b.AddUint8LengthPrefixed(func(b *cryptobyte.Builder) {
					b.AddBytes([]byte(p))
				})
			}
		})
	})
	return b.Bytes()
}

// Unmarshal populates the extension from encoded data
func (a *ALPN) Unmarshal(data []byte) error {
	val := cryptobyte.String(data)

	var extension uint16
	val.ReadUint16(&extension)
	if TypeValue(extension) != a.TypeValue() {
		return errInvalidExtensionType
	}

	var extData cryptobyte.String
	val.ReadUint16LengthPrefixed(&extData)

	var protoList cryptobyte.String
	if !extData.ReadUint16LengthPrefixed(&protoList) || protoList.Empty() {
		return ErrALPNInvalidFormat
	}
	for !protoList.Empty() {
		var proto cryptobyte.String
		if !protoList.ReadUint8LengthPrefixed(&proto) || proto.Empty() {
			return ErrALPNInvalidFormat
		}
		a.ProtocolNameList = append(a.ProtocolNameList, string(proto))
	}
	return nil
}

// ALPNProtocolSelection negotiates a shared protocol according to #3.2 of rfc7301
func ALPNProtocolSelection(supportedProtocols, peerSupportedProtocols []string) (string, error) {
	if len(supportedProtocols) == 0 || len(peerSupportedProtocols) == 0 {
		return "", nil
	}
	for _, s := range supportedProtocols {
		for _, c := range peerSupportedProtocols {
			if s == c {
				return s, nil
			}
		}
	}
	return "", errALPNNoAppProto
}
