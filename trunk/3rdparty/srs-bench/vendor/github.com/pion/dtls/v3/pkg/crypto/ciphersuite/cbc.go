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

	"github.com/pion/dtls/v3/internal/util"
	"github.com/pion/dtls/v3/pkg/crypto/prf"
	"github.com/pion/dtls/v3/pkg/protocol"
	"github.com/pion/dtls/v3/pkg/protocol/recordlayer"
	"golang.org/x/crypto/cryptobyte"
)

// block ciphers using cipher block chaining.
type cbcMode interface {
	cipher.BlockMode
	SetIV([]byte)
}

// CBC Provides an API to Encrypt/Decrypt DTLS 1.2 Packets.
type CBC struct {
	writeCBC, readCBC cbcMode
	writeMac, readMac []byte
	h                 prf.HashFunc
}

// NewCBC creates a DTLS CBC Cipher.
func NewCBC(
	localKey, localWriteIV, localMac, remoteKey, remoteWriteIV, remoteMac []byte,
	hashFunc prf.HashFunc,
) (*CBC, error) {
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
		h:       hashFunc,
	}, nil
}

// Encrypt encrypt a DTLS RecordLayer message.
func (c *CBC) Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error) {
	payload := raw[pkt.Header.Size():]
	raw = raw[:pkt.Header.Size()]
	blockSize := c.writeCBC.BlockSize()

	// Generate + Append MAC
	h := pkt.Header

	var err error
	var mac []byte
	if h.ContentType == protocol.ContentTypeConnectionID {
		mac, err = c.hmacCID(h.Epoch, h.SequenceNumber, h.Version, payload, c.writeMac, c.h, h.ConnectionID)
	} else {
		mac, err = c.hmac(h.Epoch, h.SequenceNumber, h.ContentType, h.Version, payload, c.writeMac, c.h)
	}
	if err != nil {
		return nil, err
	}
	payload = append(payload, mac...)

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
	payload = append(iv, payload...) //nolint:makezero // todo: FIX

	// Prepend unencrypted header with encrypted payload
	raw = append(raw, payload...)

	// Update recordLayer size to include IV+MAC+Padding
	binary.BigEndian.PutUint16(raw[pkt.Header.Size()-2:], uint16(len(raw)-pkt.Header.Size())) //nolint:gosec //G115

	return raw, nil
}

// Decrypt decrypts a DTLS RecordLayer message.
func (c *CBC) Decrypt(header recordlayer.Header, in []byte) ([]byte, error) {
	blockSize := c.readCBC.BlockSize()
	mac := c.h()

	if err := header.Unmarshal(in); err != nil {
		return nil, err
	}
	body := in[header.Size():]

	switch {
	case header.ContentType == protocol.ContentTypeChangeCipherSpec:
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
	var err error
	var actualMAC []byte
	if header.ContentType == protocol.ContentTypeConnectionID {
		actualMAC, err = c.hmacCID(
			header.Epoch, header.SequenceNumber, header.Version, body[:dataEnd], c.readMac, c.h, header.ConnectionID,
		)
	} else {
		actualMAC, err = c.hmac(
			header.Epoch, header.SequenceNumber, header.ContentType, header.Version, body[:dataEnd], c.readMac, c.h,
		)
	}
	// Compute Local MAC and compare
	if err != nil || !hmac.Equal(actualMAC, expectedMAC) {
		return nil, errInvalidMAC
	}

	return append(in[:header.Size()], body[:dataEnd]...), nil
}

func (c *CBC) hmac(
	epoch uint16,
	sequenceNumber uint64,
	contentType protocol.ContentType,
	protocolVersion protocol.Version,
	payload []byte,
	key []byte,
	hf func() hash.Hash,
) ([]byte, error) {
	hmacHash := hmac.New(hf, key)

	msg := make([]byte, 13)

	binary.BigEndian.PutUint16(msg, epoch)
	util.PutBigEndianUint48(msg[2:], sequenceNumber)
	msg[8] = byte(contentType)
	msg[9] = protocolVersion.Major
	msg[10] = protocolVersion.Minor
	binary.BigEndian.PutUint16(msg[11:], uint16(len(payload))) //nolint:gosec //G115

	if _, err := hmacHash.Write(msg); err != nil {
		return nil, err
	}
	if _, err := hmacHash.Write(payload); err != nil {
		return nil, err
	}

	return hmacHash.Sum(nil), nil
}

// hmacCID calculates a MAC according to
// https://datatracker.ietf.org/doc/html/rfc9146#section-5.1
func (c *CBC) hmacCID(
	epoch uint16,
	sequenceNumber uint64,
	protocolVersion protocol.Version,
	payload []byte,
	key []byte,
	hf func() hash.Hash,
	cid []byte,
) ([]byte, error) {
	// Must unmarshal inner plaintext in orde to perform MAC.
	ip := &recordlayer.InnerPlaintext{}
	if err := ip.Unmarshal(payload); err != nil {
		return nil, err
	}

	hmacHash := hmac.New(hf, key)

	var msg cryptobyte.Builder

	msg.AddUint64(seqNumPlaceholder)
	msg.AddUint8(uint8(protocol.ContentTypeConnectionID))
	msg.AddUint8(uint8(len(cid))) //nolint:gosec //G115
	msg.AddUint8(uint8(protocol.ContentTypeConnectionID))
	msg.AddUint8(protocolVersion.Major)
	msg.AddUint8(protocolVersion.Minor)
	msg.AddUint16(epoch)
	util.AddUint48(&msg, sequenceNumber)
	msg.AddBytes(cid)
	msg.AddUint16(uint16(len(payload))) //nolint:gosec //G115
	msg.AddBytes(ip.Content)
	msg.AddUint8(uint8(ip.RealType))
	msg.AddBytes(make([]byte, ip.Zeros))

	if _, err := hmacHash.Write(msg.BytesOrPanic()); err != nil {
		return nil, err
	}
	if _, err := hmacHash.Write(payload); err != nil {
		return nil, err
	}

	return hmacHash.Sum(nil), nil
}
