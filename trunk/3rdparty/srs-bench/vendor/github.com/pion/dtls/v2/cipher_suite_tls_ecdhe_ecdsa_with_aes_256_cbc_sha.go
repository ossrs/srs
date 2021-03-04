package dtls

import (
	"crypto/sha256"
	"fmt"
	"hash"
	"sync/atomic"
)

type cipherSuiteTLSEcdheEcdsaWithAes256CbcSha struct {
	cbc atomic.Value // *cryptoCBC
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) certificateType() clientCertificateType {
	return clientCertificateTypeECDSASign
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) ID() CipherSuiteID {
	return TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) String() string {
	return "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA"
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) hashFunc() func() hash.Hash {
	return sha256.New
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) isPSK() bool {
	return false
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) isInitialized() bool {
	return c.cbc.Load() != nil
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error {
	const (
		prfMacLen = 20
		prfKeyLen = 32
		prfIvLen  = 16
	)

	keys, err := prfEncryptionKeys(masterSecret, clientRandom, serverRandom, prfMacLen, prfKeyLen, prfIvLen, c.hashFunc())
	if err != nil {
		return err
	}

	var cbc *cryptoCBC
	if isClient {
		cbc, err = newCryptoCBC(
			keys.clientWriteKey, keys.clientWriteIV, keys.clientMACKey,
			keys.serverWriteKey, keys.serverWriteIV, keys.serverMACKey,
		)
	} else {
		cbc, err = newCryptoCBC(
			keys.serverWriteKey, keys.serverWriteIV, keys.serverMACKey,
			keys.clientWriteKey, keys.clientWriteIV, keys.clientMACKey,
		)
	}
	c.cbc.Store(cbc)

	return err
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) encrypt(pkt *recordLayer, raw []byte) ([]byte, error) {
	cbc := c.cbc.Load()
	if cbc == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to encrypt", errCipherSuiteNotInit)
	}

	return cbc.(*cryptoCBC).encrypt(pkt, raw)
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes256CbcSha) decrypt(raw []byte) ([]byte, error) {
	cbc := c.cbc.Load()
	if cbc == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to decrypt", errCipherSuiteNotInit)
	}

	return cbc.(*cryptoCBC).decrypt(raw)
}
