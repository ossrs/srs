package dtls

// The change cipher spec protocol exists to signal transitions in
// ciphering strategies.  The protocol consists of a single message,
// which is encrypted and compressed under the current (not the pending)
// connection state.  The message consists of a single byte of value 1.
// https://tools.ietf.org/html/rfc5246#section-7.1
type changeCipherSpec struct {
}

func (c changeCipherSpec) contentType() contentType {
	return contentTypeChangeCipherSpec
}

func (c *changeCipherSpec) Marshal() ([]byte, error) {
	return []byte{0x01}, nil
}

func (c *changeCipherSpec) Unmarshal(data []byte) error {
	if len(data) == 1 && data[0] == 0x01 {
		return nil
	}

	return errInvalidCipherSpec
}
