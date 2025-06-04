// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"encoding/binary"
	"fmt"

	"github.com/pion/rtcp"
)

const maxSRTCPIndex = 0x7FFFFFFF

const srtcpHeaderSize = 8

func (c *Context) decryptRTCP(dst, encrypted []byte) ([]byte, error) {
	authTagLen, err := c.cipher.AuthTagRTCPLen()
	if err != nil {
		return nil, err
	}
	aeadAuthTagLen, err := c.cipher.AEADAuthTagLen()
	if err != nil {
		return nil, err
	}
	mkiLen := len(c.sendMKI)

	// Verify that encrypted packet is long enough
	if len(encrypted) < (srtcpHeaderSize + aeadAuthTagLen + srtcpIndexSize + mkiLen + authTagLen) {
		return nil, fmt.Errorf("%w: %d", errTooShortRTCP, len(encrypted))
	}

	index := c.cipher.getRTCPIndex(encrypted)
	ssrc := binary.BigEndian.Uint32(encrypted[4:])

	s := c.getSRTCPSSRCState(ssrc)
	markAsValid, ok := s.replayDetector.Check(uint64(index))
	if !ok {
		return nil, &duplicatedError{Proto: "srtcp", SSRC: ssrc, Index: index}
	}

	cipher := c.cipher
	if len(c.mkis) > 0 {
		// Find cipher for MKI
		actualMKI := c.cipher.getMKI(encrypted, false)
		cipher, ok = c.mkis[string(actualMKI)]
		if !ok {
			return nil, ErrMKINotFound
		}
	}

	out := allocateIfMismatch(dst, encrypted)

	out, err = cipher.decryptRTCP(out, encrypted, index, ssrc)
	if err != nil {
		return nil, err
	}

	markAsValid()
	return out, nil
}

// DecryptRTCP decrypts a buffer that contains a RTCP packet
func (c *Context) DecryptRTCP(dst, encrypted []byte, header *rtcp.Header) ([]byte, error) {
	if header == nil {
		header = &rtcp.Header{}
	}

	if err := header.Unmarshal(encrypted); err != nil {
		return nil, err
	}

	return c.decryptRTCP(dst, encrypted)
}

func (c *Context) encryptRTCP(dst, decrypted []byte) ([]byte, error) {
	if len(decrypted) < srtcpHeaderSize {
		return nil, fmt.Errorf("%w: %d", errTooShortRTCP, len(decrypted))
	}

	ssrc := binary.BigEndian.Uint32(decrypted[4:])
	s := c.getSRTCPSSRCState(ssrc)

	if s.srtcpIndex >= maxSRTCPIndex {
		// ... when 2^48 SRTP packets or 2^31 SRTCP packets have been secured with the same key
		// (whichever occurs before), the key management MUST be called to provide new master key(s)
		// (previously stored and used keys MUST NOT be used again), or the session MUST be terminated.
		// https://www.rfc-editor.org/rfc/rfc3711#section-9.2
		return nil, errExceededMaxPackets
	}

	// We roll over early because MSB is used for marking as encrypted
	s.srtcpIndex++

	return c.cipher.encryptRTCP(dst, decrypted, s.srtcpIndex, ssrc)
}

// EncryptRTCP Encrypts a RTCP packet
func (c *Context) EncryptRTCP(dst, decrypted []byte, header *rtcp.Header) ([]byte, error) {
	if header == nil {
		header = &rtcp.Header{}
	}

	if err := header.Unmarshal(decrypted); err != nil {
		return nil, err
	}

	return c.encryptRTCP(dst, decrypted)
}
