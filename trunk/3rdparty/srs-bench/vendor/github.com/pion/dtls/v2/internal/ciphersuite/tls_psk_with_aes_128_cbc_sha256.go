package ciphersuite

import (
	"crypto/sha256"
	"fmt"
	"hash"
	"sync/atomic"

	"github.com/pion/dtls/v2/pkg/crypto/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
	"github.com/pion/dtls/v2/pkg/crypto/prf"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

// TLSPskWithAes128CbcSha256 implements the TLS_PSK_WITH_AES_128_CBC_SHA256 CipherSuite
type TLSPskWithAes128CbcSha256 struct {
	cbc atomic.Value // *cryptoCBC
}

// CertificateType returns what type of certificate this CipherSuite exchanges
func (c *TLSPskWithAes128CbcSha256) CertificateType() clientcertificate.Type {
	return clientcertificate.Type(0)
}

// ID returns the ID of the CipherSuite
func (c *TLSPskWithAes128CbcSha256) ID() ID {
	return TLS_PSK_WITH_AES_128_CBC_SHA256
}

func (c *TLSPskWithAes128CbcSha256) String() string {
	return "TLS_PSK_WITH_AES_128_CBC_SHA256"
}

// HashFunc returns the hashing func for this CipherSuite
func (c *TLSPskWithAes128CbcSha256) HashFunc() func() hash.Hash {
	return sha256.New
}

// AuthenticationType controls what authentication method is using during the handshake
func (c *TLSPskWithAes128CbcSha256) AuthenticationType() AuthenticationType {
	return AuthenticationTypePreSharedKey
}

// IsInitialized returns if the CipherSuite has keying material and can
// encrypt/decrypt packets
func (c *TLSPskWithAes128CbcSha256) IsInitialized() bool {
	return c.cbc.Load() != nil
}

// Init initializes the internal Cipher with keying material
func (c *TLSPskWithAes128CbcSha256) Init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error {
	const (
		prfMacLen = 32
		prfKeyLen = 16
		prfIvLen  = 16
	)

	keys, err := prf.GenerateEncryptionKeys(masterSecret, clientRandom, serverRandom, prfMacLen, prfKeyLen, prfIvLen, c.HashFunc())
	if err != nil {
		return err
	}

	var cbc *ciphersuite.CBC
	if isClient {
		cbc, err = ciphersuite.NewCBC(
			keys.ClientWriteKey, keys.ClientWriteIV, keys.ClientMACKey,
			keys.ServerWriteKey, keys.ServerWriteIV, keys.ServerMACKey,
			c.HashFunc(),
		)
	} else {
		cbc, err = ciphersuite.NewCBC(
			keys.ServerWriteKey, keys.ServerWriteIV, keys.ServerMACKey,
			keys.ClientWriteKey, keys.ClientWriteIV, keys.ClientMACKey,
			c.HashFunc(),
		)
	}
	c.cbc.Store(cbc)

	return err
}

// Encrypt encrypts a single TLS RecordLayer
func (c *TLSPskWithAes128CbcSha256) Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error) {
	cbc := c.cbc.Load()
	if cbc == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to decrypt", errCipherSuiteNotInit)
	}

	return cbc.(*ciphersuite.CBC).Encrypt(pkt, raw)
}

// Decrypt decrypts a single TLS RecordLayer
func (c *TLSPskWithAes128CbcSha256) Decrypt(raw []byte) ([]byte, error) {
	cbc := c.cbc.Load()
	if cbc == nil { // !c.isInitialized()
		return nil, fmt.Errorf("%w, unable to decrypt", errCipherSuiteNotInit)
	}

	return cbc.(*ciphersuite.CBC).Decrypt(raw)
}
