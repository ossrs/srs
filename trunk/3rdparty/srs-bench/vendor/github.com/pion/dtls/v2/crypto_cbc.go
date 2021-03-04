package dtls

import ( //nolint:gci
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha1" //nolint:gosec
	"encoding/binary"
)

// block ciphers using cipher block chaining.
type cbcMode interface {
	cipher.BlockMode
	SetIV([]byte)
}

// State needed to handle encrypted input/output
type cryptoCBC struct {
	writeCBC, readCBC cbcMode
	writeMac, readMac []byte
}

// Currently hardcoded to be SHA1 only
var cryptoCBCMacFunc = sha1.New //nolint:gochecknoglobals

func newCryptoCBC(localKey, localWriteIV, localMac, remoteKey, remoteWriteIV, remoteMac []byte) (*cryptoCBC, error) {
	writeBlock, err := aes.NewCipher(localKey)
	if err != nil {
		return nil, err
	}

	readBlock, err := aes.NewCipher(remoteKey)
	if err != nil {
		return nil, err
	}

	return &cryptoCBC{
		writeCBC: cipher.NewCBCEncrypter(writeBlock, localWriteIV).(cbcMode),
		writeMac: localMac,

		readCBC: cipher.NewCBCDecrypter(readBlock, remoteWriteIV).(cbcMode),
		readMac: remoteMac,
	}, nil
}

func (c *cryptoCBC) encrypt(pkt *recordLayer, raw []byte) ([]byte, error) {
	payload := raw[recordLayerHeaderSize:]
	raw = raw[:recordLayerHeaderSize]
	blockSize := c.writeCBC.BlockSize()

	// Generate + Append MAC
	h := pkt.recordLayerHeader

	MAC, err := prfMac(h.epoch, h.sequenceNumber, h.contentType, h.protocolVersion, payload, c.writeMac)
	if err != nil {
		return nil, err
	}
	payload = append(payload, MAC...)

	// Generate + Append padding
	padding := make([]byte, blockSize-len(payload)%blockSize)
	paddingLen := len(padding)
	for i := 0; i < paddingLen; i++ {
		padding[i] = byte(paddingLen - 1)
	}
	payload = append(payload, padding...)

	// Generate IV
	iv := make([]byte, blockSize)
	if _, err := rand.Read(iv); err != nil {
		return nil, err
	}

	// Set IV + Encrypt + Prepend IV
	c.writeCBC.SetIV(iv)
	c.writeCBC.CryptBlocks(payload, payload)
	payload = append(iv, payload...)

	// Prepend unencrypte header with encrypted payload
	raw = append(raw, payload...)

	// Update recordLayer size to include IV+MAC+Padding
	binary.BigEndian.PutUint16(raw[recordLayerHeaderSize-2:], uint16(len(raw)-recordLayerHeaderSize))

	return raw, nil
}

func (c *cryptoCBC) decrypt(in []byte) ([]byte, error) {
	body := in[recordLayerHeaderSize:]
	blockSize := c.readCBC.BlockSize()
	mac := cryptoCBCMacFunc()

	var h recordLayerHeader
	err := h.Unmarshal(in)
	switch {
	case err != nil:
		return nil, err
	case h.contentType == contentTypeChangeCipherSpec:
		// Nothing to encrypt with ChangeCipherSpec
		return in, nil
	case len(body)%blockSize != 0 || len(body) < blockSize+max(mac.Size()+1, blockSize):
		return nil, errNotEnoughRoomForNonce
	}

	// Set + remove per record IV
	c.readCBC.SetIV(body[:blockSize])
	body = body[blockSize:]

	// Decrypt
	c.readCBC.CryptBlocks(body, body)

	// Padding+MAC needs to be checked in constant time
	// Otherwise we reveal information about the level of correctness
	paddingLen, paddingGood := examinePadding(body)

	macSize := mac.Size()
	if len(body) < macSize {
		return nil, errInvalidMAC
	}

	dataEnd := len(body) - macSize - paddingLen

	expectedMAC := body[dataEnd : dataEnd+macSize]
	actualMAC, err := prfMac(h.epoch, h.sequenceNumber, h.contentType, h.protocolVersion, body[:dataEnd], c.readMac)

	// Compute Local MAC and compare
	if paddingGood != 255 || err != nil || !hmac.Equal(actualMAC, expectedMAC) {
		return nil, errInvalidMAC
	}

	return append(in[:recordLayerHeaderSize], body[:dataEnd]...), nil
}
