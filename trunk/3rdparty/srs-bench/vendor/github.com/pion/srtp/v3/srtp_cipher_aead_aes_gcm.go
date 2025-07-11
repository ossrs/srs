// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"fmt"

	"github.com/pion/rtp"
)

type srtpCipherAeadAesGcm struct {
	protectionProfileWithArgs

	srtpCipher, srtcpCipher cipher.AEAD

	srtpSessionSalt, srtcpSessionSalt []byte

	mki []byte

	srtpEncrypted, srtcpEncrypted bool
}

func newSrtpCipherAeadAesGcm(
	profile protectionProfileWithArgs,
	masterKey, masterSalt, mki []byte,
	encryptSRTP, encryptSRTCP bool,
) (*srtpCipherAeadAesGcm, error) {
	srtpCipher := &srtpCipherAeadAesGcm{
		protectionProfileWithArgs: profile,
		srtpEncrypted:             encryptSRTP,
		srtcpEncrypted:            encryptSRTCP,
	}

	srtpSessionKey, err := aesCmKeyDerivation(labelSRTPEncryption, masterKey, masterSalt, 0, len(masterKey))
	if err != nil {
		return nil, err
	}

	srtpBlock, err := aes.NewCipher(srtpSessionKey)
	if err != nil {
		return nil, err
	}

	srtpCipher.srtpCipher, err = cipher.NewGCM(srtpBlock)
	if err != nil {
		return nil, err
	}

	srtcpSessionKey, err := aesCmKeyDerivation(labelSRTCPEncryption, masterKey, masterSalt, 0, len(masterKey))
	if err != nil {
		return nil, err
	}

	srtcpBlock, err := aes.NewCipher(srtcpSessionKey)
	if err != nil {
		return nil, err
	}

	srtpCipher.srtcpCipher, err = cipher.NewGCM(srtcpBlock)
	if err != nil {
		return nil, err
	}

	if srtpCipher.srtpSessionSalt, err = aesCmKeyDerivation(
		labelSRTPSalt, masterKey, masterSalt, 0, len(masterSalt),
	); err != nil {
		return nil, err
	} else if srtpCipher.srtcpSessionSalt, err = aesCmKeyDerivation(
		labelSRTCPSalt, masterKey, masterSalt, 0, len(masterSalt),
	); err != nil {
		return nil, err
	}

	mkiLen := len(mki)
	if mkiLen > 0 {
		srtpCipher.mki = make([]byte, mkiLen)
		copy(srtpCipher.mki, mki)
	}

	return srtpCipher, nil
}

func (s *srtpCipherAeadAesGcm) encryptRTP(
	dst []byte,
	header *rtp.Header,
	headerLen int,
	plaintext []byte,
	roc uint32,
	rocInAuthTag bool,
) (ciphertext []byte, err error) {
	payload := plaintext[headerLen:]
	payloadLen := len(payload)

	// Grow the given buffer to fit the output.
	authTagLen, err := s.AEADAuthTagLen()
	if err != nil {
		return nil, err
	}
	authPartLen := header.MarshalSize() + len(payload) + authTagLen
	dstLen := authPartLen + len(s.mki)
	if rocInAuthTag {
		dstLen += 4
	}
	dst = growBufferSize(dst, dstLen)
	sameBuffer := isSameBuffer(dst, plaintext)

	// Copy the header unencrypted.
	if !sameBuffer {
		copy(dst, plaintext[:headerLen])
	}

	iv := s.rtpInitializationVector(header, roc)
	if s.srtpEncrypted {
		s.srtpCipher.Seal(dst[headerLen:headerLen], iv[:], payload, dst[:headerLen])
	} else {
		clearLen := headerLen + payloadLen
		if !sameBuffer {
			copy(dst[headerLen:], payload)
		}
		s.srtpCipher.Seal(dst[clearLen:clearLen], iv[:], nil, dst[:clearLen])
	}

	// Add MKI after the encrypted payload
	if len(s.mki) > 0 {
		copy(dst[authPartLen:], s.mki)
	}

	if rocInAuthTag {
		binary.BigEndian.PutUint32(dst[len(dst)-4:], roc)
	}

	return dst, nil
}

