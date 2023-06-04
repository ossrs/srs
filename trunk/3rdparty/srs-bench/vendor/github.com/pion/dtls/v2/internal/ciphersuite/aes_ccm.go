// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

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

// AesCcm is a base class used by multiple AES-CCM Ciphers
type AesCcm struct {
	ccm                   atomic.Value // *cryptoCCM
	clientCertificateType clientcertificate.Type
	id                    ID
	psk                   bool
	keyExchangeAlgorithm  KeyExchangeAlgorithm
	cryptoCCMTagLen       ciphersuite.CCMTagLen
	ecc                   bool
}

// CertificateType returns what type of certificate this CipherSuite exchanges
func (c *AesCcm) CertificateType() clientcertificate.Type {
	return c.clientCertificateType
}

// ID returns the ID of the CipherSuite
func (c *AesCcm) ID() ID {
	return c.id
}

func (c *AesCcm) String() string {
	return c.id.String()
}

// ECC uses Elliptic Curve Cryptography
func (c *AesCcm) ECC() bool {
	return c.ecc
}

// KeyExchangeAlgorithm controls what key exchange algorithm is using during the handshake
func (c *AesCcm) KeyExchangeAlgorithm() KeyExchangeAlgorithm {
	return c.keyExchangeAlgorithm
}

// HashFunc returns the hashing func for this CipherSuite
func (c *AesCcm) HashFunc() func() hash.Hash {
	return sha256.New
}

// AuthenticationType controls what authentication method is using during the handshake
func (c *AesCcm) AuthenticationType() AuthenticationType {
	if c.psk {
		return AuthenticationTypePreSharedKey
	}
	return AuthenticationTypeCertificate
}

// IsInitialized returns if the CipherSuite has keying material and can
// encrypt/decrypt packets
func (c *AesCcm) IsInitialized() bool {
	return c.ccm.Load() != nil
}

// Init initializes the internal Cipher with keying material
func (c *AesCcm) Init(masterSecret, clientRandom, serverRandom []byte, isClient bool, prfKeyLen int) error {
	const (
		prfMacLen = 0
		prfIvLen  = 4
	)

	keys, err := prf.GenerateEncryptionKeys(masterSecret, clientRandom, serverRandom, prfMacLen, prfKeyLen, prfIvLen, c.HashFunc())
	if err != nil {
		return err
	}

	var ccm *ciphersuite.CCM
	if isClient {
		ccm, err = ciphersuite.NewCCM(c.cryptoCCMTagLen, keys.ClientWriteKey, keys.ClientWriteIV, keys.ServerWriteKey, keys.ServerWriteIV)
	} else {
		ccm, err = ciphersuite.NewCCM(c.cryptoCCMTagLen, keys.ServerWriteKey, keys.ServerWriteIV, keys.ClientWriteKey, keys.ClientWriteIV)
	}
	c.ccm.Store(ccm)

	return err
}

// Encrypt encrypts a single TLS RecordLayer
func (c *AesCcm) Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error) {
	cipherSuite, ok := c.ccm.Load().(*ciphersuite.CCM)
	if !ok {
		return nil, fmt.Errorf("%w, unable to encrypt", errCipherSuiteNotInit)
	}

	return cipherSuite.Encrypt(pkt, raw)
}

// Decrypt decrypts a single TLS RecordLayer
func (c *AesCcm) Decrypt(raw []byte) ([]byte, error) {
	cipherSuite, ok := c.ccm.Load().(*ciphersuite.CCM)
	if !ok {
		return nil, fmt.Errorf("%w, unable to decrypt", errCipherSuiteNotInit)
	}

	return cipherSuite.Decrypt(raw)
}
