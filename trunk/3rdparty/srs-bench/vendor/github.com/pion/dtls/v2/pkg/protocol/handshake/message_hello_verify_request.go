package handshake

import (
	"github.com/pion/dtls/v2/pkg/protocol"
)

// MessageHelloVerifyRequest is as follows:
//
//   struct {
//     ProtocolVersion server_version;
//     opaque cookie<0..2^8-1>;
//   } HelloVerifyRequest;
//
//   The HelloVerifyRequest message type is hello_verify_request(3).
//
//   When the client sends its ClientHello message to the server, the server
//   MAY respond with a HelloVerifyRequest message.  This message contains
//   a stateless cookie generated using the technique of [PHOTURIS].  The
//   client MUST retransmit the ClientHello with the cookie added.
//
//   https://tools.ietf.org/html/rfc6347#section-4.2.1
type MessageHelloVerifyRequest struct {
	Version protocol.Version
	Cookie  []byte
}

// Type returns the Handshake Type
func (m MessageHelloVerifyRequest) Type() Type {
	return TypeHelloVerifyRequest
}

// Marshal encodes the Handshake
func (m *MessageHelloVerifyRequest) Marshal() ([]byte, error) {
	if len(m.Cookie) > 255 {
		return nil, errCookieTooLong
	}

	out := make([]byte, 3+len(m.Cookie))
	out[0] = m.Version.Major
	out[1] = m.Version.Minor
	out[2] = byte(len(m.Cookie))
	copy(out[3:], m.Cookie)

	return out, nil
}

// Unmarshal populates the message from encoded data
func (m *MessageHelloVerifyRequest) Unmarshal(data []byte) error {
	if len(data) < 3 {
		return errBufferTooSmall
	}
	m.Version.Major = data[0]
	m.Version.Minor = data[1]
	cookieLength := data[2]
	if len(data) < (int(cookieLength) + 3) {
		return errBufferTooSmall
	}
	m.Cookie = make([]byte, cookieLength)

	copy(m.Cookie, data[3:3+cookieLength])
	return nil
}