func (s *srtpCipherAeadAesGcm) decryptRTP(
	dst, ciphertext []byte,
	header *rtp.Header,
	headerLen int,
	roc uint32,
	rocInAuthTag bool,
) ([]byte, error) {
	// Grow the given buffer to fit the output.
	authTagLen, err := s.AEADAuthTagLen()
	if err != nil {
		return nil, err
	}
	rocLen := 0
	if rocInAuthTag {
		rocLen = 4
	}
	nDst := len(ciphertext) - authTagLen - len(s.mki) - rocLen
	if nDst < headerLen {
		// Size of ciphertext is shorter than AEAD auth tag len.
		return nil, ErrFailedToVerifyAuthTag
	}
	dst = growBufferSize(dst, nDst)
	sameBuffer := isSameBuffer(dst, ciphertext)

	iv := s.rtpInitializationVector(header, roc)

	nEnd := len(ciphertext) - len(s.mki) - rocLen
	if s.srtpEncrypted {
		if _, err := s.srtpCipher.Open(
			dst[headerLen:headerLen], iv[:], ciphertext[headerLen:nEnd], ciphertext[:headerLen],
		); err != nil {
			return nil, fmt.Errorf("%w: %w", ErrFailedToVerifyAuthTag, err)
		}
	} else {
		nDataEnd := nEnd - authTagLen
		if _, err := s.srtpCipher.Open(
			nil, iv[:], ciphertext[nDataEnd:nEnd], ciphertext[:nDataEnd],
		); err != nil {
			return nil, fmt.Errorf("%w: %w", ErrFailedToVerifyAuthTag, err)
		}
		if !sameBuffer {
			copy(dst[headerLen:], ciphertext[headerLen:nDataEnd])
		}
	}

	// Copy the header unencrypted.
	if !sameBuffer {
		copy(dst[:headerLen], ciphertext[:headerLen])
	}

	return dst, nil
}

func (s *srtpCipherAeadAesGcm) encryptRTCP(dst, decrypted []byte, srtcpIndex uint32, ssrc uint32) ([]byte, error) {
	authTagLen, err := s.AEADAuthTagLen()
	if err != nil {
		return nil, err
	}
	aadPos := len(decrypted) + authTagLen
	// Grow the given buffer to fit the output.
	dst = growBufferSize(dst, aadPos+srtcpIndexSize+len(s.mki))
	sameBuffer := isSameBuffer(dst, decrypted)

	iv := s.rtcpInitializationVector(srtcpIndex, ssrc)
	if s.srtcpEncrypted {
		aad := s.rtcpAdditionalAuthenticatedData(decrypted, srtcpIndex)
		if !sameBuffer {
			// Copy the header unencrypted.
			copy(dst[:srtcpHeaderSize], decrypted[:srtcpHeaderSize])
		}
		// Copy index to the proper place.
		copy(dst[aadPos:aadPos+srtcpIndexSize], aad[8:12])
		s.srtcpCipher.Seal(dst[srtcpHeaderSize:srtcpHeaderSize], iv[:], decrypted[srtcpHeaderSize:], aad[:])
	} else {
		// Copy the packet unencrypted.
		if !sameBuffer {
			copy(dst, decrypted)
		}
		// Append the SRTCP index to the end of the packet - this will form the AAD.
		binary.BigEndian.PutUint32(dst[len(decrypted):], srtcpIndex)
		// Generate the authentication tag.
		tag := make([]byte, authTagLen)
		s.srtcpCipher.Seal(tag[0:0], iv[:], nil, dst[:len(decrypted)+srtcpIndexSize])
		// Copy index to the proper place.
		copy(dst[aadPos:], dst[len(decrypted):len(decrypted)+srtcpIndexSize])
		// Copy the auth tag after RTCP payload.
		copy(dst[len(decrypted):], tag)
	}

	copy(dst[aadPos+srtcpIndexSize:], s.mki)

	return dst, nil
}

