// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import ( //nolint:gci
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/sha1" //nolint:gosec
	"crypto/subtle"
	"encoding/binary"
	"hash"

	"github.com/pion/rtp"
)

type srtpCipherAesCmHmacSha1 struct {
	ProtectionProfile

	srtpSessionSalt []byte
	srtpSessionAuth hash.Hash
	srtpBlock       cipher.Block
	srtpEncrypted   bool

	srtcpSessionSalt []byte
	srtcpSessionAuth hash.Hash
	srtcpBlock       cipher.Block
	srtcpEncrypted   bool

	mki []byte
}

func newSrtpCipherAesCmHmacSha1(profile ProtectionProfile, masterKey, masterSalt, mki []byte, encryptSRTP, encryptSRTCP bool) (*srtpCipherAesCmHmacSha1, error) {
	if profile == ProtectionProfileNullHmacSha1_80 || profile == ProtectionProfileNullHmacSha1_32 {
		encryptSRTP = false
		encryptSRTCP = false
	}

	s := &srtpCipherAesCmHmacSha1{
		ProtectionProfile: profile,
		srtpEncrypted:     encryptSRTP,
		srtcpEncrypted:    encryptSRTCP,
	}

	srtpSessionKey, err := aesCmKeyDerivation(labelSRTPEncryption, masterKey, masterSalt, 0, len(masterKey))
	if err != nil {
		return nil, err
	} else if s.srtpBlock, err = aes.NewCipher(srtpSessionKey); err != nil {
		return nil, err
	}

	srtcpSessionKey, err := aesCmKeyDerivation(labelSRTCPEncryption, masterKey, masterSalt, 0, len(masterKey))
	if err != nil {
		return nil, err
	} else if s.srtcpBlock, err = aes.NewCipher(srtcpSessionKey); err != nil {
		return nil, err
	}

	if s.srtpSessionSalt, err = aesCmKeyDerivation(labelSRTPSalt, masterKey, masterSalt, 0, len(masterSalt)); err != nil {
		return nil, err
	} else if s.srtcpSessionSalt, err = aesCmKeyDerivation(labelSRTCPSalt, masterKey, masterSalt, 0, len(masterSalt)); err != nil {
		return nil, err
	}

	authKeyLen, err := profile.AuthKeyLen()
	if err != nil {
		return nil, err
	}

	srtpSessionAuthTag, err := aesCmKeyDerivation(labelSRTPAuthenticationTag, masterKey, masterSalt, 0, authKeyLen)
	if err != nil {
		return nil, err
	}

	srtcpSessionAuthTag, err := aesCmKeyDerivation(labelSRTCPAuthenticationTag, masterKey, masterSalt, 0, authKeyLen)
	if err != nil {
		return nil, err
	}

	s.srtcpSessionAuth = hmac.New(sha1.New, srtcpSessionAuthTag)
	s.srtpSessionAuth = hmac.New(sha1.New, srtpSessionAuthTag)

	mkiLen := len(mki)
	if mkiLen > 0 {
		s.mki = make([]byte, mkiLen)
		copy(s.mki, mki)
	}

	return s, nil
}

func (s *srtpCipherAesCmHmacSha1) encryptRTP(dst []byte, header *rtp.Header, payload []byte, roc uint32) (ciphertext []byte, err error) {
	// Grow the given buffer to fit the output.
	authTagLen, err := s.AuthTagRTPLen()
	if err != nil {
		return nil, err
	}
	dst = growBufferSize(dst, header.MarshalSize()+len(payload)+len(s.mki)+authTagLen)

	// Copy the header unencrypted.
	n, err := header.MarshalTo(dst)
	if err != nil {
		return nil, err
	}

	// Encrypt the payload
	if s.srtpEncrypted {
		counter := generateCounter(header.SequenceNumber, roc, header.SSRC, s.srtpSessionSalt)
		if err = xorBytesCTR(s.srtpBlock, counter[:], dst[n:], payload); err != nil {
			return nil, err
		}
	} else {
		copy(dst[n:], payload)
	}
	n += len(payload)

	// Generate the auth tag.
	authTag, err := s.generateSrtpAuthTag(dst[:n], roc)
	if err != nil {
		return nil, err
	}

	// Append the MKI (if used)
	if len(s.mki) > 0 {
		copy(dst[n:], s.mki)
		n += len(s.mki)
	}

	// Write the auth tag to the dest.
	copy(dst[n:], authTag)

	return dst, nil
}

