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
	srtpCipher, srtcpCipher cipher.AEAD

	srtpSessionSalt, srtcpSessionSalt []byte
}

func newSrtpCipherAeadAesGcm(masterKey, masterSalt []byte) (*srtpCipherAeadAesGcm, error) {
	s := &srtpCipherAeadAesGcm{}

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

func (s *srtpCipherAeadAesGcm) authTagLen() int {
	return 16
}

func (s *srtpCipherAeadAesGcm) encryptRTP(dst []byte, header *rtp.Header, payload []byte, roc uint32) (ciphertext []byte, err error) {
	hdr, err := header.Marshal()
	if err != nil {
		return nil, err
	}

	iv := s.rtpInitializationVector(header, roc)
	out := s.srtpCipher.Seal(nil, iv, payload, hdr)
	return append(hdr, out...), nil
}

func (s *srtpCipherAeadAesGcm) decryptRTP(dst, ciphertext []byte, header *rtp.Header, roc uint32) ([]byte, error) {
	iv := s.rtpInitializationVector(header, roc)

	out, err := s.srtpCipher.Open(nil, iv, ciphertext[header.PayloadOffset:], ciphertext[:header.PayloadOffset])
	if err != nil {
		return nil, err
	}

	out = append(make([]byte, header.PayloadOffset), out...)
	copy(out, ciphertext[:header.PayloadOffset])

	return out, nil
}

func (s *srtpCipherAeadAesGcm) encryptRTCP(dst, decrypted []byte, srtcpIndex uint32, ssrc uint32) ([]byte, error) {
	iv := s.rtcpInitializationVector(srtcpIndex, ssrc)
	aad := s.rtcpAdditionalAuthenticatedData(decrypted, srtcpIndex)

	out := s.srtcpCipher.Seal(nil, iv, decrypted[8:], aad)

	out = append(make([]byte, 8), out...)
	copy(out, decrypted[:8])
	out = append(out, aad[8:]...)

	return out, nil
}

func (s *srtpCipherAeadAesGcm) decryptRTCP(out, encrypted []byte, srtcpIndex, ssrc uint32) ([]byte, error) {
	iv := s.rtcpInitializationVector(srtcpIndex, ssrc)
	aad := s.rtcpAdditionalAuthenticatedData(encrypted, srtcpIndex)

	decrypted, err := s.srtcpCipher.Open(nil, iv, encrypted[8:len(encrypted)-srtcpIndexSize], aad)
	if err != nil {
		return nil, err
	}

	decrypted = append(encrypted[:8], decrypted...)
	return decrypted, nil
}

// The 12-octet IV used by AES-GCM SRTP is formed by first concatenating
// 2 octets of zeroes, the 4-octet SSRC, the 4-octet rollover counter
// (ROC), and the 2-octet sequence number (SEQ).  The resulting 12-octet
// value is then XORed to the 12-octet salt to form the 12-octet IV.
//
// https://tools.ietf.org/html/rfc7714#section-8.1
func (s *srtpCipherAeadAesGcm) rtpInitializationVector(header *rtp.Header, roc uint32) []byte {
	iv := make([]byte, 12)
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
func (s *srtpCipherAeadAesGcm) rtcpInitializationVector(srtcpIndex uint32, ssrc uint32) []byte {
	iv := make([]byte, 12)

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
func (s *srtpCipherAeadAesGcm) rtcpAdditionalAuthenticatedData(rtcpPacket []byte, srtcpIndex uint32) []byte {
	aad := make([]byte, 12)

	copy(aad, rtcpPacket[:8])
	binary.BigEndian.PutUint32(aad[8:], srtcpIndex)
	aad[8] |= rtcpEncryptionFlag

	return aad
}

func (s *srtpCipherAeadAesGcm) getRTCPIndex(in []byte) uint32 {
	return binary.BigEndian.Uint32(in[len(in)-4:]) &^ (rtcpEncryptionFlag << 24)
}
