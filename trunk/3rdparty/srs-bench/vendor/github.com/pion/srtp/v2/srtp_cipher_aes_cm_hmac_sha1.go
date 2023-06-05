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

	srtcpSessionSalt []byte
	srtcpSessionAuth hash.Hash
	srtcpBlock       cipher.Block
}

func newSrtpCipherAesCmHmacSha1(profile ProtectionProfile, masterKey, masterSalt []byte) (*srtpCipherAesCmHmacSha1, error) {
	s := &srtpCipherAesCmHmacSha1{ProtectionProfile: profile}
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

	authKeyLen, err := profile.authKeyLen()
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
	return s, nil
}

func (s *srtpCipherAesCmHmacSha1) encryptRTP(dst []byte, header *rtp.Header, payload []byte, roc uint32) (ciphertext []byte, err error) {
	// Grow the given buffer to fit the output.
	authTagLen, err := s.rtpAuthTagLen()
	if err != nil {
		return nil, err
	}
	dst = growBufferSize(dst, header.MarshalSize()+len(payload)+authTagLen)

	// Copy the header unencrypted.
	n, err := header.MarshalTo(dst)
	if err != nil {
		return nil, err
	}

	// Encrypt the payload
	counter := generateCounter(header.SequenceNumber, roc, header.SSRC, s.srtpSessionSalt)
	if err = xorBytesCTR(s.srtpBlock, counter[:], dst[n:], payload); err != nil {
		return nil, err
	}
	n += len(payload)

	// Generate the auth tag.
	authTag, err := s.generateSrtpAuthTag(dst[:n], roc)
	if err != nil {
		return nil, err
	}

	// Write the auth tag to the dest.
	copy(dst[n:], authTag)

	return dst, nil
}

func (s *srtpCipherAesCmHmacSha1) decryptRTP(dst, ciphertext []byte, header *rtp.Header, headerLen int, roc uint32) ([]byte, error) {
	// Split the auth tag and the cipher text into two parts.
	authTagLen, err := s.rtpAuthTagLen()
	if err != nil {
		return nil, err
	}
	actualTag := ciphertext[len(ciphertext)-authTagLen:]
	ciphertext = ciphertext[:len(ciphertext)-authTagLen]

	// Generate the auth tag we expect to see from the ciphertext.
	expectedTag, err := s.generateSrtpAuthTag(ciphertext, roc)
	if err != nil {
		return nil, err
	}

	// See if the auth tag actually matches.
	// We use a constant time comparison to prevent timing attacks.
	if subtle.ConstantTimeCompare(actualTag, expectedTag) != 1 {
		return nil, errFailedToVerifyAuthTag
	}

	// Write the plaintext header to the destination buffer.
	copy(dst, ciphertext[:headerLen])

	// Decrypt the ciphertext for the payload.
	counter := generateCounter(header.SequenceNumber, roc, header.SSRC, s.srtpSessionSalt)
	err = xorBytesCTR(
		s.srtpBlock, counter[:], dst[headerLen:], ciphertext[headerLen:],
	)
	return dst, err
}

func (s *srtpCipherAesCmHmacSha1) encryptRTCP(dst, decrypted []byte, srtcpIndex uint32, ssrc uint32) ([]byte, error) {
	dst = allocateIfMismatch(dst, decrypted)

	// Encrypt everything after header
	counter := generateCounter(uint16(srtcpIndex&0xffff), srtcpIndex>>16, ssrc, s.srtcpSessionSalt)
	if err := xorBytesCTR(s.srtcpBlock, counter[:], dst[8:], dst[8:]); err != nil {
		return nil, err
	}

	// Add SRTCP Index and set Encryption bit
	dst = append(dst, make([]byte, 4)...)
	binary.BigEndian.PutUint32(dst[len(dst)-4:], srtcpIndex)
	dst[len(dst)-4] |= 0x80

	authTag, err := s.generateSrtcpAuthTag(dst)
	if err != nil {
		return nil, err
	}
	return append(dst, authTag...), nil
}

func (s *srtpCipherAesCmHmacSha1) decryptRTCP(out, encrypted []byte, index, ssrc uint32) ([]byte, error) {
	authTagLen, err := s.rtcpAuthTagLen()
	if err != nil {
		return nil, err
	}
	tailOffset := len(encrypted) - (authTagLen + srtcpIndexSize)
	out = out[0:tailOffset]

	expectedTag, err := s.generateSrtcpAuthTag(encrypted[:len(encrypted)-authTagLen])
	if err != nil {
		return nil, err
	}

	actualTag := encrypted[len(encrypted)-authTagLen:]
	if subtle.ConstantTimeCompare(actualTag, expectedTag) != 1 {
		return nil, errFailedToVerifyAuthTag
	}

	counter := generateCounter(uint16(index&0xffff), index>>16, ssrc, s.srtcpSessionSalt)
	err = xorBytesCTR(s.srtcpBlock, counter[:], out[8:], out[8:])

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
	authTagLen, err := s.rtpAuthTagLen()
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
	authTagLen, err := s.rtcpAuthTagLen()
	if err != nil {
		return nil, err
	}

	return s.srtcpSessionAuth.Sum(nil)[0:authTagLen], nil
}

func (s *srtpCipherAesCmHmacSha1) getRTCPIndex(in []byte) uint32 {
	authTagLen, _ := s.rtcpAuthTagLen()
	tailOffset := len(in) - (authTagLen + srtcpIndexSize)
	srtcpIndexBuffer := in[tailOffset : tailOffset+srtcpIndexSize]
	return binary.BigEndian.Uint32(srtcpIndexBuffer) &^ (1 << 31)
}
