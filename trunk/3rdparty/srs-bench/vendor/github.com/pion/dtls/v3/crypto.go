// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/asn1"
	"encoding/binary"
	"math/big"
	"time"

	"github.com/pion/dtls/v3/pkg/crypto/elliptic"
	"github.com/pion/dtls/v3/pkg/crypto/hash"
)

type ecdsaSignature struct {
	R, S *big.Int
}

func valueKeyMessage(clientRandom, serverRandom, publicKey []byte, namedCurve elliptic.Curve) []byte {
	serverECDHParams := make([]byte, 4)
	serverECDHParams[0] = 3 // named curve
	binary.BigEndian.PutUint16(serverECDHParams[1:], uint16(namedCurve))
	serverECDHParams[3] = byte(len(publicKey))

	plaintext := []byte{}
	plaintext = append(plaintext, clientRandom...)
	plaintext = append(plaintext, serverRandom...)
	plaintext = append(plaintext, serverECDHParams...)
	plaintext = append(plaintext, publicKey...)

	return plaintext
}

// If the client provided a "signature_algorithms" extension, then all
// certificates provided by the server MUST be signed by a
// hash/signature algorithm pair that appears in that extension
//
// https://tools.ietf.org/html/rfc5246#section-7.4.2
func generateKeySignature(
	clientRandom, serverRandom, publicKey []byte,
	namedCurve elliptic.Curve,
	signer crypto.Signer,
	hashAlgorithm hash.Algorithm,
) ([]byte, error) {
	msg := valueKeyMessage(clientRandom, serverRandom, publicKey, namedCurve)
	switch signer.Public().(type) {
	case ed25519.PublicKey:
		// https://crypto.stackexchange.com/a/55483
		return signer.Sign(rand.Reader, msg, crypto.Hash(0))
	case *ecdsa.PublicKey:
		hashed := hashAlgorithm.Digest(msg)

		return signer.Sign(rand.Reader, hashed, hashAlgorithm.CryptoHash())
	case *rsa.PublicKey:
		hashed := hashAlgorithm.Digest(msg)

		return signer.Sign(rand.Reader, hashed, hashAlgorithm.CryptoHash())
	}

	return nil, errKeySignatureGenerateUnimplemented
}

//nolint:dupl,cyclop
func verifyKeySignature(
	message, remoteKeySignature []byte,
	hashAlgorithm hash.Algorithm,
	rawCertificates [][]byte,
) error {
	if len(rawCertificates) == 0 {
		return errLengthMismatch
	}
	certificate, err := x509.ParseCertificate(rawCertificates[0])
	if err != nil {
		return err
	}

	switch pubKey := certificate.PublicKey.(type) {
	case ed25519.PublicKey:
		if ok := ed25519.Verify(pubKey, message, remoteKeySignature); !ok {
			return errKeySignatureMismatch
		}

		return nil
	case *ecdsa.PublicKey:
		ecdsaSig := &ecdsaSignature{}
		if _, err := asn1.Unmarshal(remoteKeySignature, ecdsaSig); err != nil {
			return err
		}
		if ecdsaSig.R.Sign() <= 0 || ecdsaSig.S.Sign() <= 0 {
			return errInvalidECDSASignature
		}
		hashed := hashAlgorithm.Digest(message)
		if !ecdsa.Verify(pubKey, hashed, ecdsaSig.R, ecdsaSig.S) {
			return errKeySignatureMismatch
		}

		return nil
	case *rsa.PublicKey:
		hashed := hashAlgorithm.Digest(message)
		if rsa.VerifyPKCS1v15(pubKey, hashAlgorithm.CryptoHash(), hashed, remoteKeySignature) != nil {
			return errKeySignatureMismatch
		}

		return nil
	}

	return errKeySignatureVerifyUnimplemented
}

