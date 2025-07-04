// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package srtp implements Secure Real-time Transport Protocol
package srtp

import (
	"encoding/binary"
	"fmt"

	"github.com/pion/rtp"
)

/*
Simplified structure of SRTP Packets:
- RTP Header (with optional RTP Header Extension)
- Payload (with optional padding)
- AEAD Auth Tag - used by AEAD profiles only
- MKI (optional)
- Auth Tag - used by non-AEAD profiles only. When RCC is used with AEAD profiles, the ROC is sent here.
*/

func (c *Context) decryptRTP(dst, ciphertext []byte, header *rtp.Header, headerLen int) ([]byte, error) {
	authTagLen, err := c.cipher.AuthTagRTPLen()
	if err != nil {
		return nil, err
	}
	aeadAuthTagLen, err := c.cipher.AEADAuthTagLen()
	if err != nil {
		return nil, err
	}
	mkiLen := len(c.sendMKI)

	var hasRocInPacket bool
	hasRocInPacket, authTagLen = c.hasROCInPacket(header, authTagLen)

	// Verify that encrypted packet is long enough
	if len(ciphertext) < (headerLen + aeadAuthTagLen + mkiLen + authTagLen) {
		return nil, fmt.Errorf("%w: %d", errTooShortRTP, len(ciphertext))
	}

	ssrcState := c.getSRTPSSRCState(header.SSRC)

	var roc uint32
	var diff int64
	var index uint64
	if !hasRocInPacket {
		// The ROC is not sent in the packet. We need to guess it.
		roc, diff, _ = ssrcState.nextRolloverCount(header.SequenceNumber)
		index = (uint64(roc) << 16) | uint64(header.SequenceNumber)
	} else {
		// Extract ROC from the packet. The ROC is sent in the first 4 bytes of the auth tag.
		roc = binary.BigEndian.Uint32(ciphertext[len(ciphertext)-authTagLen:])
		index = (uint64(roc) << 16) | uint64(header.SequenceNumber)
		diff = int64(ssrcState.index) - int64(index) //nolint:gosec
	}

	markAsValid, ok := ssrcState.replayDetector.Check(index)
	if !ok {
		return nil, &duplicatedError{
			Proto: "srtp", SSRC: header.SSRC, Index: uint32(header.SequenceNumber),
		}
	}

	cipher := c.cipher
	if len(c.mkis) > 0 {
		// Find cipher for MKI
		actualMKI := ciphertext[len(ciphertext)-mkiLen-authTagLen : len(ciphertext)-authTagLen]
		cipher, ok = c.mkis[string(actualMKI)]
		if !ok {
			return nil, ErrMKINotFound
		}
	}

	dst = growBufferSize(dst, len(ciphertext)-authTagLen-len(c.sendMKI))

	dst, err = cipher.decryptRTP(dst, ciphertext, header, headerLen, roc, hasRocInPacket)
	if err != nil {
		return nil, err
	}

	markAsValid()
	ssrcState.updateRolloverCount(header.SequenceNumber, diff, hasRocInPacket, roc)

	return dst, nil
}

// DecryptRTP decrypts a RTP packet with an encrypted payload.
func (c *Context) DecryptRTP(dst, encrypted []byte, header *rtp.Header) ([]byte, error) {
	if header == nil {
		header = &rtp.Header{}
	}

	headerLen, err := header.Unmarshal(encrypted)
	if err != nil {
		return nil, err
	}

	return c.decryptRTP(dst, encrypted, header, headerLen)
}

// EncryptRTP marshals and encrypts an RTP packet, writing to the dst buffer provided.
// If the dst buffer does not have the capacity to hold `len(plaintext) + 10` bytes,
// a new one will be allocated and returned.
// If a rtp.Header is provided, it will be Unmarshaled using the plaintext.
func (c *Context) EncryptRTP(dst []byte, plaintext []byte, header *rtp.Header) ([]byte, error) {
	if header == nil {
		header = &rtp.Header{}
	}

	headerLen, err := header.Unmarshal(plaintext)
	if err != nil {
		return nil, err
	}

	return c.encryptRTP(dst, header, headerLen, plaintext)
}

// encryptRTP marshals and encrypts an RTP packet, writing to the dst buffer provided.
// If the dst buffer does not have the capacity, a new one will be allocated and returned.
// Similar to above but faster because it can avoid unmarshaling the header and marshaling the payload.
func (c *Context) encryptRTP(dst []byte, header *rtp.Header, headerLen int, plaintext []byte,
) (ciphertext []byte, err error) {
	s := c.getSRTPSSRCState(header.SSRC)
	roc, diff, ovf := s.nextRolloverCount(header.SequenceNumber)
	if ovf {
		// ... when 2^48 SRTP packets or 2^31 SRTCP packets have been secured with the same key
		// (whichever occurs before), the key management MUST be called to provide new master key(s)
		// (previously stored and used keys MUST NOT be used again), or the session MUST be terminated.
		// https://www.rfc-editor.org/rfc/rfc3711#section-9.2
		return nil, errExceededMaxPackets
	}
	s.updateRolloverCount(header.SequenceNumber, diff, false, 0)

	rocInPacket := false
	if c.rccMode != RCCModeNone && header.SequenceNumber%c.rocTransmitRate == 0 {
		rocInPacket = true
	}

	return c.cipher.encryptRTP(dst, header, headerLen, plaintext, roc, rocInPacket)
}

func (c *Context) hasROCInPacket(header *rtp.Header, authTagLen int) (bool, int) {
	hasRocInPacket := false
	switch c.rccMode {
	case RCCMode2:
		// This mode is supported for AES-CM and NULL profiles only. The ROC is sent in the first 4 bytes of the auth tag.
		hasRocInPacket = header.SequenceNumber%c.rocTransmitRate == 0
	case RCCMode3:
		// This mode is supported for AES-GCM only. The ROC is sent as 4-byte auth tag.
		hasRocInPacket = header.SequenceNumber%c.rocTransmitRate == 0
		if hasRocInPacket {
			authTagLen = 4
		}
	default:
	}

	return hasRocInPacket, authTagLen
}
