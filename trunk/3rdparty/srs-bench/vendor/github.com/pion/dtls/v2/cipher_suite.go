// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/rsa"
	"crypto/tls"
	"fmt"
	"hash"

	"github.com/pion/dtls/v2/internal/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
)

// CipherSuiteID is an ID for our supported CipherSuites
type CipherSuiteID = ciphersuite.ID

// Supported Cipher Suites
const (
	// AES-128-CCM
	TLS_ECDHE_ECDSA_WITH_AES_128_CCM   CipherSuiteID = ciphersuite.TLS_ECDHE_ECDSA_WITH_AES_128_CCM   //nolint:revive,stylecheck
	TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 CipherSuiteID = ciphersuite.TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 //nolint:revive,stylecheck

	// AES-128-GCM-SHA256
	TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 CipherSuiteID = ciphersuite.TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 //nolint:revive,stylecheck
	TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256   CipherSuiteID = ciphersuite.TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256   //nolint:revive,stylecheck

	TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 CipherSuiteID = ciphersuite.TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 //nolint:revive,stylecheck
	TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384   CipherSuiteID = ciphersuite.TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384   //nolint:revive,stylecheck

	// AES-256-CBC-SHA
	TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA CipherSuiteID = ciphersuite.TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA //nolint:revive,stylecheck
	TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA   CipherSuiteID = ciphersuite.TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA   //nolint:revive,stylecheck

	TLS_PSK_WITH_AES_128_CCM        CipherSuiteID = ciphersuite.TLS_PSK_WITH_AES_128_CCM        //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_128_CCM_8      CipherSuiteID = ciphersuite.TLS_PSK_WITH_AES_128_CCM_8      //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_256_CCM_8      CipherSuiteID = ciphersuite.TLS_PSK_WITH_AES_256_CCM_8      //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_128_GCM_SHA256 CipherSuiteID = ciphersuite.TLS_PSK_WITH_AES_128_GCM_SHA256 //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_128_CBC_SHA256 CipherSuiteID = ciphersuite.TLS_PSK_WITH_AES_128_CBC_SHA256 //nolint:revive,stylecheck

	TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256 CipherSuiteID = ciphersuite.TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256 //nolint:revive,stylecheck
)

// CipherSuiteAuthenticationType controls what authentication method is using during the handshake for a CipherSuite
type CipherSuiteAuthenticationType = ciphersuite.AuthenticationType

// AuthenticationType Enums
const (
	CipherSuiteAuthenticationTypeCertificate  CipherSuiteAuthenticationType = ciphersuite.AuthenticationTypeCertificate
	CipherSuiteAuthenticationTypePreSharedKey CipherSuiteAuthenticationType = ciphersuite.AuthenticationTypePreSharedKey
	CipherSuiteAuthenticationTypeAnonymous    CipherSuiteAuthenticationType = ciphersuite.AuthenticationTypeAnonymous
)

// CipherSuiteKeyExchangeAlgorithm controls what exchange algorithm is using during the handshake for a CipherSuite
type CipherSuiteKeyExchangeAlgorithm = ciphersuite.KeyExchangeAlgorithm

// CipherSuiteKeyExchangeAlgorithm Bitmask
const (
	CipherSuiteKeyExchangeAlgorithmNone  CipherSuiteKeyExchangeAlgorithm = ciphersuite.KeyExchangeAlgorithmNone
	CipherSuiteKeyExchangeAlgorithmPsk   CipherSuiteKeyExchangeAlgorithm = ciphersuite.KeyExchangeAlgorithmPsk
	CipherSuiteKeyExchangeAlgorithmEcdhe CipherSuiteKeyExchangeAlgorithm = ciphersuite.KeyExchangeAlgorithmEcdhe
)

var _ = allCipherSuites() // Necessary until this function isn't only used by Go 1.14

