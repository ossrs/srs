package dtls

import "encoding/binary"

const (
	extensionRenegotiationInfoHeaderSize = 5
)

// https://tools.ietf.org/html/rfc5746
type extensionRenegotiationInfo struct {
	renegotiatedConnection uint8
}

func (e extensionRenegotiationInfo) extensionValue() extensionValue {
	return extensionRenegotiationInfoValue
}

func (e *extensionRenegotiationInfo) Marshal() ([]byte, error) {
	out := make([]byte, extensionRenegotiationInfoHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(e.extensionValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(1)) // length
	out[4] = e.renegotiatedConnection
	return out, nil
}

func (e *extensionRenegotiationInfo) Unmarshal(data []byte) error {
	if len(data) < extensionRenegotiationInfoHeaderSize {
		return errBufferTooSmall
	} else if extensionValue(binary.BigEndian.Uint16(data)) != e.extensionValue() {
		return errInvalidExtensionType
	}

	e.renegotiatedConnection = data[4]

	return nil
}
