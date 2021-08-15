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

// Aes128Ccm is a base class used by multiple AES-CCM Ciphers
type Aes128Ccm struct {
	ccm                   atomic.Value // *cryptoCCM
	clientCertificateType clientcertificate.Type
	id                    ID
	psk                   bool
	cryptoCCMTagLen       ciphersuite.CCMTagLen
}

func newAes128Ccm(clientCertificateType clientcertificate.Type, id ID, psk bool, cryptoCCMTagLen ciphersuite.CCMTagLen) *Aes128Ccm {
	return &Aes128Ccm{
		clientCertificateType: clientCertificateType,
		id:                    id,
		psk:                   psk,
		cryptoCCMTagLen:       cryptoCCMTagLen,
	}
}

// CertificateType returns what type of certificate this CipherSuite exchanges
func (c *Aes128Ccm) CertificateType() clientcertificate.Type {
	return c.clientCertificateType
}

// ID returns the ID of the CipherSuite
func (c *Aes128Ccm) ID() ID {
	return c.id
}

func (c *Aes128Ccm) String() string {
	return c.id.String()
}

// HashFunc returns the hashing func for this CipherSuite
func (c *Aes128Ccm) HashFunc() func() hash.Hash {
	return sha256.New
}

// AuthenticationType controls what authentication method is using during the handshake
func (c *Aes128Ccm) AuthenticationType() AuthenticationType {
	if c.psk {
		return AuthenticationTypePreSharedKey
	}
	return AuthenticationTypeCertificate
}

// IsInitialized returns if the CipherSuite has keying material and can
// encrypt/decrypt packets
func (c *Aes128Ccm) IsInitialized() bool {
	return c.ccm.Load() != nil
}

// Init initializes the internal Cipher with keying material
func (c *Aes128Ccm) Init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error {
	const (
		prfMacLen = 0
		prfKeyLen = 16
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
func (c *Aes128Ccm) Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error) {
	ccm := c.ccm.Load()
	if ccm == nil {
		return nil, fmt.Errorf("%w, unable to encrypt", errCipherSuiteNotInit)
	}

	return ccm.(*ciphersuite.CCM).Encrypt(pkt, raw)
}

// Decrypt decrypts a single TLS RecordLayer
func (c *Aes128Ccm) Decrypt(raw []byte) ([]byte, error) {
	ccm := c.ccm.Load()
	if ccm == nil {
		return nil, fmt.Errorf("%w, unable to decrypt", errCipherSuiteNotInit)
	}

	return ccm.(*ciphersuite.CCM).Decrypt(raw)
}
