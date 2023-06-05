// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package extension implements the extension values in the ClientHello/ServerHello
package extension

import "encoding/binary"

// TypeValue is the 2 byte value for a TLS Extension as registered in the IANA
//
// https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml
type TypeValue uint16

// TypeValue constants
const (
	ServerNameTypeValue                   TypeValue = 0
	SupportedEllipticCurvesTypeValue      TypeValue = 10
	SupportedPointFormatsTypeValue        TypeValue = 11
	SupportedSignatureAlgorithmsTypeValue TypeValue = 13
	UseSRTPTypeValue                      TypeValue = 14
	ALPNTypeValue                         TypeValue = 16
	UseExtendedMasterSecretTypeValue      TypeValue = 23
	RenegotiationInfoTypeValue            TypeValue = 65281
)

// Extension represents a single TLS extension
type Extension interface {
	Marshal() ([]byte, error)
	Unmarshal(data []byte) error
	TypeValue() TypeValue
}

// Unmarshal many extensions at once
func Unmarshal(buf []byte) ([]Extension, error) {
	switch {
	case len(buf) == 0:
		return []Extension{}, nil
	case len(buf) < 2:
		return nil, errBufferTooSmall
	}

	declaredLen := binary.BigEndian.Uint16(buf)
	if len(buf)-2 != int(declaredLen) {
		return nil, errLengthMismatch
	}

	extensions := []Extension{}
	unmarshalAndAppend := func(data []byte, e Extension) error {
		err := e.Unmarshal(data)
		if err != nil {
			return err
		}
		extensions = append(extensions, e)
		return nil
	}

	for offset := 2; offset < len(buf); {
		if len(buf) < (offset + 2) {
			return nil, errBufferTooSmall
		}
		var err error
		switch TypeValue(binary.BigEndian.Uint16(buf[offset:])) {
		case ServerNameTypeValue:
			err = unmarshalAndAppend(buf[offset:], &ServerName{})
		case SupportedEllipticCurvesTypeValue:
			err = unmarshalAndAppend(buf[offset:], &SupportedEllipticCurves{})
		case UseSRTPTypeValue:
			err = unmarshalAndAppend(buf[offset:], &UseSRTP{})
		case ALPNTypeValue:
			err = unmarshalAndAppend(buf[offset:], &ALPN{})
		case UseExtendedMasterSecretTypeValue:
			err = unmarshalAndAppend(buf[offset:], &UseExtendedMasterSecret{})
		case RenegotiationInfoTypeValue:
			err = unmarshalAndAppend(buf[offset:], &RenegotiationInfo{})
		default:
		}
		if err != nil {
			return nil, err
		}
		if len(buf) < (offset + 4) {
			return nil, errBufferTooSmall
		}
		extensionLength := binary.BigEndian.Uint16(buf[offset+2:])
		offset += (4 + int(extensionLength))
	}
	return extensions, nil
}

// Marshal many extensions at once
func Marshal(e []Extension) ([]byte, error) {
	extensions := []byte{}
	for _, e := range e {
		raw, err := e.Marshal()
		if err != nil {
			return nil, err
		}
		extensions = append(extensions, raw...)
	}
	out := []byte{0x00, 0x00}
	binary.BigEndian.PutUint16(out, uint16(len(extensions)))
	return append(out, extensions...), nil
}
