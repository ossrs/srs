package extension

import "encoding/binary"

const (
	renegotiationInfoHeaderSize = 5
)

// RenegotiationInfo allows a Client/Server to
// communicate their renegotation support
//
// https://tools.ietf.org/html/rfc5746
type RenegotiationInfo struct {
	RenegotiatedConnection uint8
}

// TypeValue returns the extension TypeValue
func (r RenegotiationInfo) TypeValue() TypeValue {
	return RenegotiationInfoTypeValue
}

// Marshal encodes the extension
func (r *RenegotiationInfo) Marshal() ([]byte, error) {
	out := make([]byte, renegotiationInfoHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(r.TypeValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(1)) // length
	out[4] = r.RenegotiatedConnection
	return out, nil
}

// Unmarshal populates the extension from encoded data
func (r *RenegotiationInfo) Unmarshal(data []byte) error {
	if len(data) < renegotiationInfoHeaderSize {
		return errBufferTooSmall
	} else if TypeValue(binary.BigEndian.Uint16(data)) != r.TypeValue() {
		return errInvalidExtensionType
	}

	r.RenegotiatedConnection = data[4]

	return nil
}
