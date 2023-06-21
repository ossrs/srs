// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/base64"
	"encoding/pem"
	"fmt"
	"math/big"
	"strings"
	"time"

	"github.com/pion/dtls/v2/pkg/crypto/fingerprint"
	"github.com/pion/webrtc/v3/pkg/rtcerr"
)

// Certificate represents a x509Cert used to authenticate WebRTC communications.
type Certificate struct {
	privateKey crypto.PrivateKey
	x509Cert   *x509.Certificate
	statsID    string
}

// NewCertificate generates a new x509 compliant Certificate to be used
// by DTLS for encrypting data sent over the wire. This method differs from
// GenerateCertificate by allowing to specify a template x509.Certificate to
// be used in order to define certificate parameters.
func NewCertificate(key crypto.PrivateKey, tpl x509.Certificate) (*Certificate, error) {
	var err error
	var certDER []byte
	switch sk := key.(type) {
	case *rsa.PrivateKey:
		pk := sk.Public()
		tpl.SignatureAlgorithm = x509.SHA256WithRSA
		certDER, err = x509.CreateCertificate(rand.Reader, &tpl, &tpl, pk, sk)
		if err != nil {
			return nil, &rtcerr.UnknownError{Err: err}
		}
	case *ecdsa.PrivateKey:
		pk := sk.Public()
		tpl.SignatureAlgorithm = x509.ECDSAWithSHA256
		certDER, err = x509.CreateCertificate(rand.Reader, &tpl, &tpl, pk, sk)
		if err != nil {
			return nil, &rtcerr.UnknownError{Err: err}
		}
	default:
		return nil, &rtcerr.NotSupportedError{Err: ErrPrivateKeyType}
	}

	cert, err := x509.ParseCertificate(certDER)
	if err != nil {
		return nil, &rtcerr.UnknownError{Err: err}
	}

	return &Certificate{privateKey: key, x509Cert: cert, statsID: fmt.Sprintf("certificate-%d", time.Now().UnixNano())}, nil
}

// Equals determines if two certificates are identical by comparing both the
// secretKeys and x509Certificates.
func (c Certificate) Equals(o Certificate) bool {
	switch cSK := c.privateKey.(type) {
	case *rsa.PrivateKey:
		if oSK, ok := o.privateKey.(*rsa.PrivateKey); ok {
			if cSK.N.Cmp(oSK.N) != 0 {
				return false
			}
			return c.x509Cert.Equal(o.x509Cert)
		}
		return false
	case *ecdsa.PrivateKey:
		if oSK, ok := o.privateKey.(*ecdsa.PrivateKey); ok {
			if cSK.X.Cmp(oSK.X) != 0 || cSK.Y.Cmp(oSK.Y) != 0 {
				return false
			}
			return c.x509Cert.Equal(o.x509Cert)
		}
		return false
	default:
		return false
	}
}

// Expires returns the timestamp after which this certificate is no longer valid.
func (c Certificate) Expires() time.Time {
	if c.x509Cert == nil {
		return time.Time{}
	}
	return c.x509Cert.NotAfter
}

// GetFingerprints returns the list of certificate fingerprints, one of which
// is computed with the digest algorithm used in the certificate signature.
func (c Certificate) GetFingerprints() ([]DTLSFingerprint, error) {
	fingerprintAlgorithms := []crypto.Hash{crypto.SHA256}
	res := make([]DTLSFingerprint, len(fingerprintAlgorithms))

	i := 0
	for _, algo := range fingerprintAlgorithms {
		name, err := fingerprint.StringFromHash(algo)
		if err != nil {
			// nolint
			return nil, fmt.Errorf("%w: %v", ErrFailedToGenerateCertificateFingerprint, err)
		}
		value, err := fingerprint.Fingerprint(c.x509Cert, algo)
		if err != nil {
			// nolint
			return nil, fmt.Errorf("%w: %v", ErrFailedToGenerateCertificateFingerprint, err)
		}
		res[i] = DTLSFingerprint{
			Algorithm: name,
			Value:     value,
		}
	}

	return res[:i+1], nil
}

