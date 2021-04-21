package dtls

import (
	"crypto/sha256"
	"fmt"
	"hash"
	"sync/atomic"
)

type cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256 struct {
	gcm atomic.Value // *cryptoGCM
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) certificateType() clientCertificateType {
	return clientCertificateTypeECDSASign
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) ID() CipherSuiteID {
	return TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) String() string {
	return "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) hashFunc() func() hash.Hash {
	return sha256.New
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) isPSK() bool {
	return false
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) isInitialized() bool {
	return c.gcm.Load() != nil
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error {
	const (
		prfMacLen = 0
		prfKeyLen = 16
		prfIvLen  = 4
	)

	keys, err := prfEncryptionKeys(masterSecret, clientRandom, serverRandom, prfMacLen, prfKeyLen, prfIvLen, c.hashFunc())
	if err != nil {
		return err
	}

	var gcm *cryptoGCM
	if isClient {
		gcm, err = newCryptoGCM(keys.clientWriteKey, keys.clientWriteIV, keys.serverWriteKey, keys.serverWriteIV)
	} else {
		gcm, err = newCryptoGCM(keys.serverWriteKey, keys.serverWriteIV, keys.clientWriteKey, keys.clientWriteIV)
	}
	c.gcm.Store(gcm)

	return err
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) encrypt(pkt *recordLayer, raw []byte) ([]byte, error) {
	gcm := c.gcm.Load()
	if gcm == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to encrypt", errCipherSuiteNotInit)
	}

	return gcm.(*cryptoGCM).encrypt(pkt, raw)
}

func (c *cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256) decrypt(raw []byte) ([]byte, error) {
	gcm := c.gcm.Load()
	if gcm == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to decrypt", errCipherSuiteNotInit)
	}

	return gcm.(*cryptoGCM).decrypt(raw)
}
