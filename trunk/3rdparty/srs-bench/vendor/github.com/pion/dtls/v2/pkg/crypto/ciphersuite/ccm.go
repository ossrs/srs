// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import (
	"crypto/aes"
	"crypto/rand"
	"encoding/binary"
	"fmt"

	"github.com/pion/dtls/v2/pkg/crypto/ccm"
	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

// CCMTagLen is the length of Authentication Tag
type CCMTagLen int

// CCM Enums
const (
	CCMTagLength8  CCMTagLen = 8
	CCMTagLength   CCMTagLen = 16
	ccmNonceLength           = 12
)

// CCM Provides an API to Encrypt/Decrypt DTLS 1.2 Packets
type CCM struct {
	localCCM, remoteCCM         ccm.CCM
	localWriteIV, remoteWriteIV []byte
	tagLen                      CCMTagLen
}

// NewCCM creates a DTLS GCM Cipher
func NewCCM(tagLen CCMTagLen, localKey, localWriteIV, remoteKey, remoteWriteIV []byte) (*CCM, error) {
	localBlock, err := aes.NewCipher(localKey)
	if err != nil {
		return nil, err
	}
	localCCM, err := ccm.NewCCM(localBlock, int(tagLen), ccmNonceLength)
	if err != nil {
		return nil, err
	}

	remoteBlock, err := aes.NewCipher(remoteKey)
	if err != nil {
		return nil, err
	}
	remoteCCM, err := ccm.NewCCM(remoteBlock, int(tagLen), ccmNonceLength)
	if err != nil {
		return nil, err
	}

	return &CCM{
		localCCM:      localCCM,
		localWriteIV:  localWriteIV,
		remoteCCM:     remoteCCM,
		remoteWriteIV: remoteWriteIV,
		tagLen:        tagLen,
	}, nil
}

// Encrypt encrypt a DTLS RecordLayer message
func (c *CCM) Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error) {
	payload := raw[recordlayer.HeaderSize:]
	raw = raw[:recordlayer.HeaderSize]

	nonce := append(append([]byte{}, c.localWriteIV[:4]...), make([]byte, 8)...)
	if _, err := rand.Read(nonce[4:]); err != nil {
		return nil, err
	}

	additionalData := generateAEADAdditionalData(&pkt.Header, len(payload))
	encryptedPayload := c.localCCM.Seal(nil, nonce, payload, additionalData)

	encryptedPayload = append(nonce[4:], encryptedPayload...)
	raw = append(raw, encryptedPayload...)

	// Update recordLayer size to include explicit nonce
	binary.BigEndian.PutUint16(raw[recordlayer.HeaderSize-2:], uint16(len(raw)-recordlayer.HeaderSize))
	return raw, nil
}

// Decrypt decrypts a DTLS RecordLayer message
func (c *CCM) Decrypt(in []byte) ([]byte, error) {
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

	nonce := append(append([]byte{}, c.remoteWriteIV[:4]...), in[recordlayer.HeaderSize:recordlayer.HeaderSize+8]...)
	out := in[recordlayer.HeaderSize+8:]

	additionalData := generateAEADAdditionalData(&h, len(out)-int(c.tagLen))
	out, err = c.remoteCCM.Open(out[:0], nonce, out, additionalData)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errDecryptPacket, err) //nolint:errorlint
	}
	return append(in[:recordlayer.HeaderSize], out...), nil
}
