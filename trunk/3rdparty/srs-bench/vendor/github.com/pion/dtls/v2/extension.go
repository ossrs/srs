package dtls

import (
	"encoding/binary"
)

// https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml
type extensionValue uint16

const (
	extensionServerNameValue                   extensionValue = 0
	extensionSupportedEllipticCurvesValue      extensionValue = 10
	extensionSupportedPointFormatsValue        extensionValue = 11
	extensionSupportedSignatureAlgorithmsValue extensionValue = 13
	extensionUseSRTPValue                      extensionValue = 14
	extensionUseExtendedMasterSecretValue      extensionValue = 23
	extensionRenegotiationInfoValue            extensionValue = 65281
)

type extension interface {
	Marshal() ([]byte, error)
	Unmarshal(data []byte) error

	extensionValue() extensionValue
}

func decodeExtensions(buf []byte) ([]extension, error) {
	if len(buf) < 2 {
		return nil, errBufferTooSmall
	}
	declaredLen := binary.BigEndian.Uint16(buf)
	if len(buf)-2 != int(declaredLen) {
		return nil, errLengthMismatch
	}

	extensions := []extension{}
	unmarshalAndAppend := func(data []byte, e extension) error {
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
		switch extensionValue(binary.BigEndian.Uint16(buf[offset:])) {
		case extensionServerNameValue:
			err = unmarshalAndAppend(buf[offset:], &extensionServerName{})
		case extensionSupportedEllipticCurvesValue:
			err = unmarshalAndAppend(buf[offset:], &extensionSupportedEllipticCurves{})
		case extensionUseSRTPValue:
			err = unmarshalAndAppend(buf[offset:], &extensionUseSRTP{})
		case extensionUseExtendedMasterSecretValue:
			err = unmarshalAndAppend(buf[offset:], &extensionUseExtendedMasterSecret{})
		case extensionRenegotiationInfoValue:
			err = unmarshalAndAppend(buf[offset:], &extensionRenegotiationInfo{})
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

func encodeExtensions(e []extension) ([]byte, error) {
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
