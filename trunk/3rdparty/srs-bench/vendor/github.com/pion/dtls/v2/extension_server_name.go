package dtls

import (
	"strings"

	"golang.org/x/crypto/cryptobyte"
)

const extensionServerNameTypeDNSHostName = 0

type extensionServerName struct {
	serverName string
}

func (e extensionServerName) extensionValue() extensionValue {
	return extensionServerNameValue
}

func (e *extensionServerName) Marshal() ([]byte, error) {
	var b cryptobyte.Builder
	b.AddUint16(uint16(e.extensionValue()))
	b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
		b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
			b.AddUint8(extensionServerNameTypeDNSHostName)
			b.AddUint16LengthPrefixed(func(b *cryptobyte.Builder) {
				b.AddBytes([]byte(e.serverName))
			})
		})
	})
	return b.Bytes()
}

func (e *extensionServerName) Unmarshal(data []byte) error {
	s := cryptobyte.String(data)
	var extension uint16
	s.ReadUint16(&extension)
	if extensionValue(extension) != e.extensionValue() {
		return errInvalidExtensionType
	}

	var extData cryptobyte.String
	s.ReadUint16LengthPrefixed(&extData)

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
		if nameType != extensionServerNameTypeDNSHostName {
			continue
		}
		if len(e.serverName) != 0 {
			// Multiple names of the same name_type are prohibited.
			return errInvalidSNIFormat
		}
		e.serverName = string(serverName)
		// An SNI value may not include a trailing dot.
		if strings.HasSuffix(e.serverName, ".") {
			return errInvalidSNIFormat
		}
	}
	return nil
}
