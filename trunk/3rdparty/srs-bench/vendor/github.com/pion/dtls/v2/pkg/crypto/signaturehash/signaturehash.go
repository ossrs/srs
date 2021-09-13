// Package signaturehash provides the SignatureHashAlgorithm as defined in TLS 1.2
package signaturehash

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/rsa"
	"crypto/tls"

	"github.com/pion/dtls/v2/pkg/crypto/hash"
	"github.com/pion/dtls/v2/pkg/crypto/signature"
	"golang.org/x/xerrors"
)

// Algorithm is a signature/hash algorithm pairs which may be used in
// digital signatures.
//
// https://tools.ietf.org/html/rfc5246#section-7.4.1.4.1
type Algorithm struct {
	Hash      hash.Algorithm
	Signature signature.Algorithm
}

// Algorithms are all the know SignatureHash Algorithms
func Algorithms() []Algorithm {
	return []Algorithm{
		{hash.SHA256, signature.ECDSA},
		{hash.SHA384, signature.ECDSA},
		{hash.SHA512, signature.ECDSA},
		{hash.SHA256, signature.RSA},
		{hash.SHA384, signature.RSA},
		{hash.SHA512, signature.RSA},
		{hash.Ed25519, signature.Ed25519},
	}
}

// SelectSignatureScheme returns most preferred and compatible scheme.
func SelectSignatureScheme(sigs []Algorithm, privateKey crypto.PrivateKey) (Algorithm, error) {
	for _, ss := range sigs {
		if ss.isCompatible(privateKey) {
			return ss, nil
		}
	}
	return Algorithm{}, errNoAvailableSignatureSchemes
}

// isCompatible checks that given private key is compatible with the signature scheme.
func (a *Algorithm) isCompatible(privateKey crypto.PrivateKey) bool {
	switch privateKey.(type) {
	case ed25519.PrivateKey:
		return a.Signature == signature.Ed25519
	case *ecdsa.PrivateKey:
		return a.Signature == signature.ECDSA
	case *rsa.PrivateKey:
		return a.Signature == signature.RSA
	default:
		return false
	}
}

// ParseSignatureSchemes translates []tls.SignatureScheme to []signatureHashAlgorithm.
// It returns default signature scheme list if no SignatureScheme is passed.
func ParseSignatureSchemes(sigs []tls.SignatureScheme, insecureHashes bool) ([]Algorithm, error) {
	if len(sigs) == 0 {
		return Algorithms(), nil
	}
	out := []Algorithm{}
	for _, ss := range sigs {
		sig := signature.Algorithm(ss & 0xFF)
		if _, ok := signature.Algorithms()[sig]; !ok {
			return nil,
				xerrors.Errorf("SignatureScheme %04x: %w", ss, errInvalidSignatureAlgorithm)
		}
		h := hash.Algorithm(ss >> 8)
		if _, ok := hash.Algorithms()[h]; !ok || (ok && h == hash.None) {
			return nil, xerrors.Errorf("SignatureScheme %04x: %w", ss, errInvalidHashAlgorithm)
		}
		if h.Insecure() && !insecureHashes {
			continue
		}
		out = append(out, Algorithm{
			Hash:      h,
			Signature: sig,
		})
	}

	if len(out) == 0 {
		return nil, errNoAvailableSignatureSchemes
	}

	return out, nil
}
