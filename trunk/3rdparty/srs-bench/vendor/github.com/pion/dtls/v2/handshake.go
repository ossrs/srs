package dtls

// https://tools.ietf.org/html/rfc5246#section-7.4
type handshakeType uint8

const (
	handshakeTypeHelloRequest       handshakeType = 0
	handshakeTypeClientHello        handshakeType = 1
	handshakeTypeServerHello        handshakeType = 2
	handshakeTypeHelloVerifyRequest handshakeType = 3
	handshakeTypeCertificate        handshakeType = 11
	handshakeTypeServerKeyExchange  handshakeType = 12
	handshakeTypeCertificateRequest handshakeType = 13
	handshakeTypeServerHelloDone    handshakeType = 14
	handshakeTypeCertificateVerify  handshakeType = 15
	handshakeTypeClientKeyExchange  handshakeType = 16
	handshakeTypeFinished           handshakeType = 20

	// msg_len for Handshake messages assumes an extra 12 bytes for
	// sequence, fragment and version information
	handshakeMessageHeaderLength = 12
)

type handshakeMessage interface {
	Marshal() ([]byte, error)
	Unmarshal(data []byte) error

	handshakeType() handshakeType
}

func (h handshakeType) String() string {
	switch h {
	case handshakeTypeHelloRequest:
		return "HelloRequest"
	case handshakeTypeClientHello:
		return "ClientHello"
	case handshakeTypeServerHello:
		return "ServerHello"
	case handshakeTypeHelloVerifyRequest:
		return "HelloVerifyRequest"
	case handshakeTypeCertificate:
		return "TypeCertificate"
	case handshakeTypeServerKeyExchange:
		return "ServerKeyExchange"
	case handshakeTypeCertificateRequest:
		return "CertificateRequest"
	case handshakeTypeServerHelloDone:
		return "ServerHelloDone"
	case handshakeTypeCertificateVerify:
		return "CertificateVerify"
	case handshakeTypeClientKeyExchange:
		return "ClientKeyExchange"
	case handshakeTypeFinished:
		return "Finished"
	}
	return ""
}

// The handshake protocol is responsible for selecting a cipher spec and
// generating a master secret, which together comprise the primary
// cryptographic parameters associated with a secure session.  The
// handshake protocol can also optionally authenticate parties who have
// certificates signed by a trusted certificate authority.
// https://tools.ietf.org/html/rfc5246#section-7.3
type handshake struct {
	handshakeHeader  handshakeHeader
	handshakeMessage handshakeMessage
}

func (h handshake) contentType() contentType {
	return contentTypeHandshake
}

func (h *handshake) Marshal() ([]byte, error) {
	if h.handshakeMessage == nil {
		return nil, errHandshakeMessageUnset
	} else if h.handshakeHeader.fragmentOffset != 0 {
		return nil, errUnableToMarshalFragmented
	}

	msg, err := h.handshakeMessage.Marshal()
	if err != nil {
		return nil, err
	}

	h.handshakeHeader.length = uint32(len(msg))
	h.handshakeHeader.fragmentLength = h.handshakeHeader.length
	h.handshakeHeader.handshakeType = h.handshakeMessage.handshakeType()
	header, err := h.handshakeHeader.Marshal()
	if err != nil {
		return nil, err
	}

	return append(header, msg...), nil
}

func (h *handshake) Unmarshal(data []byte) error {
	if err := h.handshakeHeader.Unmarshal(data); err != nil {
		return err
	}

	reportedLen := bigEndianUint24(data[1:])
	if uint32(len(data)-handshakeMessageHeaderLength) != reportedLen {
		return errLengthMismatch
	} else if reportedLen != h.handshakeHeader.fragmentLength {
		return errLengthMismatch
	}

	switch handshakeType(data[0]) {
	case handshakeTypeHelloRequest:
		return errNotImplemented
	case handshakeTypeClientHello:
		h.handshakeMessage = &handshakeMessageClientHello{}
	case handshakeTypeHelloVerifyRequest:
		h.handshakeMessage = &handshakeMessageHelloVerifyRequest{}
	case handshakeTypeServerHello:
		h.handshakeMessage = &handshakeMessageServerHello{}
	case handshakeTypeCertificate:
		h.handshakeMessage = &handshakeMessageCertificate{}
	case handshakeTypeServerKeyExchange:
		h.handshakeMessage = &handshakeMessageServerKeyExchange{}
	case handshakeTypeCertificateRequest:
		h.handshakeMessage = &handshakeMessageCertificateRequest{}
	case handshakeTypeServerHelloDone:
		h.handshakeMessage = &handshakeMessageServerHelloDone{}
	case handshakeTypeClientKeyExchange:
		h.handshakeMessage = &handshakeMessageClientKeyExchange{}
	case handshakeTypeFinished:
		h.handshakeMessage = &handshakeMessageFinished{}
	case handshakeTypeCertificateVerify:
		h.handshakeMessage = &handshakeMessageCertificateVerify{}
	default:
		return errNotImplemented
	}
	return h.handshakeMessage.Unmarshal(data[handshakeMessageHeaderLength:])
}
