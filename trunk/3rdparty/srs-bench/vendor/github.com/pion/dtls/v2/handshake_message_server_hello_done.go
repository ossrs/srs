package dtls

type handshakeMessageServerHelloDone struct {
}

func (h handshakeMessageServerHelloDone) handshakeType() handshakeType {
	return handshakeTypeServerHelloDone
}

func (h *handshakeMessageServerHelloDone) Marshal() ([]byte, error) {
	return []byte{}, nil
}

func (h *handshakeMessageServerHelloDone) Unmarshal(data []byte) error {
	return nil
}
