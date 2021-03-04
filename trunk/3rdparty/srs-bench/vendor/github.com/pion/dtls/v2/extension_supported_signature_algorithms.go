package dtls

import (
	"encoding/binary"
)

const (
	extensionSupportedSignatureAlgorithmsHeaderSize = 6
)

// https://tools.ietf.org/html/rfc5246#section-7.4.1.4.1
type extensionSupportedSignatureAlgorithms struct {
	signatureHashAlgorithms []signatureHashAlgorithm
}

func (e extensionSupportedSignatureAlgorithms) extensionValue() extensionValue {
	return extensionSupportedSignatureAlgorithmsValue
}

func (e *extensionSupportedSignatureAlgorithms) Marshal() ([]byte, error) {
	out := make([]byte, extensionSupportedSignatureAlgorithmsHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(e.extensionValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(2+(len(e.signatureHashAlgorithms)*2)))
	binary.BigEndian.PutUint16(out[4:], uint16(len(e.signatureHashAlgorithms)*2))
	for _, v := range e.signatureHashAlgorithms {
		out = append(out, []byte{0x00, 0x00}...)
		out[len(out)-2] = byte(v.hash)
		out[len(out)-1] = byte(v.signature)
	}

	return out, nil
}

func (e *extensionSupportedSignatureAlgorithms) Unmarshal(data []byte) error {
	if len(data) <= extensionSupportedSignatureAlgorithmsHeaderSize {
		return errBufferTooSmall
	} else if extensionValue(binary.BigEndian.Uint16(data)) != e.extensionValue() {
		return errInvalidExtensionType
	}

	algorithmCount := int(binary.BigEndian.Uint16(data[4:]) / 2)
	if extensionSupportedSignatureAlgorithmsHeaderSize+(algorithmCount*2) > len(data) {
		return errLengthMismatch
	}
	for i := 0; i < algorithmCount; i++ {
		supportedHashAlgorithm := hashAlgorithm(data[extensionSupportedSignatureAlgorithmsHeaderSize+(i*2)])
		supportedSignatureAlgorithm := signatureAlgorithm(data[extensionSupportedSignatureAlgorithmsHeaderSize+(i*2)+1])
		if _, ok := hashAlgorithms()[supportedHashAlgorithm]; ok {
			if _, ok := signatureAlgorithms()[supportedSignatureAlgorithm]; ok {
				e.signatureHashAlgorithms = append(e.signatureHashAlgorithms, signatureHashAlgorithm{
					supportedHashAlgorithm,
					supportedSignatureAlgorithm,
				})
			}
		}
	}

	return nil
}