func (s *srtpCipherAesCmHmacSha1) decryptRTP(dst, ciphertext []byte, header *rtp.Header, headerLen int, roc uint32) ([]byte, error) {
	// Split the auth tag and the cipher text into two parts.
	authTagLen, err := s.AuthTagRTPLen()
	if err != nil {
		return nil, err
	}

	// Split the auth tag and the cipher text into two parts.
	actualTag := ciphertext[len(ciphertext)-authTagLen:]
	ciphertext = ciphertext[:len(ciphertext)-len(s.mki)-authTagLen]

	// Generate the auth tag we expect to see from the ciphertext.
	expectedTag, err := s.generateSrtpAuthTag(ciphertext, roc)
	if err != nil {
		return nil, err
	}

	// See if the auth tag actually matches.
	// We use a constant time comparison to prevent timing attacks.
	if subtle.ConstantTimeCompare(actualTag, expectedTag) != 1 {
		return nil, ErrFailedToVerifyAuthTag
	}

	// Write the plaintext header to the destination buffer.
	copy(dst, ciphertext[:headerLen])

	// Decrypt the ciphertext for the payload.
	if s.srtpEncrypted {
		counter := generateCounter(header.SequenceNumber, roc, header.SSRC, s.srtpSessionSalt)
		err = xorBytesCTR(
			s.srtpBlock, counter[:], dst[headerLen:], ciphertext[headerLen:],
		)
		if err != nil {
			return nil, err
		}
	} else {
		copy(dst[headerLen:], ciphertext[headerLen:])
	}
	return dst, nil
}

func (s *srtpCipherAesCmHmacSha1) encryptRTCP(dst, decrypted []byte, srtcpIndex uint32, ssrc uint32) ([]byte, error) {
	dst = allocateIfMismatch(dst, decrypted)

	// Encrypt everything after header
	if s.srtcpEncrypted {
		counter := generateCounter(uint16(srtcpIndex&0xffff), srtcpIndex>>16, ssrc, s.srtcpSessionSalt)
		if err := xorBytesCTR(s.srtcpBlock, counter[:], dst[8:], dst[8:]); err != nil {
			return nil, err
		}

		// Add SRTCP Index and set Encryption bit
		dst = append(dst, make([]byte, 4)...)
		binary.BigEndian.PutUint32(dst[len(dst)-4:], srtcpIndex)
		dst[len(dst)-4] |= 0x80
	} else {
		// Copy the decrypted payload as is
		copy(dst[8:], decrypted[8:])

		// Add SRTCP Index with Encryption bit cleared
		dst = append(dst, make([]byte, 4)...)
		binary.BigEndian.PutUint32(dst[len(dst)-4:], srtcpIndex)
	}

	// Generate the authentication tag
	authTag, err := s.generateSrtcpAuthTag(dst)
	if err != nil {
		return nil, err
	}

	// Include the MKI if provided
	if len(s.mki) > 0 {
		dst = append(dst, s.mki...)
	}

	// Append the auth tag at the end of the buffer
	return append(dst, authTag...), nil
}

func (s *srtpCipherAesCmHmacSha1) decryptRTCP(out, encrypted []byte, index, ssrc uint32) ([]byte, error) {
	authTagLen, err := s.AuthTagRTCPLen()
	if err != nil {
		return nil, err
	}
	tailOffset := len(encrypted) - (authTagLen + len(s.mki) + srtcpIndexSize)
	if tailOffset < 8 {
		return nil, errTooShortRTCP
	}
	out = out[0:tailOffset]

	expectedTag, err := s.generateSrtcpAuthTag(encrypted[:len(encrypted)-len(s.mki)-authTagLen])
	if err != nil {
		return nil, err
	}

	actualTag := encrypted[len(encrypted)-authTagLen:]
	if subtle.ConstantTimeCompare(actualTag, expectedTag) != 1 {
		return nil, ErrFailedToVerifyAuthTag
	}

	isEncrypted := encrypted[tailOffset]>>7 != 0
	if isEncrypted {
		counter := generateCounter(uint16(index&0xffff), index>>16, ssrc, s.srtcpSessionSalt)
		err = xorBytesCTR(s.srtcpBlock, counter[:], out[8:], out[8:])
	} else {
		copy(out[8:], encrypted[8:])
	}

	return out, err
}

