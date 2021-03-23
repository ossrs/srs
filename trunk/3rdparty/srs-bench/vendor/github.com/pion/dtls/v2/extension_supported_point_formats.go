package dtls

import "encoding/binary"

const (
	extensionSupportedPointFormatsSize = 5
)

type ellipticCurvePointFormat byte

const ellipticCurvePointFormatUncompressed ellipticCurvePointFormat = 0

// https://tools.ietf.org/html/rfc4492#section-5.1.2
type extensionSupportedPointFormats struct {
	pointFormats []ellipticCurvePointFormat
}

func (e extensionSupportedPointFormats) extensionValue() extensionValue {
	return extensionSupportedPointFormatsValue
}

func (e *extensionSupportedPointFormats) Marshal() ([]byte, error) {
	out := make([]byte, extensionSupportedPointFormatsSize)

	binary.BigEndian.PutUint16(out, uint16(e.extensionValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(1+(len(e.pointFormats))))
	out[4] = byte(len(e.pointFormats))

	for _, v := range e.pointFormats {
		out = append(out, byte(v))
	}
	return out, nil
}

func (e *extensionSupportedPointFormats) Unmarshal(data []byte) error {
	if len(data) <= extensionSupportedPointFormatsSize {
		return errBufferTooSmall
	} else if extensionValue(binary.BigEndian.Uint16(data)) != e.extensionValue() {
		return errInvalidExtensionType
	}

	pointFormatCount := int(binary.BigEndian.Uint16(data[4:]))
	if extensionSupportedGroupsHeaderSize+(pointFormatCount) > len(data) {
		return errLengthMismatch
	}

	for i := 0; i < pointFormatCount; i++ {
		p := ellipticCurvePointFormat(data[extensionSupportedPointFormatsSize+i])
		switch p {
		case ellipticCurvePointFormatUncompressed:
			e.pointFormats = append(e.pointFormats, p)
		default:
		}
	}
	return nil
}
