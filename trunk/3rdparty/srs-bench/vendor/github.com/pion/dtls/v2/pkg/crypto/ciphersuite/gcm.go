package ciphersuite

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"encoding/binary"
	"fmt"

	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

const (
	gcmTagLength   = 16
	gcmNonceLength = 12
)

// GCM Provides an API to Encrypt/Decrypt DTLS 1.2 Packets
type GCM struct {
	localGCM, remoteGCM         cipher.AEAD
	localWriteIV, remoteWriteIV []byte
}

// NewGCM creates a DTLS GCM Cipher
func NewGCM(localKey, localWriteIV, remoteKey, remoteWriteIV []byte) (*GCM, error) {
	localBlock, err := aes.NewCipher(localKey)
	if err != nil {
		return nil, err
	}
	localGCM, err := cipher.NewGCM(localBlock)
	if err != nil {
		return nil, err
	}

	remoteBlock, err := aes.NewCipher(remoteKey)
	if err != nil {
		return nil, err
	}
	remoteGCM, err := cipher.NewGCM(remoteBlock)
	if err != nil {
		return nil, err
	}

	return &GCM{
		localGCM:      localGCM,
		localWriteIV:  localWriteIV,
		remoteGCM:     remoteGCM,
		remoteWriteIV: remoteWriteIV,
	}, nil
}

// Encrypt encrypt a DTLS RecordLayer message
func (g *GCM) Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error) {
	payload := raw[recordlayer.HeaderSize:]
	raw = raw[:recordlayer.HeaderSize]

	nonce := make([]byte, gcmNonceLength)
	copy(nonce, g.localWriteIV[:4])
	if _, err := rand.Read(nonce[4:]); err != nil {
		return nil, err
	}

	additionalData := generateAEADAdditionalData(&pkt.Header, len(payload))
	encryptedPayload := g.localGCM.Seal(nil, nonce, payload, additionalData)
	r := make([]byte, len(raw)+len(nonce[4:])+len(encryptedPayload))
	copy(r, raw)
	copy(r[len(raw):], nonce[4:])
	copy(r[len(raw)+len(nonce[4:]):], encryptedPayload)

	// Update recordLayer size to include explicit nonce
	binary.BigEndian.PutUint16(r[recordlayer.HeaderSize-2:], uint16(len(r)-recordlayer.HeaderSize))
	return r, nil
}

// Decrypt decrypts a DTLS RecordLayer message
func (g *GCM) Decrypt(in []byte) ([]byte, error) {
	var h recordlayer.Header
	err := h.Unmarshal(in)
	switch {
	case err != nil:
		return nil, err
	case h.ContentType == protocol.ContentTypeChangeCipherSpec:
		// Nothing to encrypt with ChangeCipherSpec
		return in, nil
	case len(in) <= (8 + recordlayer.HeaderSize):
		return nil, errNotEnoughRoomForNonce
	}

	nonce := make([]byte, 0, gcmNonceLength)
	nonce = append(append(nonce, g.remoteWriteIV[:4]...), in[recordlayer.HeaderSize:recordlayer.HeaderSize+8]...)
	out := in[recordlayer.HeaderSize+8:]

	additionalData := generateAEADAdditionalData(&h, len(out)-gcmTagLength)
	out, err = g.remoteGCM.Open(out[:0], nonce, out, additionalData)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errDecryptPacket, err)
	}
	return append(in[:recordlayer.HeaderSize], out...), nil
}
