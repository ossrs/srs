package dtls

type handshakeMessageCertificate struct {
	certificate [][]byte
}

func (h handshakeMessageCertificate) handshakeType() handshakeType {
	return handshakeTypeCertificate
}

const (
	handshakeMessageCertificateLengthFieldSize = 3
)

func (h *handshakeMessageCertificate) Marshal() ([]byte, error) {
	out := make([]byte, handshakeMessageCertificateLengthFieldSize)

	for _, r := range h.certificate {
		// Certificate Length
		out = append(out, make([]byte, handshakeMessageCertificateLengthFieldSize)...)
		putBigEndianUint24(out[len(out)-handshakeMessageCertificateLengthFieldSize:], uint32(len(r)))

		// Certificate body
		out = append(out, append([]byte{}, r...)...)
	}

	// Total Payload Size
	putBigEndianUint24(out[0:], uint32(len(out[handshakeMessageCertificateLengthFieldSize:])))
	return out, nil
}

func (h *handshakeMessageCertificate) Unmarshal(data []byte) error {
	if len(data) < handshakeMessageCertificateLengthFieldSize {
		return errBufferTooSmall
	}

	if certificateBodyLen := int(bigEndianUint24(data)); certificateBodyLen+handshakeMessageCertificateLengthFieldSize != len(data) {
		return errLengthMismatch
	}

	offset := handshakeMessageCertificateLengthFieldSize
	for offset < len(data) {
		certificateLen := int(bigEndianUint24(data[offset:]))
		offset += handshakeMessageCertificateLengthFieldSize

		if offset+certificateLen > len(data) {
			return errLengthMismatch
		}

		h.certificate = append(h.certificate, append([]byte{}, data[offset:offset+certificateLen]...))
		offset += certificateLen
	}

	return nil
}