// If the server has sent a CertificateRequest message, the client MUST send the Certificate
// message.  The ClientKeyExchange message is now sent, and the content
// of that message will depend on the public key algorithm selected
// between the ClientHello and the ServerHello.  If the client has sent
// a certificate with signing ability, a digitally-signed
// CertificateVerify message is sent to explicitly verify possession of
// the private key in the certificate.
// https://tools.ietf.org/html/rfc5246#section-7.3
func generateCertificateVerify(
	handshakeBodies []byte,
	signer crypto.Signer,
	hashAlgorithm hash.Algorithm,
) ([]byte, error) {
	if _, ok := signer.Public().(ed25519.PublicKey); ok {
		// https://pkg.go.dev/crypto/ed25519#PrivateKey.Sign
		// Sign signs the given message with priv. Ed25519 performs two passes over
		// messages to be signed and therefore cannot handle pre-hashed messages.
		return signer.Sign(rand.Reader, handshakeBodies, crypto.Hash(0))
	}

	hashed := hashAlgorithm.Digest(handshakeBodies)

	switch signer.Public().(type) {
	case *ecdsa.PublicKey:
		return signer.Sign(rand.Reader, hashed, hashAlgorithm.CryptoHash())
	case *rsa.PublicKey:
		return signer.Sign(rand.Reader, hashed, hashAlgorithm.CryptoHash())
	}

	return nil, errInvalidSignatureAlgorithm
}

//nolint:dupl,cyclop
func verifyCertificateVerify(
	handshakeBodies []byte,
	hashAlgorithm hash.Algorithm,
	remoteKeySignature []byte,
	rawCertificates [][]byte,
) error {
	if len(rawCertificates) == 0 {
		return errLengthMismatch
	}
	certificate, err := x509.ParseCertificate(rawCertificates[0])
	if err != nil {
		return err
	}

	switch pubKey := certificate.PublicKey.(type) {
	case ed25519.PublicKey:
		if ok := ed25519.Verify(pubKey, handshakeBodies, remoteKeySignature); !ok {
			return errKeySignatureMismatch
		}

		return nil
	case *ecdsa.PublicKey:
		ecdsaSig := &ecdsaSignature{}
		if _, err := asn1.Unmarshal(remoteKeySignature, ecdsaSig); err != nil {
			return err
		}
		if ecdsaSig.R.Sign() <= 0 || ecdsaSig.S.Sign() <= 0 {
			return errInvalidECDSASignature
		}
		hash := hashAlgorithm.Digest(handshakeBodies)
		if !ecdsa.Verify(pubKey, hash, ecdsaSig.R, ecdsaSig.S) {
			return errKeySignatureMismatch
		}

		return nil
	case *rsa.PublicKey:
		hash := hashAlgorithm.Digest(handshakeBodies)
		if rsa.VerifyPKCS1v15(pubKey, hashAlgorithm.CryptoHash(), hash, remoteKeySignature) != nil {
			return errKeySignatureMismatch
		}

		return nil
	}

	return errKeySignatureVerifyUnimplemented
}

func loadCerts(rawCertificates [][]byte) ([]*x509.Certificate, error) {
	if len(rawCertificates) == 0 {
		return nil, errLengthMismatch
	}

	certs := make([]*x509.Certificate, 0, len(rawCertificates))
	for _, rawCert := range rawCertificates {
		cert, err := x509.ParseCertificate(rawCert)
		if err != nil {
			return nil, err
		}
		certs = append(certs, cert)
	}

	return certs, nil
}

func verifyClientCert(rawCertificates [][]byte, roots *x509.CertPool) (chains [][]*x509.Certificate, err error) {
	certificate, err := loadCerts(rawCertificates)
	if err != nil {
		return nil, err
	}
	intermediateCAPool := x509.NewCertPool()
	for _, cert := range certificate[1:] {
		intermediateCAPool.AddCert(cert)
	}
	opts := x509.VerifyOptions{
		Roots:         roots,
		CurrentTime:   time.Now(),
		Intermediates: intermediateCAPool,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}

	return certificate[0].Verify(opts)
}

func verifyServerCert(
	rawCertificates [][]byte,
	roots *x509.CertPool,
	serverName string,
) (chains [][]*x509.Certificate, err error) {
	certificate, err := loadCerts(rawCertificates)
	if err != nil {
		return nil, err
	}
	intermediateCAPool := x509.NewCertPool()
	for _, cert := range certificate[1:] {
		intermediateCAPool.AddCert(cert)
	}
	opts := x509.VerifyOptions{
		Roots:         roots,
		CurrentTime:   time.Now(),
		DNSName:       serverName,
		Intermediates: intermediateCAPool,
	}

	return certificate[0].Verify(opts)
}
