package dtls

import (
	"encoding/binary"
)

/*
A non-anonymous server can optionally request a certificate from
the client, if appropriate for the selected cipher suite.  This
message, if sent, will immediately follow the ServerKeyExchange
message (if it is sent; otherwise, this message follows the
server's Certificate message).
*/

type handshakeMessageCertificateRequest struct {
	certificateTypes        []clientCertificateType
	signatureHashAlgorithms []signatureHashAlgorithm
}

const (
	handshakeMessageCertificateRequestMinLength = 5
)

func (h handshakeMessageCertificateRequest) handshakeType() handshakeType {
	return handshakeTypeCertificateRequest
}

func (h *handshakeMessageCertificateRequest) Marshal() ([]byte, error) {
	out := []byte{byte(len(h.certificateTypes))}
	for _, v := range h.certificateTypes {
		out = append(out, byte(v))
	}

	out = append(out, []byte{0x00, 0x00}...)
	binary.BigEndian.PutUint16(out[len(out)-2:], uint16(len(h.signatureHashAlgorithms)*2))
	for _, v := range h.signatureHashAlgorithms {
		out = append(out, byte(v.hash))
		out = append(out, byte(v.signature))
	}

	out = append(out, []byte{0x00, 0x00}...) // Distinguished Names Length
	return out, nil
}

func (h *handshakeMessageCertificateRequest) Unmarshal(data []byte) error {
	if len(data) < handshakeMessageCertificateRequestMinLength {
		return errBufferTooSmall
	}

	offset := 0
	certificateTypesLength := int(data[0])
	offset++

	if (offset + certificateTypesLength) > len(data) {
		return errBufferTooSmall
	}

	for i := 0; i < certificateTypesLength; i++ {
		certType := clientCertificateType(data[offset+i])
		if _, ok := clientCertificateTypes()[certType]; ok {
			h.certificateTypes = append(h.certificateTypes, certType)
		}
	}
	offset += certificateTypesLength
	if len(data) < offset+2 {
		return errBufferTooSmall
	}
	signatureHashAlgorithmsLength := int(binary.BigEndian.Uint16(data[offset:]))
	offset += 2

	if (offset + signatureHashAlgorithmsLength) > len(data) {
		return errBufferTooSmall
	}

	for i := 0; i < signatureHashAlgorithmsLength; i += 2 {
		if len(data) < (offset + i + 2) {
			return errBufferTooSmall
		}
		hash := hashAlgorithm(data[offset+i])
		signature := signatureAlgorithm(data[offset+i+1])

		if _, ok := hashAlgorithms()[hash]; !ok {
			continue
		} else if _, ok := signatureAlgorithms()[signature]; !ok {
			continue
		}
		h.signatureHashAlgorithms = append(h.signatureHashAlgorithms, signatureHashAlgorithm{signature: signature, hash: hash})
	}

	return nil
}