func (s *srtpCipherAeadAesGcm) decryptRTCP(dst, encrypted []byte, srtcpIndex, ssrc uint32) ([]byte, error) {
	aadPos := len(encrypted) - srtcpIndexSize - len(s.mki)
	// Grow the given buffer to fit the output.
	authTagLen, err := s.AEADAuthTagLen()
	if err != nil {
		return nil, err
	}
	nDst := aadPos - authTagLen
	if nDst < 0 {
		// Size of ciphertext is shorter than AEAD auth tag len.
		return nil, ErrFailedToVerifyAuthTag
	}
	dst = growBufferSize(dst, nDst)
	sameBuffer := isSameBuffer(dst, encrypted)

	isEncrypted := encrypted[aadPos]&srtcpEncryptionFlag != 0
	iv := s.rtcpInitializationVector(srtcpIndex, ssrc)
	if isEncrypted {
		aad := s.rtcpAdditionalAuthenticatedData(encrypted, srtcpIndex)
		if _, err := s.srtcpCipher.Open(dst[srtcpHeaderSize:srtcpHeaderSize], iv[:], encrypted[srtcpHeaderSize:aadPos],
			aad[:]); err != nil {
			return nil, fmt.Errorf("%w: %w", ErrFailedToVerifyAuthTag, err)
		}
	} else {
		// Prepare AAD for received packet.
		dataEnd := aadPos - authTagLen
		aad := make([]byte, dataEnd+4)
		copy(aad, encrypted[:dataEnd])
		copy(aad[dataEnd:], encrypted[aadPos:aadPos+4])
		// Verify the auth tag.
		if _, err := s.srtcpCipher.Open(nil, iv[:], encrypted[dataEnd:aadPos], aad); err != nil {
			return nil, fmt.Errorf("%w: %w", ErrFailedToVerifyAuthTag, err)
		}
		// Copy the unencrypted payload.
		if !sameBuffer {
			copy(dst[srtcpHeaderSize:], encrypted[srtcpHeaderSize:dataEnd])
		}
	}

	// Copy the header unencrypted.
	if !sameBuffer {
		copy(dst[:srtcpHeaderSize], encrypted[:srtcpHeaderSize])
	}

	return dst, nil
}

// The 12-octet IV used by AES-GCM SRTP is formed by first concatenating
// 2 octets of zeroes, the 4-octet SSRC, the 4-octet rollover counter
// (ROC), and the 2-octet sequence number (SEQ).  The resulting 12-octet
// value is then XORed to the 12-octet salt to form the 12-octet IV.
//
// https://tools.ietf.org/html/rfc7714#section-8.1
func (s *srtpCipherAeadAesGcm) rtpInitializationVector(header *rtp.Header, roc uint32) [12]byte {
	var iv [12]byte
	binary.BigEndian.PutUint32(iv[2:], header.SSRC)
	binary.BigEndian.PutUint32(iv[6:], roc)
	binary.BigEndian.PutUint16(iv[10:], header.SequenceNumber)

	for i := range iv {
		iv[i] ^= s.srtpSessionSalt[i]
	}

	return iv
}

// The 12-octet IV used by AES-GCM SRTCP is formed by first
// concatenating 2 octets of zeroes, the 4-octet SSRC identifier,
// 2 octets of zeroes, a single "0" bit, and the 31-bit SRTCP index.
// The resulting 12-octet value is then XORed to the 12-octet salt to
// form the 12-octet IV.
//
// https://tools.ietf.org/html/rfc7714#section-9.1
func (s *srtpCipherAeadAesGcm) rtcpInitializationVector(srtcpIndex uint32, ssrc uint32) [12]byte {
	var iv [12]byte

	binary.BigEndian.PutUint32(iv[2:], ssrc)
	binary.BigEndian.PutUint32(iv[8:], srtcpIndex)

	for i := range iv {
		iv[i] ^= s.srtcpSessionSalt[i]
	}

	return iv
}

// In an SRTCP packet, a 1-bit Encryption flag is prepended to the
// 31-bit SRTCP index to form a 32-bit value we shall call the
// "ESRTCP word"
//
// https://tools.ietf.org/html/rfc7714#section-17
func (s *srtpCipherAeadAesGcm) rtcpAdditionalAuthenticatedData(rtcpPacket []byte, srtcpIndex uint32) [12]byte {
	var aad [12]byte

	copy(aad[:], rtcpPacket[:8])
	binary.BigEndian.PutUint32(aad[8:], srtcpIndex)
	aad[8] |= srtcpEncryptionFlag

	return aad
}

func (s *srtpCipherAeadAesGcm) getRTCPIndex(in []byte) uint32 {
	return binary.BigEndian.Uint32(in[len(in)-len(s.mki)-srtcpIndexSize:]) &^ (srtcpEncryptionFlag << 24)
}
