package dtls

import "encoding/binary"

const (
	extensionUseExtendedMasterSecretHeaderSize = 4
)

// https://tools.ietf.org/html/rfc8422
type extensionUseExtendedMasterSecret struct {
	supported bool
}

func (e extensionUseExtendedMasterSecret) extensionValue() extensionValue {
	return extensionUseExtendedMasterSecretValue
}

func (e *extensionUseExtendedMasterSecret) Marshal() ([]byte, error) {
	if !e.supported {
		return []byte{}, nil
	}

	out := make([]byte, extensionUseExtendedMasterSecretHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(e.extensionValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(0)) // length
	return out, nil
}

func (e *extensionUseExtendedMasterSecret) Unmarshal(data []byte) error {
	if len(data) < extensionUseExtendedMasterSecretHeaderSize {
		return errBufferTooSmall
	} else if extensionValue(binary.BigEndian.Uint16(data)) != e.extensionValue() {
		return errInvalidExtensionType
	}

	e.supported = true

	return nil
}
