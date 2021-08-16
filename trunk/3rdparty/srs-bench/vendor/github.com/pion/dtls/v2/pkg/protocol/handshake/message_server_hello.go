package handshake

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/extension"
)

// MessageServerHello is sent in response to a ClientHello
// message when it was able to find an acceptable set of algorithms.
// If it cannot find such a match, it will respond with a handshake
// failure alert.
//
// https://tools.ietf.org/html/rfc5246#section-7.4.1.3
type MessageServerHello struct {
	Version protocol.Version
	Random  Random

	CipherSuiteID     *uint16
	CompressionMethod *protocol.CompressionMethod
	Extensions        []extension.Extension
}

const messageServerHelloVariableWidthStart = 2 + RandomLength

// Type returns the Handshake Type
func (m MessageServerHello) Type() Type {
	return TypeServerHello
}

// Marshal encodes the Handshake
func (m *MessageServerHello) Marshal() ([]byte, error) {
	if m.CipherSuiteID == nil {
		return nil, errCipherSuiteUnset
	} else if m.CompressionMethod == nil {
		return nil, errCompressionMethodUnset
	}

	out := make([]byte, messageServerHelloVariableWidthStart)
	out[0] = m.Version.Major
	out[1] = m.Version.Minor

	rand := m.Random.MarshalFixed()
	copy(out[2:], rand[:])

	out = append(out, 0x00) // SessionID

	out = append(out, []byte{0x00, 0x00}...)
	binary.BigEndian.PutUint16(out[len(out)-2:], *m.CipherSuiteID)

	out = append(out, byte(m.CompressionMethod.ID))

	extensions, err := extension.Marshal(m.Extensions)
	if err != nil {
		return nil, err
	}

	return append(out, extensions...), nil
}

// Unmarshal populates the message from encoded data
func (m *MessageServerHello) Unmarshal(data []byte) error {
	if len(data) < 2+RandomLength {
		return errBufferTooSmall
	}

	m.Version.Major = data[0]
	m.Version.Minor = data[1]

	var random [RandomLength]byte
	copy(random[:], data[2:])
	m.Random.UnmarshalFixed(random)

	currOffset := messageServerHelloVariableWidthStart
	currOffset += int(data[currOffset]) + 1 // SessionID
	if len(data) < (currOffset + 2) {
		return errBufferTooSmall
	}

	m.CipherSuiteID = new(uint16)
	*m.CipherSuiteID = binary.BigEndian.Uint16(data[currOffset:])
	currOffset += 2

	if len(data) < currOffset {
		return errBufferTooSmall
	}
	if compressionMethod, ok := protocol.CompressionMethods()[protocol.CompressionMethodID(data[currOffset])]; ok {
		m.CompressionMethod = compressionMethod
		currOffset++
	} else {
		return errInvalidCompressionMethod
	}

	if len(data) <= currOffset {
		m.Extensions = []extension.Extension{}
		return nil
	}

	extensions, err := extension.Unmarshal(data[currOffset:])
	if err != nil {
		return err
	}
	m.Extensions = extensions
	return nil
}
