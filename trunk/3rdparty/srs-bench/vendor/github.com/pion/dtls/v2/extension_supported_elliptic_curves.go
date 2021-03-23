package dtls

import (
	"encoding/binary"
)

const (
	extensionSupportedGroupsHeaderSize = 6
)

// https://tools.ietf.org/html/rfc8422#section-5.1.1
type extensionSupportedEllipticCurves struct {
	ellipticCurves []namedCurve
}

func (e extensionSupportedEllipticCurves) extensionValue() extensionValue {
	return extensionSupportedEllipticCurvesValue
}

func (e *extensionSupportedEllipticCurves) Marshal() ([]byte, error) {
	out := make([]byte, extensionSupportedGroupsHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(e.extensionValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(2+(len(e.ellipticCurves)*2)))
	binary.BigEndian.PutUint16(out[4:], uint16(len(e.ellipticCurves)*2))

	for _, v := range e.ellipticCurves {
		out = append(out, []byte{0x00, 0x00}...)
		binary.BigEndian.PutUint16(out[len(out)-2:], uint16(v))
	}

	return out, nil
}

func (e *extensionSupportedEllipticCurves) Unmarshal(data []byte) error {
	if len(data) <= extensionSupportedGroupsHeaderSize {
		return errBufferTooSmall
	} else if extensionValue(binary.BigEndian.Uint16(data)) != e.extensionValue() {
		return errInvalidExtensionType
	}

	groupCount := int(binary.BigEndian.Uint16(data[4:]) / 2)
	if extensionSupportedGroupsHeaderSize+(groupCount*2) > len(data) {
		return errLengthMismatch
	}

	for i := 0; i < groupCount; i++ {
		supportedGroupID := namedCurve(binary.BigEndian.Uint16(data[(extensionSupportedGroupsHeaderSize + (i * 2)):]))
		if _, ok := namedCurves()[supportedGroupID]; ok {
			e.ellipticCurves = append(e.ellipticCurves, supportedGroupID)
		}
	}
	return nil
}
