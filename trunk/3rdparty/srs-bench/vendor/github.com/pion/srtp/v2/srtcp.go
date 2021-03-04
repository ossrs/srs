package srtp

import (
	"encoding/binary"
	"fmt"

	"github.com/pion/rtcp"
)

const maxSRTCPIndex = 0x7FFFFFFF

func (c *Context) decryptRTCP(dst, encrypted []byte) ([]byte, error) {
	out := allocateIfMismatch(dst, encrypted)
	tailOffset := len(encrypted) - (c.cipher.authTagLen() + srtcpIndexSize)

	if tailOffset < 0 {
		return nil, fmt.Errorf("%w: %d", errTooShortRTCP, len(encrypted))
	} else if isEncrypted := encrypted[tailOffset] >> 7; isEncrypted == 0 {
		return out, nil
	}

	index := c.cipher.getRTCPIndex(encrypted)
	ssrc := binary.BigEndian.Uint32(encrypted[4:])

	s := c.getSRTCPSSRCState(ssrc)
	markAsValid, ok := s.replayDetector.Check(uint64(index))
	if !ok {
		return nil, &errorDuplicated{Proto: "srtcp", SSRC: ssrc, Index: index}
	}

	out, err := c.cipher.decryptRTCP(out, encrypted, index, ssrc)
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
	ssrc := binary.BigEndian.Uint32(decrypted[4:])
	s := c.getSRTCPSSRCState(ssrc)

	// We roll over early because MSB is used for marking as encrypted
	s.srtcpIndex++
	if s.srtcpIndex > maxSRTCPIndex {
		s.srtcpIndex = 0
	}

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
