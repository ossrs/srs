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
	srtpSessionSalt []byte
	srtpSessionAuth hash.Hash
	srtpBlock       cipher.Block

	srtcpSessionSalt []byte
	srtcpSessionAuth hash.Hash
	srtcpBlock       cipher.Block
}

func newSrtpCipherAesCmHmacSha1(masterKey, masterSalt []byte) (*srtpCipherAesCmHmacSha1, error) {
	s := &srtpCipherAesCmHmacSha1{}
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

	authKeyLen, err := ProtectionProfileAes128CmHmacSha1_80.authKeyLen()
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

func (s *srtpCipherAesCmHmacSha1) authTagLen() int {
	return 10
}

func (s *srtpCipherAesCmHmacSha1) encryptRTP(dst []byte, header *rtp.Header, payload []byte, roc uint32) (ciphertext []byte, err error) {
	// Grow the given buffer to fit the output.
	dst = growBufferSize(dst, header.MarshalSize()+len(payload)+s.authTagLen())

	// Copy the header unencrypted.
	n, err := header.MarshalTo(dst)
	if err != nil {
		return nil, err
	}

	// Encrypt the payload
	counter := generateCounter(header.SequenceNumber, roc, header.SSRC, s.srtpSessionSalt)
	stream := cipher.NewCTR(s.srtpBlock, counter)
	stream.XORKeyStream(dst[n:], payload)
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

func (s *srtpCipherAesCmHmacSha1) decryptRTP(dst, ciphertext []byte, header *rtp.Header, roc uint32) ([]byte, error) {
	// Split the auth tag and the cipher text into two parts.
	actualTag := ciphertext[len(ciphertext)-s.authTagLen():]
	ciphertext = ciphertext[:len(ciphertext)-s.authTagLen()]

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
	copy(dst, ciphertext[:header.PayloadOffset])

	// Decrypt the ciphertext for the payload.
	counter := generateCounter(header.SequenceNumber, roc, header.SSRC, s.srtpSessionSalt)
	stream := cipher.NewCTR(s.srtpBlock, counter)
	stream.XORKeyStream(dst[header.PayloadOffset:], ciphertext[header.PayloadOffset:])
	return dst, nil
}

func (s *srtpCipherAesCmHmacSha1) encryptRTCP(dst, decrypted []byte, srtcpIndex uint32, ssrc uint32) ([]byte, error) {
	dst = allocateIfMismatch(dst, decrypted)

	// Encrypt everything after header
	stream := cipher.NewCTR(s.srtcpBlock, generateCounter(uint16(srtcpIndex&0xffff), srtcpIndex>>16, ssrc, s.srtcpSessionSalt))
	stream.XORKeyStream(dst[8:], dst[8:])

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
	tailOffset := len(encrypted) - (s.authTagLen() + srtcpIndexSize)
	out = out[0:tailOffset]

	expectedTag, err := s.generateSrtcpAuthTag(encrypted[:len(encrypted)-s.authTagLen()])
	if err != nil {
		return nil, err
	}

	actualTag := encrypted[len(encrypted)-s.authTagLen():]
	if subtle.ConstantTimeCompare(actualTag, expectedTag) != 1 {
		return nil, errFailedToVerifyAuthTag
	}

	stream := cipher.NewCTR(s.srtcpBlock, generateCounter(uint16(index&0xffff), index>>16, ssrc, s.srtcpSessionSalt))
	stream.XORKeyStream(out[8:], out[8:])

	return out, nil
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

	// Truncate the hash to the first 10 bytes.
	return s.srtpSessionAuth.Sum(nil)[0:s.authTagLen()], nil
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

	return s.srtcpSessionAuth.Sum(nil)[0:s.authTagLen()], nil
}

func (s *srtpCipherAesCmHmacSha1) getRTCPIndex(in []byte) uint32 {
	tailOffset := len(in) - (s.authTagLen() + srtcpIndexSize)
	srtcpIndexBuffer := in[tailOffset : tailOffset+srtcpIndexSize]
	return binary.BigEndian.Uint32(srtcpIndexBuffer) &^ (1 << 31)
}
