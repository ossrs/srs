package dtls

import (
	"encoding/binary"
)

type handshakeMessageCertificateVerify struct {
	hashAlgorithm      hashAlgorithm
	signatureAlgorithm signatureAlgorithm
	signature          []byte
}

const handshakeMessageCertificateVerifyMinLength = 4

func (h handshakeMessageCertificateVerify) handshakeType() handshakeType {
	return handshakeTypeCertificateVerify
}

func (h *handshakeMessageCertificateVerify) Marshal() ([]byte, error) {
	out := make([]byte, 1+1+2+len(h.signature))

	out[0] = byte(h.hashAlgorithm)
	out[1] = byte(h.signatureAlgorithm)
	binary.BigEndian.PutUint16(out[2:], uint16(len(h.signature)))
	copy(out[4:], h.signature)
	return out, nil
}

func (h *handshakeMessageCertificateVerify) Unmarshal(data []byte) error {
	if len(data) < handshakeMessageCertificateVerifyMinLength {
		return errBufferTooSmall
	}

	h.hashAlgorithm = hashAlgorithm(data[0])
	if _, ok := hashAlgorithms()[h.hashAlgorithm]; !ok {
		return errInvalidHashAlgorithm
	}

	h.signatureAlgorithm = signatureAlgorithm(data[1])
	if _, ok := signatureAlgorithms()[h.signatureAlgorithm]; !ok {
		return errInvalidSignatureAlgorithm
	}

	signatureLength := int(binary.BigEndian.Uint16(data[2:]))
	if (signatureLength + 4) != len(data) {
		return errBufferTooSmall
	}

	h.signature = append([]byte{}, data[4:]...)
	return nil
}
