package dtls

import (
	"encoding/binary"
)

// Structure supports ECDH and PSK
type handshakeMessageServerKeyExchange struct {
	identityHint []byte

	ellipticCurveType  ellipticCurveType
	namedCurve         namedCurve
	publicKey          []byte
	hashAlgorithm      hashAlgorithm
	signatureAlgorithm signatureAlgorithm
	signature          []byte
}

func (h handshakeMessageServerKeyExchange) handshakeType() handshakeType {
	return handshakeTypeServerKeyExchange
}

func (h *handshakeMessageServerKeyExchange) Marshal() ([]byte, error) {
	if h.identityHint != nil {
		out := append([]byte{0x00, 0x00}, h.identityHint...)
		binary.BigEndian.PutUint16(out, uint16(len(out)-2))
		return out, nil
	}

	out := []byte{byte(h.ellipticCurveType), 0x00, 0x00}
	binary.BigEndian.PutUint16(out[1:], uint16(h.namedCurve))

	out = append(out, byte(len(h.publicKey)))
	out = append(out, h.publicKey...)

	out = append(out, []byte{byte(h.hashAlgorithm), byte(h.signatureAlgorithm), 0x00, 0x00}...)

	binary.BigEndian.PutUint16(out[len(out)-2:], uint16(len(h.signature)))
	out = append(out, h.signature...)

	return out, nil
}

func (h *handshakeMessageServerKeyExchange) Unmarshal(data []byte) error {
	if len(data) < 2 {
		return errBufferTooSmall
	}

	// If parsed as PSK return early and only populate PSK Identity Hint
	if pskLength := binary.BigEndian.Uint16(data); len(data) == int(pskLength+2) {
		h.identityHint = append([]byte{}, data[2:]...)
		return nil
	}

	if _, ok := ellipticCurveTypes()[ellipticCurveType(data[0])]; ok {
		h.ellipticCurveType = ellipticCurveType(data[0])
	} else {
		return errInvalidEllipticCurveType
	}

	if len(data[1:]) < 2 {
		return errBufferTooSmall
	}
	h.namedCurve = namedCurve(binary.BigEndian.Uint16(data[1:3]))
	if _, ok := namedCurves()[h.namedCurve]; !ok {
		return errInvalidNamedCurve
	}
	if len(data) < 4 {
		return errBufferTooSmall
	}

	publicKeyLength := int(data[3])
	offset := 4 + publicKeyLength
	if len(data) < offset {
		return errBufferTooSmall
	}
	h.publicKey = append([]byte{}, data[4:offset]...)
	if len(data) <= offset {
		return errBufferTooSmall
	}
	h.hashAlgorithm = hashAlgorithm(data[offset])
	if _, ok := hashAlgorithms()[h.hashAlgorithm]; !ok {
		return errInvalidHashAlgorithm
	}
	offset++
	if len(data) <= offset {
		return errBufferTooSmall
	}
	h.signatureAlgorithm = signatureAlgorithm(data[offset])
	if _, ok := signatureAlgorithms()[h.signatureAlgorithm]; !ok {
		return errInvalidSignatureAlgorithm
	}
	offset++
	if len(data) < offset+2 {
		return errBufferTooSmall
	}
	signatureLength := int(binary.BigEndian.Uint16(data[offset:]))
	offset += 2
	if len(data) < offset+signatureLength {
		return errBufferTooSmall
	}
	h.signature = append([]byte{}, data[offset:offset+signatureLength]...)
	return nil
}
