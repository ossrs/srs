package handshake

// MessageFinished is a DTLS Handshake Message
// this message is the first one protected with the just
// negotiated algorithms, keys, and secrets.  Recipients of Finished
// messages MUST verify that the contents are correct.
//
// https://tools.ietf.org/html/rfc5246#section-7.4.9
type MessageFinished struct {
	VerifyData []byte
}

// Type returns the Handshake Type
func (m MessageFinished) Type() Type {
	return TypeFinished
}

// Marshal encodes the Handshake
func (m *MessageFinished) Marshal() ([]byte, error) {
	return append([]byte{}, m.VerifyData...), nil
}

// Unmarshal populates the message from encoded data
func (m *MessageFinished) Unmarshal(data []byte) error {
	m.VerifyData = append([]byte{}, data...)
	return nil
}
