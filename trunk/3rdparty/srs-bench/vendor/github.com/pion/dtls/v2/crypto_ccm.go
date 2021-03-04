package dtls

import (
	"crypto/aes"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"

	"github.com/pion/dtls/v2/pkg/crypto/ccm"
)

var errDecryptPacket = errors.New("decryptPacket")

type cryptoCCMTagLen int

const (
	cryptoCCM8TagLength  cryptoCCMTagLen = 8
	cryptoCCMTagLength   cryptoCCMTagLen = 16
	cryptoCCMNonceLength                 = 12
)

// State needed to handle encrypted input/output
type cryptoCCM struct {
	localCCM, remoteCCM         ccm.CCM
	localWriteIV, remoteWriteIV []byte
	tagLen                      cryptoCCMTagLen
}

func newCryptoCCM(tagLen cryptoCCMTagLen, localKey, localWriteIV, remoteKey, remoteWriteIV []byte) (*cryptoCCM, error) {
	localBlock, err := aes.NewCipher(localKey)
	if err != nil {
		return nil, err
	}
	localCCM, err := ccm.NewCCM(localBlock, int(tagLen), cryptoCCMNonceLength)
	if err != nil {
		return nil, err
	}

	remoteBlock, err := aes.NewCipher(remoteKey)
	if err != nil {
		return nil, err
	}
	remoteCCM, err := ccm.NewCCM(remoteBlock, int(tagLen), cryptoCCMNonceLength)
	if err != nil {
		return nil, err
	}

	return &cryptoCCM{
		localCCM:      localCCM,
		localWriteIV:  localWriteIV,
		remoteCCM:     remoteCCM,
		remoteWriteIV: remoteWriteIV,
		tagLen:        tagLen,
	}, nil
}

func (c *cryptoCCM) encrypt(pkt *recordLayer, raw []byte) ([]byte, error) {
	payload := raw[recordLayerHeaderSize:]
	raw = raw[:recordLayerHeaderSize]

	nonce := append(append([]byte{}, c.localWriteIV[:4]...), make([]byte, 8)...)
	if _, err := rand.Read(nonce[4:]); err != nil {
		return nil, err
	}

	additionalData := generateAEADAdditionalData(&pkt.recordLayerHeader, len(payload))
	encryptedPayload := c.localCCM.Seal(nil, nonce, payload, additionalData)

	encryptedPayload = append(nonce[4:], encryptedPayload...)
	raw = append(raw, encryptedPayload...)

	// Update recordLayer size to include explicit nonce
	binary.BigEndian.PutUint16(raw[recordLayerHeaderSize-2:], uint16(len(raw)-recordLayerHeaderSize))
	return raw, nil
}

func (c *cryptoCCM) decrypt(in []byte) ([]byte, error) {
	var h recordLayerHeader
	err := h.Unmarshal(in)
	switch {
	case err != nil:
		return nil, err
	case h.contentType == contentTypeChangeCipherSpec:
		// Nothing to encrypt with ChangeCipherSpec
		return in, nil
	case len(in) <= (8 + recordLayerHeaderSize):
		return nil, errNotEnoughRoomForNonce
	}

	nonce := append(append([]byte{}, c.remoteWriteIV[:4]...), in[recordLayerHeaderSize:recordLayerHeaderSize+8]...)
	out := in[recordLayerHeaderSize+8:]

	additionalData := generateAEADAdditionalData(&h, len(out)-int(c.tagLen))
	out, err = c.remoteCCM.Open(out[:0], nonce, out, additionalData)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errDecryptPacket, err)
	}
	return append(in[:recordLayerHeaderSize], out...), nil
}