// CipherSuite is an interface that all DTLS CipherSuites must satisfy
type CipherSuite interface {
	// String of CipherSuite, only used for logging
	String() string

	// ID of CipherSuite.
	ID() CipherSuiteID

	// What type of Certificate does this CipherSuite use
	CertificateType() clientcertificate.Type

	// What Hash function is used during verification
	HashFunc() func() hash.Hash

	// AuthenticationType controls what authentication method is using during the handshake
	AuthenticationType() CipherSuiteAuthenticationType

	// KeyExchangeAlgorithm controls what exchange algorithm is using during the handshake
	KeyExchangeAlgorithm() CipherSuiteKeyExchangeAlgorithm

	// ECC (Elliptic Curve Cryptography) determines whether ECC extesions will be send during handshake.
	// https://datatracker.ietf.org/doc/html/rfc4492#page-10
	ECC() bool

	// Called when keying material has been generated, should initialize the internal cipher
	Init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error
	IsInitialized() bool
	Encrypt(pkt *recordlayer.RecordLayer, raw []byte) ([]byte, error)
	Decrypt(in []byte) ([]byte, error)
}

// CipherSuiteName provides the same functionality as tls.CipherSuiteName
// that appeared first in Go 1.14.
//
// Our implementation differs slightly in that it takes in a CiperSuiteID,
// like the rest of our library, instead of a uint16 like crypto/tls.
func CipherSuiteName(id CipherSuiteID) string {
	suite := cipherSuiteForID(id, nil)
	if suite != nil {
		return suite.String()
	}
	return fmt.Sprintf("0x%04X", uint16(id))
}

// Taken from https://www.iana.org/assignments/tls-parameters/tls-parameters.xml
// A cipherSuite is a specific combination of key agreement, cipher and MAC
// function.
func cipherSuiteForID(id CipherSuiteID, customCiphers func() []CipherSuite) CipherSuite {
	switch id { //nolint:exhaustive
	case TLS_ECDHE_ECDSA_WITH_AES_128_CCM:
		return ciphersuite.NewTLSEcdheEcdsaWithAes128Ccm()
	case TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8:
		return ciphersuite.NewTLSEcdheEcdsaWithAes128Ccm8()
	case TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
		return &ciphersuite.TLSEcdheEcdsaWithAes128GcmSha256{}
	case TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
		return &ciphersuite.TLSEcdheRsaWithAes128GcmSha256{}
	case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA:
		return &ciphersuite.TLSEcdheEcdsaWithAes256CbcSha{}
	case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA:
		return &ciphersuite.TLSEcdheRsaWithAes256CbcSha{}
	case TLS_PSK_WITH_AES_128_CCM:
		return ciphersuite.NewTLSPskWithAes128Ccm()
	case TLS_PSK_WITH_AES_128_CCM_8:
		return ciphersuite.NewTLSPskWithAes128Ccm8()
	case TLS_PSK_WITH_AES_256_CCM_8:
		return ciphersuite.NewTLSPskWithAes256Ccm8()
	case TLS_PSK_WITH_AES_128_GCM_SHA256:
		return &ciphersuite.TLSPskWithAes128GcmSha256{}
	case TLS_PSK_WITH_AES_128_CBC_SHA256:
		return &ciphersuite.TLSPskWithAes128CbcSha256{}
	case TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
		return &ciphersuite.TLSEcdheEcdsaWithAes256GcmSha384{}
	case TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
		return &ciphersuite.TLSEcdheRsaWithAes256GcmSha384{}
	case TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256:
		return ciphersuite.NewTLSEcdhePskWithAes128CbcSha256()
	}

	if customCiphers != nil {
		for _, c := range customCiphers() {
			if c.ID() == id {
				return c
			}
		}
	}

	return nil
}

// CipherSuites we support in order of preference
func defaultCipherSuites() []CipherSuite {
	return []CipherSuite{
		&ciphersuite.TLSEcdheEcdsaWithAes128GcmSha256{},
		&ciphersuite.TLSEcdheRsaWithAes128GcmSha256{},
		&ciphersuite.TLSEcdheEcdsaWithAes256CbcSha{},
		&ciphersuite.TLSEcdheRsaWithAes256CbcSha{},
		&ciphersuite.TLSEcdheEcdsaWithAes256GcmSha384{},
		&ciphersuite.TLSEcdheRsaWithAes256GcmSha384{},
	}
}

