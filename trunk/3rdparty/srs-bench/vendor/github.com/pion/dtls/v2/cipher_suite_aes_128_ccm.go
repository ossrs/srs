package dtls

import (
	"crypto/sha256"
	"errors"
	"fmt"
	"hash"
	"sync/atomic"
)

type cipherSuiteAes128Ccm struct {
	ccm                   atomic.Value // *cryptoCCM
	clientCertificateType clientCertificateType
	id                    CipherSuiteID
	psk                   bool
	cryptoCCMTagLen       cryptoCCMTagLen
}

func newCipherSuiteAes128Ccm(clientCertificateType clientCertificateType, id CipherSuiteID, psk bool, cryptoCCMTagLen cryptoCCMTagLen) *cipherSuiteAes128Ccm {
	return &cipherSuiteAes128Ccm{
		clientCertificateType: clientCertificateType,
		id:                    id,
		psk:                   psk,
		cryptoCCMTagLen:       cryptoCCMTagLen,
	}
}

func (c *cipherSuiteAes128Ccm) certificateType() clientCertificateType {
	return c.clientCertificateType
}

func (c *cipherSuiteAes128Ccm) ID() CipherSuiteID {
	return c.id
}

func (c *cipherSuiteAes128Ccm) String() string {
	return c.id.String()
}

func (c *cipherSuiteAes128Ccm) hashFunc() func() hash.Hash {
	return sha256.New
}

func (c *cipherSuiteAes128Ccm) isPSK() bool {
	return c.psk
}

func (c *cipherSuiteAes128Ccm) isInitialized() bool {
	return c.ccm.Load() != nil
}

func (c *cipherSuiteAes128Ccm) init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error {
	const (
		prfMacLen = 0
		prfKeyLen = 16
		prfIvLen  = 4
	)

	keys, err := prfEncryptionKeys(masterSecret, clientRandom, serverRandom, prfMacLen, prfKeyLen, prfIvLen, c.hashFunc())
	if err != nil {
		return err
	}

	var ccm *cryptoCCM
	if isClient {
		ccm, err = newCryptoCCM(c.cryptoCCMTagLen, keys.clientWriteKey, keys.clientWriteIV, keys.serverWriteKey, keys.serverWriteIV)
	} else {
		ccm, err = newCryptoCCM(c.cryptoCCMTagLen, keys.serverWriteKey, keys.serverWriteIV, keys.clientWriteKey, keys.clientWriteIV)
	}
	c.ccm.Store(ccm)

	return err
}

var errCipherSuiteNotInit = errors.New("CipherSuite has not been initialized")

func (c *cipherSuiteAes128Ccm) encrypt(pkt *recordLayer, raw []byte) ([]byte, error) {
	ccm := c.ccm.Load()
	if ccm == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to encrypt", errCipherSuiteNotInit)
	}

	return ccm.(*cryptoCCM).encrypt(pkt, raw)
}

func (c *cipherSuiteAes128Ccm) decrypt(raw []byte) ([]byte, error) {
	ccm := c.ccm.Load()
	if ccm == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to decrypt", errCipherSuiteNotInit)
	}

	return ccm.(*cryptoCCM).decrypt(raw)
}
