// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package handshake provides the DTLS wire protocol for handshakes
package handshake

import (
	"github.com/pion/dtls/v2/internal/ciphersuite/types"
	"github.com/pion/dtls/v2/internal/util"
	"github.com/pion/dtls/v2/pkg/protocol"
)

// Type is the unique identifier for each handshake message
// https://tools.ietf.org/html/rfc5246#section-7.4
type Type uint8

// Types of DTLS Handshake messages we know about
const (
	TypeHelloRequest       Type = 0
	TypeClientHello        Type = 1
	TypeServerHello        Type = 2
	TypeHelloVerifyRequest Type = 3
	TypeCertificate        Type = 11
	TypeServerKeyExchange  Type = 12
	TypeCertificateRequest Type = 13
	TypeServerHelloDone    Type = 14
	TypeCertificateVerify  Type = 15
	TypeClientKeyExchange  Type = 16
	TypeFinished           Type = 20
)

// String returns the string representation of this type
func (t Type) String() string {
	switch t {
	case TypeHelloRequest:
		return "HelloRequest"
	case TypeClientHello:
		return "ClientHello"
	case TypeServerHello:
		return "ServerHello"
	case TypeHelloVerifyRequest:
		return "HelloVerifyRequest"
	case TypeCertificate:
		return "TypeCertificate"
	case TypeServerKeyExchange:
		return "ServerKeyExchange"
	case TypeCertificateRequest:
		return "CertificateRequest"
	case TypeServerHelloDone:
		return "ServerHelloDone"
	case TypeCertificateVerify:
		return "CertificateVerify"
	case TypeClientKeyExchange:
		return "ClientKeyExchange"
	case TypeFinished:
		return "Finished"
	}
	return ""
}

// Message is the body of a Handshake datagram
type Message interface {
	Marshal() ([]byte, error)
	Unmarshal(data []byte) error
	Type() Type
}

// Handshake protocol is responsible for selecting a cipher spec and
// generating a master secret, which together comprise the primary
// cryptographic parameters associated with a secure session.  The
// handshake protocol can also optionally authenticate parties who have
// certificates signed by a trusted certificate authority.
// https://tools.ietf.org/html/rfc5246#section-7.3
type Handshake struct {
	Header  Header
	Message Message

	KeyExchangeAlgorithm types.KeyExchangeAlgorithm
}

// ContentType returns what kind of content this message is carying
func (h Handshake) ContentType() protocol.ContentType {
	return protocol.ContentTypeHandshake
}

// Marshal encodes a handshake into a binary message
func (h *Handshake) Marshal() ([]byte, error) {
	if h.Message == nil {
		return nil, errHandshakeMessageUnset
	} else if h.Header.FragmentOffset != 0 {
		return nil, errUnableToMarshalFragmented
	}

	msg, err := h.Message.Marshal()
	if err != nil {
		return nil, err
	}

	h.Header.Length = uint32(len(msg))
	h.Header.FragmentLength = h.Header.Length
	h.Header.Type = h.Message.Type()
	header, err := h.Header.Marshal()
	if err != nil {
		return nil, err
	}

	return append(header, msg...), nil
}

// Unmarshal decodes a handshake from a binary message
func (h *Handshake) Unmarshal(data []byte) error {
	if err := h.Header.Unmarshal(data); err != nil {
		return err
	}

	reportedLen := util.BigEndianUint24(data[1:])
	if uint32(len(data)-HeaderLength) != reportedLen {
		return errLengthMismatch
	} else if reportedLen != h.Header.FragmentLength {
		return errLengthMismatch
	}

	switch Type(data[0]) {
	case TypeHelloRequest:
		return errNotImplemented
	case TypeClientHello:
		h.Message = &MessageClientHello{}
	case TypeHelloVerifyRequest:
		h.Message = &MessageHelloVerifyRequest{}
	case TypeServerHello:
		h.Message = &MessageServerHello{}
	case TypeCertificate:
		h.Message = &MessageCertificate{}
	case TypeServerKeyExchange:
		h.Message = &MessageServerKeyExchange{KeyExchangeAlgorithm: h.KeyExchangeAlgorithm}
	case TypeCertificateRequest:
		h.Message = &MessageCertificateRequest{}
	case TypeServerHelloDone:
		h.Message = &MessageServerHelloDone{}
	case TypeClientKeyExchange:
		h.Message = &MessageClientKeyExchange{KeyExchangeAlgorithm: h.KeyExchangeAlgorithm}
	case TypeFinished:
		h.Message = &MessageFinished{}
	case TypeCertificateVerify:
		h.Message = &MessageCertificateVerify{}
	default:
		return errNotImplemented
	}
	return h.Message.Unmarshal(data[HeaderLength:])
}