func (s *srtpCipherAesCmHmacSha1) generateSrtpAuthTag(buf []byte, roc uint32) ([]byte, error) {
	// https://tools.ietf.org/html/rfc3711#section-4.2
	// In the case of SRTP, M SHALL consist of the Authenticated
	// Portion of the packet (as specified in Figure 1) concatenated with
	// the ROC, M = Authenticated Portion || ROC;
	//
	// The pre-defined authentication transform for SRTP is HMAC-SHA1
	// [RFC2104].  With HMAC-SHA1, the SRTP_PREFIX_LENGTH (Figure 3) SHALL
	// be 0.  For SRTP (respectively SRTCP), the HMAC SHALL be applied to
	// the session authentication key and M as specified above, i.e.,
	// HMAC(k_a, M).  The HMAC output SHALL then be truncated to the n_tag
	// left-most bits.
	// - Authenticated portion of the packet is everything BEFORE MKI
	// - k_a is the session message authentication key
	// - n_tag is the bit-length of the output authentication tag
	s.srtpSessionAuth.Reset()

	if _, err := s.srtpSessionAuth.Write(buf); err != nil {
		return nil, err
	}

	// For SRTP only, we need to hash the rollover counter as well.
	rocRaw := [4]byte{}
	binary.BigEndian.PutUint32(rocRaw[:], roc)

	_, err := s.srtpSessionAuth.Write(rocRaw[:])
	if err != nil {
		return nil, err
	}

	// Truncate the hash to the size indicated by the profile
	authTagLen, err := s.AuthTagRTPLen()
	if err != nil {
		return nil, err
	}
	return s.srtpSessionAuth.Sum(nil)[0:authTagLen], nil
}

func (s *srtpCipherAesCmHmacSha1) generateSrtcpAuthTag(buf []byte) ([]byte, error) {
	// https://tools.ietf.org/html/rfc3711#section-4.2
	//
	// The pre-defined authentication transform for SRTP is HMAC-SHA1
	// [RFC2104].  With HMAC-SHA1, the SRTP_PREFIX_LENGTH (Figure 3) SHALL
	// be 0.  For SRTP (respectively SRTCP), the HMAC SHALL be applied to
	// the session authentication key and M as specified above, i.e.,
	// HMAC(k_a, M).  The HMAC output SHALL then be truncated to the n_tag
	// left-most bits.
	// - Authenticated portion of the packet is everything BEFORE MKI
	// - k_a is the session message authentication key
	// - n_tag is the bit-length of the output authentication tag
	s.srtcpSessionAuth.Reset()

	if _, err := s.srtcpSessionAuth.Write(buf); err != nil {
		return nil, err
	}
	authTagLen, err := s.AuthTagRTCPLen()
	if err != nil {
		return nil, err
	}

	return s.srtcpSessionAuth.Sum(nil)[0:authTagLen], nil
}

func (s *srtpCipherAesCmHmacSha1) getRTCPIndex(in []byte) uint32 {
	authTagLen, _ := s.AuthTagRTCPLen()
	tailOffset := len(in) - (authTagLen + srtcpIndexSize + len(s.mki))
	srtcpIndexBuffer := in[tailOffset : tailOffset+srtcpIndexSize]
	return binary.BigEndian.Uint32(srtcpIndexBuffer) &^ (1 << 31)
}

func (s *srtpCipherAesCmHmacSha1) getMKI(in []byte, rtp bool) []byte {
	mkiLen := len(s.mki)
	if mkiLen == 0 {
		return nil
	}

	var authTagLen int
	if rtp {
		authTagLen, _ = s.AuthTagRTPLen()
	} else {
		authTagLen, _ = s.AuthTagRTCPLen()
	}
	tailOffset := len(in) - (authTagLen + mkiLen)
	return in[tailOffset : tailOffset+mkiLen]
}
