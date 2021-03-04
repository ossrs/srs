package dtls

/*
   The definition of HelloVerifyRequest is as follows:

   struct {
     ProtocolVersion server_version;
     opaque cookie<0..2^8-1>;
   } HelloVerifyRequest;

   The HelloVerifyRequest message type is hello_verify_request(3).

   When the client sends its ClientHello message to the server, the server
   MAY respond with a HelloVerifyRequest message.  This message contains
   a stateless cookie generated using the technique of [PHOTURIS].  The
   client MUST retransmit the ClientHello with the cookie added.

   https://tools.ietf.org/html/rfc6347#section-4.2.1
*/
type handshakeMessageHelloVerifyRequest struct {
	version protocolVersion
	cookie  []byte
}

func (h handshakeMessageHelloVerifyRequest) handshakeType() handshakeType {
	return handshakeTypeHelloVerifyRequest
}

func (h *handshakeMessageHelloVerifyRequest) Marshal() ([]byte, error) {
	if len(h.cookie) > 255 {
		return nil, errCookieTooLong
	}

	out := make([]byte, 3+len(h.cookie))
	out[0] = h.version.major
	out[1] = h.version.minor
	out[2] = byte(len(h.cookie))
	copy(out[3:], h.cookie)

	return out, nil
}

func (h *handshakeMessageHelloVerifyRequest) Unmarshal(data []byte) error {
	if len(data) < 3 {
		return errBufferTooSmall
	}
	h.version.major = data[0]
	h.version.minor = data[1]
	cookieLength := data[2]
	if len(data) < (int(cookieLength) + 3) {
		return errBufferTooSmall
	}
	h.cookie = make([]byte, cookieLength)

	copy(h.cookie, data[3:3+cookieLength])
	return nil
}
