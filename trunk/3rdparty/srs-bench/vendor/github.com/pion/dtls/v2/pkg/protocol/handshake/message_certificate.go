package handshake

import (
	"github.com/pion/dtls/v2/internal/util"
)

// MessageCertificate is a DTLS Handshake Message
// it can contain either a Client or Server Certificate
//
// https://tools.ietf.org/html/rfc5246#section-7.4.2
type MessageCertificate struct {
	Certificate [][]byte
}

// Type returns the Handshake Type
func (m MessageCertificate) Type() Type {
	return TypeCertificate
}

const (
	handshakeMessageCertificateLengthFieldSize = 3
)

// Marshal encodes the Handshake
func (m *MessageCertificate) Marshal() ([]byte, error) {
	out := make([]byte, handshakeMessageCertificateLengthFieldSize)

	for _, r := range m.Certificate {
		// Certificate Length
		out = append(out, make([]byte, handshakeMessageCertificateLengthFieldSize)...)
		util.PutBigEndianUint24(out[len(out)-handshakeMessageCertificateLengthFieldSize:], uint32(len(r)))

		// Certificate body
		out = append(out, append([]byte{}, r...)...)
	}

	// Total Payload Size
	util.PutBigEndianUint24(out[0:], uint32(len(out[handshakeMessageCertificateLengthFieldSize:])))
	return out, nil
}

// Unmarshal populates the message from encoded data
func (m *MessageCertificate) Unmarshal(data []byte) error {
	if len(data) < handshakeMessageCertificateLengthFieldSize {
		return errBufferTooSmall
	}

	if certificateBodyLen := int(util.BigEndianUint24(data)); certificateBodyLen+handshakeMessageCertificateLengthFieldSize != len(data) {
		return errLengthMismatch
	}

	offset := handshakeMessageCertificateLengthFieldSize
	for offset < len(data) {
		certificateLen := int(util.BigEndianUint24(data[offset:]))
		offset += handshakeMessageCertificateLengthFieldSize

		if offset+certificateLen > len(data) {
			return errLengthMismatch
		}

		m.Certificate = append(m.Certificate, append([]byte{}, data[offset:offset+certificateLen]...))
		offset += certificateLen
	}

	return nil
}
