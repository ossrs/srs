// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package srtp implements Secure Real-time Transport Protocol
package srtp

import (
	"fmt"

	"github.com/pion/rtp"
)

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

	// Verify that encrypted packet is long enough
	if len(ciphertext) < (headerLen + aeadAuthTagLen + mkiLen + authTagLen) {
		return nil, fmt.Errorf("%w: %d", errTooShortRTP, len(ciphertext))
	}

	s := c.getSRTPSSRCState(header.SSRC)

	roc, diff, _ := s.nextRolloverCount(header.SequenceNumber)
	markAsValid, ok := s.replayDetector.Check(
		(uint64(roc) << 16) | uint64(header.SequenceNumber),
	)
	if !ok {
		return nil, &duplicatedError{
			Proto: "srtp", SSRC: header.SSRC, Index: uint32(header.SequenceNumber),
		}
	}

	cipher := c.cipher
	if len(c.mkis) > 0 {
		// Find cipher for MKI
		actualMKI := c.cipher.getMKI(ciphertext, true)
		cipher, ok = c.mkis[string(actualMKI)]
		if !ok {
			return nil, ErrMKINotFound
		}
	}

	dst = growBufferSize(dst, len(ciphertext)-authTagLen-len(c.sendMKI))

	dst, err = cipher.decryptRTP(dst, ciphertext, header, headerLen, roc)
	if err != nil {
		return nil, err
	}

	markAsValid()
	s.updateRolloverCount(header.SequenceNumber, diff)
	return dst, nil
}

// DecryptRTP decrypts a RTP packet with an encrypted payload
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
// If the dst buffer does not have the capacity to hold `len(plaintext) + 10` bytes, a new one will be allocated and returned.
// If a rtp.Header is provided, it will be Unmarshaled using the plaintext.
func (c *Context) EncryptRTP(dst []byte, plaintext []byte, header *rtp.Header) ([]byte, error) {
	if header == nil {
		header = &rtp.Header{}
	}

	headerLen, err := header.Unmarshal(plaintext)
	if err != nil {
		return nil, err
	}

	return c.encryptRTP(dst, header, plaintext[headerLen:])
}

// encryptRTP marshals and encrypts an RTP packet, writing to the dst buffer provided.
// If the dst buffer does not have the capacity, a new one will be allocated and returned.
// Similar to above but faster because it can avoid unmarshaling the header and marshaling the payload.
func (c *Context) encryptRTP(dst []byte, header *rtp.Header, payload []byte) (ciphertext []byte, err error) {
	s := c.getSRTPSSRCState(header.SSRC)
	roc, diff, ovf := s.nextRolloverCount(header.SequenceNumber)
	if ovf {
		// ... when 2^48 SRTP packets or 2^31 SRTCP packets have been secured with the same key
		// (whichever occurs before), the key management MUST be called to provide new master key(s)
		// (previously stored and used keys MUST NOT be used again), or the session MUST be terminated.
		// https://www.rfc-editor.org/rfc/rfc3711#section-9.2
		return nil, errExceededMaxPackets
	}
	s.updateRolloverCount(header.SequenceNumber, diff)

	return c.cipher.encryptRTP(dst, header, payload, roc)
}