// GenerateCertificate causes the creation of an X.509 certificate and
// corresponding private key.
func GenerateCertificate(secretKey crypto.PrivateKey) (*Certificate, error) {
	// Max random value, a 130-bits integer, i.e 2^130 - 1
	maxBigInt := new(big.Int)
	/* #nosec */
	maxBigInt.Exp(big.NewInt(2), big.NewInt(130), nil).Sub(maxBigInt, big.NewInt(1))
	/* #nosec */
	serialNumber, err := rand.Int(rand.Reader, maxBigInt)
	if err != nil {
		return nil, &rtcerr.UnknownError{Err: err}
	}

	return NewCertificate(secretKey, x509.Certificate{
		Issuer:       pkix.Name{CommonName: generatedCertificateOrigin},
		NotBefore:    time.Now().AddDate(0, 0, -1),
		NotAfter:     time.Now().AddDate(0, 1, -1),
		SerialNumber: serialNumber,
		Version:      2,
		Subject:      pkix.Name{CommonName: generatedCertificateOrigin},
	})
}

// CertificateFromX509 creates a new WebRTC Certificate from a given PrivateKey and Certificate
//
// This can be used if you want to share a certificate across multiple PeerConnections
func CertificateFromX509(privateKey crypto.PrivateKey, certificate *x509.Certificate) Certificate {
	return Certificate{privateKey, certificate, fmt.Sprintf("certificate-%d", time.Now().UnixNano())}
}

func (c Certificate) collectStats(report *statsReportCollector) error {
	report.Collecting()

	fingerPrintAlgo, err := c.GetFingerprints()
	if err != nil {
		return err
	}

	base64Certificate := base64.RawURLEncoding.EncodeToString(c.x509Cert.Raw)

	stats := CertificateStats{
		Timestamp:            statsTimestampFrom(time.Now()),
		Type:                 StatsTypeCertificate,
		ID:                   c.statsID,
		Fingerprint:          fingerPrintAlgo[0].Value,
		FingerprintAlgorithm: fingerPrintAlgo[0].Algorithm,
		Base64Certificate:    base64Certificate,
		IssuerCertificateID:  c.x509Cert.Issuer.String(),
	}

	report.Collect(stats.ID, stats)
	return nil
}

// CertificateFromPEM creates a fresh certificate based on a string containing
// pem blocks fort the private key and x509 certificate
func CertificateFromPEM(pems string) (*Certificate, error) {
	// decode & parse the certificate
	block, more := pem.Decode([]byte(pems))
	if block == nil || block.Type != "CERTIFICATE" {
		return nil, errCertificatePEMFormatError
	}
	certBytes := make([]byte, base64.StdEncoding.DecodedLen(len(block.Bytes)))
	n, err := base64.StdEncoding.Decode(certBytes, block.Bytes)
	if err != nil {
		return nil, fmt.Errorf("failed to decode ceritifcate: %w", err)
	}
	cert, err := x509.ParseCertificate(certBytes[:n])
	if err != nil {
		return nil, fmt.Errorf("failed parsing ceritifcate: %w", err)
	}
	// decode & parse the private key
	block, _ = pem.Decode(more)
	if block == nil || block.Type != "PRIVATE KEY" {
		return nil, errCertificatePEMFormatError
	}
	privateKey, err := x509.ParsePKCS8PrivateKey(block.Bytes)
	if err != nil {
		return nil, fmt.Errorf("unable to parse private key: %w", err)
	}
	x := CertificateFromX509(privateKey, cert)
	return &x, nil
}

// PEM returns the certificate encoded as two pem block: once for the X509
// certificate and the other for the private key
func (c Certificate) PEM() (string, error) {
	// First write the X509 certificate
	var o strings.Builder
	xcertBytes := make(
		[]byte, base64.StdEncoding.EncodedLen(len(c.x509Cert.Raw)))
	base64.StdEncoding.Encode(xcertBytes, c.x509Cert.Raw)
	err := pem.Encode(&o, &pem.Block{Type: "CERTIFICATE", Bytes: xcertBytes})
	if err != nil {
		return "", fmt.Errorf("failed to pem encode the X certificate: %w", err)
	}
	// Next write the private key
	privBytes, err := x509.MarshalPKCS8PrivateKey(c.privateKey)
	if err != nil {
		return "", fmt.Errorf("failed to marshal private key: %w", err)
	}
	err = pem.Encode(&o, &pem.Block{Type: "PRIVATE KEY", Bytes: privBytes})
	if err != nil {
		return "", fmt.Errorf("failed to encode private key: %w", err)
	}
	return o.String(), nil
}
