// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"

	"github.com/pion/rtp"
)

const (
	rtcpEncryptionFlag = 0x80
)

type srtpCipherAeadAesGcm struct {
	ProtectionProfile

	srtpCipher, srtcpCipher cipher.AEAD

	srtpSessionSalt, srtcpSessionSalt []byte
}

func newSrtpCipherAeadAesGcm(profile ProtectionProfile, masterKey, masterSalt []byte) (*srtpCipherAeadAesGcm, error) {
	s := &srtpCipherAeadAesGcm{ProtectionProfile: profile}

	srtpSessionKey, err := aesCmKeyDerivation(labelSRTPEncryption, masterKey, masterSalt, 0, len(masterKey))
	if err != nil {
		return nil, err
	}

	srtpBlock, err := aes.NewCipher(srtpSessionKey)
	if err != nil {
		return nil, err
	}

	s.srtpCipher, err = cipher.NewGCM(srtpBlock)
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

	s.srtcpCipher, err = cipher.NewGCM(srtcpBlock)
	if err != nil {
		return nil, err
	}

	if s.srtpSessionSalt, err = aesCmKeyDerivation(labelSRTPSalt, masterKey, masterSalt, 0, len(masterSalt)); err != nil {
		return nil, err
	} else if s.srtcpSessionSalt, err = aesCmKeyDerivation(labelSRTCPSalt, masterKey, masterSalt, 0, len(masterSalt)); err != nil {
		return nil, err
	}

	return s, nil
}

func (s *srtpCipherAeadAesGcm) encryptRTP(dst []byte, header *rtp.Header, payload []byte, roc uint32) (ciphertext []byte, err error) {
	// Grow the given buffer to fit the output.
	authTagLen, err := s.aeadAuthTagLen()
	if err != nil {
		return nil, err
	}
	dst = growBufferSize(dst, header.MarshalSize()+len(payload)+authTagLen)

	n, err := header.MarshalTo(dst)
	if err != nil {
		return nil, err
	}

	iv := s.rtpInitializationVector(header, roc)
	s.srtpCipher.Seal(dst[n:n], iv[:], payload, dst[:n])
	return dst, nil
}

func (s *srtpCipherAeadAesGcm) decryptRTP(dst, ciphertext []byte, header *rtp.Header, headerLen int, roc uint32) ([]byte, error) {
	// Grow the given buffer to fit the output.
	authTagLen, err := s.aeadAuthTagLen()
	if err != nil {
		return nil, err
	}
	nDst := len(ciphertext) - authTagLen
	if nDst < 0 {
		// Size of ciphertext is shorter than AEAD auth tag len.
		return nil, errFailedToVerifyAuthTag
	}
	dst = growBufferSize(dst, nDst)

	iv := s.rtpInitializationVector(header, roc)

	if _, err := s.srtpCipher.Open(
		dst[headerLen:headerLen], iv[:], ciphertext[headerLen:], ciphertext[:headerLen],
	); err != nil {
		return nil, err
	}

	copy(dst[:headerLen], ciphertext[:headerLen])
	return dst, nil
}

func (s *srtpCipherAeadAesGcm) encryptRTCP(dst, decrypted []byte, srtcpIndex uint32, ssrc uint32) ([]byte, error) {
	authTagLen, err := s.aeadAuthTagLen()
	if err != nil {
		return nil, err
	}
	aadPos := len(decrypted) + authTagLen
	// Grow the given buffer to fit the output.
	dst = growBufferSize(dst, aadPos+srtcpIndexSize)

	iv := s.rtcpInitializationVector(srtcpIndex, ssrc)
	aad := s.rtcpAdditionalAuthenticatedData(decrypted, srtcpIndex)

	s.srtcpCipher.Seal(dst[8:8], iv[:], decrypted[8:], aad[:])

	copy(dst[:8], decrypted[:8])
	copy(dst[aadPos:aadPos+4], aad[8:12])
	return dst, nil
}

func (s *srtpCipherAeadAesGcm) decryptRTCP(dst, encrypted []byte, srtcpIndex, ssrc uint32) ([]byte, error) {
	aadPos := len(encrypted) - srtcpIndexSize
	// Grow the given buffer to fit the output.
	authTagLen, err := s.aeadAuthTagLen()
	if err != nil {
		return nil, err
	}
	nDst := aadPos - authTagLen
	if nDst < 0 {
		// Size of ciphertext is shorter than AEAD auth tag len.
		return nil, errFailedToVerifyAuthTag
	}
	dst = growBufferSize(dst, nDst)

	iv := s.rtcpInitializationVector(srtcpIndex, ssrc)
	aad := s.rtcpAdditionalAuthenticatedData(encrypted, srtcpIndex)

	if _, err := s.srtcpCipher.Open(dst[8:8], iv[:], encrypted[8:aadPos], aad[:]); err != nil {
		return nil, err
	}

	copy(dst[:8], encrypted[:8])
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
	aad[8] |= rtcpEncryptionFlag

	return aad
}

func (s *srtpCipherAeadAesGcm) getRTCPIndex(in []byte) uint32 {
	return binary.BigEndian.Uint32(in[len(in)-4:]) &^ (rtcpEncryptionFlag << 24)
}
