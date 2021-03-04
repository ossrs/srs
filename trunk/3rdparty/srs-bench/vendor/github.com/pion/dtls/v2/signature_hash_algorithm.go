package dtls

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/rsa"
	"crypto/tls"

	"golang.org/x/xerrors"
)

type signatureHashAlgorithm struct {
	hash      hashAlgorithm
	signature signatureAlgorithm
}

func defaultSignatureSchemes() []signatureHashAlgorithm {
	return []signatureHashAlgorithm{
		{hashAlgorithmSHA256, signatureAlgorithmECDSA},
		{hashAlgorithmSHA384, signatureAlgorithmECDSA},
		{hashAlgorithmSHA512, signatureAlgorithmECDSA},
		{hashAlgorithmSHA256, signatureAlgorithmRSA},
		{hashAlgorithmSHA384, signatureAlgorithmRSA},
		{hashAlgorithmSHA512, signatureAlgorithmRSA},
		{hashAlgorithmEd25519, signatureAlgorithmEd25519},
	}
}

// select Signature Scheme returns most preferred and compatible scheme.
func selectSignatureScheme(sigs []signatureHashAlgorithm, privateKey crypto.PrivateKey) (signatureHashAlgorithm, error) {
	for _, ss := range sigs {
		if ss.isCompatible(privateKey) {
			return ss, nil
		}
	}
	return signatureHashAlgorithm{}, errNoAvailableSignatureSchemes
}

// isCompatible checks that given private key is compatible with the signature scheme.
func (s *signatureHashAlgorithm) isCompatible(privateKey crypto.PrivateKey) bool {
	switch privateKey.(type) {
	case ed25519.PrivateKey:
		return s.signature == signatureAlgorithmEd25519
	case *ecdsa.PrivateKey:
		return s.signature == signatureAlgorithmECDSA
	case *rsa.PrivateKey:
		return s.signature == signatureAlgorithmRSA
	default:
		return false
	}
}

// parseSignatureSchemes translates []tls.SignatureScheme to []signatureHashAlgorithm.
// It returns default signature scheme list if no SignatureScheme is passed.
func parseSignatureSchemes(sigs []tls.SignatureScheme, insecureHashes bool) ([]signatureHashAlgorithm, error) {
	if len(sigs) == 0 {
		return defaultSignatureSchemes(), nil
	}
	out := []signatureHashAlgorithm{}
	for _, ss := range sigs {
		sig := signatureAlgorithm(ss & 0xFF)
		if _, ok := signatureAlgorithms()[sig]; !ok {
			return nil, &FatalError{
				xerrors.Errorf("SignatureScheme %04x: %w", ss, errInvalidSignatureAlgorithm),
			}
		}
		h := hashAlgorithm(ss >> 8)
		if _, ok := hashAlgorithms()[h]; !ok {
			return nil, &FatalError{
				xerrors.Errorf("SignatureScheme %04x: %w", ss, errInvalidHashAlgorithm),
			}
		}
		if h.insecure() && !insecureHashes {
			continue
		}
		out = append(out, signatureHashAlgorithm{
			hash:      h,
			signature: sig,
		})
	}

	if len(out) == 0 {
		return nil, errNoAvailableSignatureSchemes
	}

	return out, nil
}
