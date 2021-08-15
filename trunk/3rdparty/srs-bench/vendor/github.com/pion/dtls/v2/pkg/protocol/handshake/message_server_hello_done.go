package handshake

// MessageServerHelloDone is final non-encrypted message from server
// this communicates server has sent all its handshake messages and next
// should be MessageFinished
type MessageServerHelloDone struct {
}

// Type returns the Handshake Type
func (m MessageServerHelloDone) Type() Type {
	return TypeServerHelloDone
}

// Marshal encodes the Handshake
func (m *MessageServerHelloDone) Marshal() ([]byte, error) {
	return []byte{}, nil
}

// Unmarshal populates the message from encoded data
func (m *MessageServerHelloDone) Unmarshal(data []byte) error {
	return nil
}
