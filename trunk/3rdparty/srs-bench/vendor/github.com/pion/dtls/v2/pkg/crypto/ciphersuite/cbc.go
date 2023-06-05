// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import ( //nolint:gci
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rand"
	"encoding/binary"
	"hash"

	"github.com/pion/dtls/v2/internal/util"
	"github.com/pion/dtls/v2/pkg/crypto/prf"
	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

// block ciphers using cipher block chaining.
type cbcMode interface {
	cipher.BlockMode
	SetIV([]byte)
}

// CBC Provides an API to Encrypt/Decrypt DTLS 1.2 Packets
type CBC struct {
	writeCBC, readCBC cbcMode
	writeMac, readMac []byte
	h                 prf.HashFunc
}

// NewCBC creates a DTLS CBC Cipher
func NewCBC(localKey, localWriteIV, localMac, remoteKey, remoteWriteIV, remoteMac []byte, h prf.HashFunc) (*CBC, error) {
	writeBlock, err := aes.NewCipher(localKey)
	if err != nil {
		return nil, err
	}

	readBlock, err := aes.NewCipher(remoteKey)
	if err != nil {
		return nil, err
	}

	writeCBC, ok := cipher.NewCBCEncrypter(writeBlock, localWriteIV).(cbcMode)
	if !ok {
		return nil, errFailedToCast
	}

	readCBC, ok := cipher.NewCBCDecrypter(readBlock, remoteWriteIV).(cbcMode)
	if !ok {
		return nil, errFailedToCast
	}

	return &CBC{
		writeCBC: writeCBC,
		writeMac: localMac,

		readCBC: readCBC,
		readMac: remoteMac,
		h:       h,
	}, nil
}

// Encrypt encrypt a DTLS RecordLayer message
func (c *CBC) Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error) {
	payload := raw[recordlayer.HeaderSize:]
	raw = raw[:recordlayer.HeaderSize]
	blockSize := c.writeCBC.BlockSize()

	// Generate + Append MAC
	h := pkt.Header

	MAC, err := c.hmac(h.Epoch, h.SequenceNumber, h.ContentType, h.Version, payload, c.writeMac, c.h)
	if err != nil {
		return nil, err
	}
	payload = append(payload, MAC...)

	// Generate + Append padding
	padding := make([]byte, blockSize-len(payload)%blockSize)
	paddingLen := len(padding)
	for i := 0; i < paddingLen; i++ {
		padding[i] = byte(paddingLen - 1)
	}
	payload = append(payload, padding...)

	// Generate IV
	iv := make([]byte, blockSize)
	if _, err := rand.Read(iv); err != nil {
		return nil, err
	}

	// Set IV + Encrypt + Prepend IV
	c.writeCBC.SetIV(iv)
	c.writeCBC.CryptBlocks(payload, payload)
	payload = append(iv, payload...)

	// Prepend unencrypte header with encrypted payload
	raw = append(raw, payload...)

	// Update recordLayer size to include IV+MAC+Padding
	binary.BigEndian.PutUint16(raw[recordlayer.HeaderSize-2:], uint16(len(raw)-recordlayer.HeaderSize))

	return raw, nil
}

// Decrypt decrypts a DTLS RecordLayer message
func (c *CBC) Decrypt(in []byte) ([]byte, error) {
	body := in[recordlayer.HeaderSize:]
	blockSize := c.readCBC.BlockSize()
	mac := c.h()

	var h recordlayer.Header
	err := h.Unmarshal(in)
	switch {
	case err != nil:
		return nil, err
	case h.ContentType == protocol.ContentTypeChangeCipherSpec:
		// Nothing to encrypt with ChangeCipherSpec
		return in, nil
	case len(body)%blockSize != 0 || len(body) < blockSize+util.Max(mac.Size()+1, blockSize):
		return nil, errNotEnoughRoomForNonce
	}

	// Set + remove per record IV
	c.readCBC.SetIV(body[:blockSize])
	body = body[blockSize:]

	// Decrypt
	c.readCBC.CryptBlocks(body, body)

	// Padding+MAC needs to be checked in constant time
	// Otherwise we reveal information about the level of correctness
	paddingLen, paddingGood := examinePadding(body)
	if paddingGood != 255 {
		return nil, errInvalidMAC
	}

	macSize := mac.Size()
	if len(body) < macSize {
		return nil, errInvalidMAC
	}

	dataEnd := len(body) - macSize - paddingLen

	expectedMAC := body[dataEnd : dataEnd+macSize]
	actualMAC, err := c.hmac(h.Epoch, h.SequenceNumber, h.ContentType, h.Version, body[:dataEnd], c.readMac, c.h)

	// Compute Local MAC and compare
	if err != nil || !hmac.Equal(actualMAC, expectedMAC) {
		return nil, errInvalidMAC
	}

	return append(in[:recordlayer.HeaderSize], body[:dataEnd]...), nil
}

func (c *CBC) hmac(epoch uint16, sequenceNumber uint64, contentType protocol.ContentType, protocolVersion protocol.Version, payload []byte, key []byte, hf func() hash.Hash) ([]byte, error) {
	h := hmac.New(hf, key)

	msg := make([]byte, 13)

	binary.BigEndian.PutUint16(msg, epoch)
	util.PutBigEndianUint48(msg[2:], sequenceNumber)
	msg[8] = byte(contentType)
	msg[9] = protocolVersion.Major
	msg[10] = protocolVersion.Minor
	binary.BigEndian.PutUint16(msg[11:], uint16(len(payload)))

	if _, err := h.Write(msg); err != nil {
		return nil, err
	} else if _, err := h.Write(payload); err != nil {
		return nil, err
	}

	return h.Sum(nil), nil
}
