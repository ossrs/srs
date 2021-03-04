// Package srtp implements Secure Real-time Transport Protocol
package srtp

import (
	"github.com/pion/rtp"
)

func (c *Context) decryptRTP(dst, ciphertext []byte, header *rtp.Header) ([]byte, error) {
	s := c.getSRTPSSRCState(header.SSRC)

	markAsValid, ok := s.replayDetector.Check(uint64(header.SequenceNumber))
	if !ok {
		return nil, &errorDuplicated{
			Proto: "srtp", SSRC: header.SSRC, Index: uint32(header.SequenceNumber),
		}
	}

	dst = growBufferSize(dst, len(ciphertext)-c.cipher.authTagLen())
	roc, updateROC := s.nextRolloverCount(header.SequenceNumber)

	dst, err := c.cipher.decryptRTP(dst, ciphertext, header, roc)
	if err != nil {
		return nil, err
	}

	markAsValid()
	updateROC()
	return dst, nil
}

// DecryptRTP decrypts a RTP packet with an encrypted payload
func (c *Context) DecryptRTP(dst, encrypted []byte, header *rtp.Header) ([]byte, error) {
	if header == nil {
		header = &rtp.Header{}
	}

	if err := header.Unmarshal(encrypted); err != nil {
		return nil, err
	}

	return c.decryptRTP(dst, encrypted, header)
}

// EncryptRTP marshals and encrypts an RTP packet, writing to the dst buffer provided.
// If the dst buffer does not have the capacity to hold `len(plaintext) + 10` bytes, a new one will be allocated and returned.
// If a rtp.Header is provided, it will be Unmarshaled using the plaintext.
func (c *Context) EncryptRTP(dst []byte, plaintext []byte, header *rtp.Header) ([]byte, error) {
	if header == nil {
		header = &rtp.Header{}
	}

	if err := header.Unmarshal(plaintext); err != nil {
		return nil, err
	}

	return c.encryptRTP(dst, header, plaintext[header.PayloadOffset:])
}

// encryptRTP marshals and encrypts an RTP packet, writing to the dst buffer provided.
// If the dst buffer does not have the capacity, a new one will be allocated and returned.
// Similar to above but faster because it can avoid unmarshaling the header and marshaling the payload.
func (c *Context) encryptRTP(dst []byte, header *rtp.Header, payload []byte) (ciphertext []byte, err error) {
	s := c.getSRTPSSRCState(header.SSRC)
	roc, updateROC := s.nextRolloverCount(header.SequenceNumber)
	updateROC()

	return c.cipher.encryptRTP(dst, header, payload, roc)
}
