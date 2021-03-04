package dtls

import (
	"encoding/binary"
)

/*
When a client first connects to a server it is required to send
the client hello as its first message.  The client can also send a
client hello in response to a hello request or on its own
initiative in order to renegotiate the security parameters in an
existing connection.
*/
type handshakeMessageClientHello struct {
	version protocolVersion
	random  handshakeRandom
	cookie  []byte

	cipherSuites       []cipherSuite
	compressionMethods []*compressionMethod
	extensions         []extension
}

const handshakeMessageClientHelloVariableWidthStart = 34

func (h handshakeMessageClientHello) handshakeType() handshakeType {
	return handshakeTypeClientHello
}

func (h *handshakeMessageClientHello) Marshal() ([]byte, error) {
	if len(h.cookie) > 255 {
		return nil, errCookieTooLong
	}

	out := make([]byte, handshakeMessageClientHelloVariableWidthStart)
	out[0] = h.version.major
	out[1] = h.version.minor

	rand := h.random.marshalFixed()
	copy(out[2:], rand[:])

	out = append(out, 0x00) // SessionID

	out = append(out, byte(len(h.cookie)))
	out = append(out, h.cookie...)
	out = append(out, encodeCipherSuites(h.cipherSuites)...)
	out = append(out, encodeCompressionMethods(h.compressionMethods)...)

	extensions, err := encodeExtensions(h.extensions)
	if err != nil {
		return nil, err
	}

	return append(out, extensions...), nil
}

func (h *handshakeMessageClientHello) Unmarshal(data []byte) error {
	if len(data) < 2+handshakeRandomLength {
		return errBufferTooSmall
	}

	h.version.major = data[0]
	h.version.minor = data[1]

	var random [handshakeRandomLength]byte
	copy(random[:], data[2:])
	h.random.unmarshalFixed(random)

	// rest of packet has variable width sections
	currOffset := handshakeMessageClientHelloVariableWidthStart
	currOffset += int(data[currOffset]) + 1 // SessionID

	currOffset++
	if len(data) <= currOffset {
		return errBufferTooSmall
	}
	n := int(data[currOffset-1])
	if len(data) <= currOffset+n {
		return errBufferTooSmall
	}
	h.cookie = append([]byte{}, data[currOffset:currOffset+n]...)
	currOffset += len(h.cookie)

	// Cipher Suites
	if len(data) < currOffset {
		return errBufferTooSmall
	}
	cipherSuites, err := decodeCipherSuites(data[currOffset:])
	if err != nil {
		return err
	}
	h.cipherSuites = cipherSuites
	if len(data) < currOffset+2 {
		return errBufferTooSmall
	}
	currOffset += int(binary.BigEndian.Uint16(data[currOffset:])) + 2

	// Compression Methods
	if len(data) < currOffset {
		return errBufferTooSmall
	}
	compressionMethods, err := decodeCompressionMethods(data[currOffset:])
	if err != nil {
		return err
	}
	h.compressionMethods = compressionMethods
	if len(data) < currOffset {
		return errBufferTooSmall
	}
	currOffset += int(data[currOffset]) + 1

	// Extensions
	extensions, err := decodeExtensions(data[currOffset:])
	if err != nil {
		return err
	}
	h.extensions = extensions
	return nil
}