func allCipherSuites() []CipherSuite {
	return []CipherSuite{
		ciphersuite.NewTLSEcdheEcdsaWithAes128Ccm(),
		ciphersuite.NewTLSEcdheEcdsaWithAes128Ccm8(),
		&ciphersuite.TLSEcdheEcdsaWithAes128GcmSha256{},
		&ciphersuite.TLSEcdheRsaWithAes128GcmSha256{},
		&ciphersuite.TLSEcdheEcdsaWithAes256CbcSha{},
		&ciphersuite.TLSEcdheRsaWithAes256CbcSha{},
		ciphersuite.NewTLSPskWithAes128Ccm(),
		ciphersuite.NewTLSPskWithAes128Ccm8(),
		ciphersuite.NewTLSPskWithAes256Ccm8(),
		&ciphersuite.TLSPskWithAes128GcmSha256{},
		&ciphersuite.TLSEcdheEcdsaWithAes256GcmSha384{},
		&ciphersuite.TLSEcdheRsaWithAes256GcmSha384{},
	}
}

func cipherSuiteIDs(cipherSuites []CipherSuite) []uint16 {
	rtrn := []uint16{}
	for _, c := range cipherSuites {
		rtrn = append(rtrn, uint16(c.ID()))
	}
	return rtrn
}

func parseCipherSuites(userSelectedSuites []CipherSuiteID, customCipherSuites func() []CipherSuite, includeCertificateSuites, includePSKSuites bool) ([]CipherSuite, error) {
	cipherSuitesForIDs := func(ids []CipherSuiteID) ([]CipherSuite, error) {
		cipherSuites := []CipherSuite{}
		for _, id := range ids {
			c := cipherSuiteForID(id, nil)
			if c == nil {
				return nil, &invalidCipherSuiteError{id}
			}
			cipherSuites = append(cipherSuites, c)
		}
		return cipherSuites, nil
	}

	var (
		cipherSuites []CipherSuite
		err          error
		i            int
	)
	if userSelectedSuites != nil {
		cipherSuites, err = cipherSuitesForIDs(userSelectedSuites)
		if err != nil {
			return nil, err
		}
	} else {
		cipherSuites = defaultCipherSuites()
	}

	// Put CustomCipherSuites before ID selected suites
	if customCipherSuites != nil {
		cipherSuites = append(customCipherSuites(), cipherSuites...)
	}

	var foundCertificateSuite, foundPSKSuite, foundAnonymousSuite bool
	for _, c := range cipherSuites {
		switch {
		case includeCertificateSuites && c.AuthenticationType() == CipherSuiteAuthenticationTypeCertificate:
			foundCertificateSuite = true
		case includePSKSuites && c.AuthenticationType() == CipherSuiteAuthenticationTypePreSharedKey:
			foundPSKSuite = true
		case c.AuthenticationType() == CipherSuiteAuthenticationTypeAnonymous:
			foundAnonymousSuite = true
		default:
			continue
		}
		cipherSuites[i] = c
		i++
	}

	switch {
	case includeCertificateSuites && !foundCertificateSuite && !foundAnonymousSuite:
		return nil, errNoAvailableCertificateCipherSuite
	case includePSKSuites && !foundPSKSuite:
		return nil, errNoAvailablePSKCipherSuite
	case i == 0:
		return nil, errNoAvailableCipherSuites
	}

	return cipherSuites[:i], nil
}

func filterCipherSuitesForCertificate(cert *tls.Certificate, cipherSuites []CipherSuite) []CipherSuite {
	if cert == nil || cert.PrivateKey == nil {
		return cipherSuites
	}
	var certType clientcertificate.Type
	switch cert.PrivateKey.(type) {
	case ed25519.PrivateKey, *ecdsa.PrivateKey:
		certType = clientcertificate.ECDSASign
	case *rsa.PrivateKey:
		certType = clientcertificate.RSASign
	}

	filtered := []CipherSuite{}
	for _, c := range cipherSuites {
		if c.AuthenticationType() != CipherSuiteAuthenticationTypeCertificate || certType == c.CertificateType() {
			filtered = append(filtered, c)
		}
	}
	return filtered
}
