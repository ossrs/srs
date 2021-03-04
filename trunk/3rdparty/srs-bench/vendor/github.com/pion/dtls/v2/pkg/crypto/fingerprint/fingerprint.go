// Package fingerprint provides a helper to create fingerprint string from certificate
package fingerprint

import (
	"crypto"
	"crypto/x509"
	"errors"
	"fmt"
)

var (
	errHashUnavailable          = errors.New("fingerprint: hash algorithm is not linked into the binary")
	errInvalidFingerprintLength = errors.New("fingerprint: invalid fingerprint length")
)

// Fingerprint creates a fingerprint for a certificate using the specified hash algorithm
func Fingerprint(cert *x509.Certificate, algo crypto.Hash) (string, error) {
	if !algo.Available() {
		return "", errHashUnavailable
	}
	h := algo.New()
	for i := 0; i < len(cert.Raw); {
		n, _ := h.Write(cert.Raw[i:])
		// Hash.Writer is specified to be never returning an error.
		// https://golang.org/pkg/hash/#Hash
		i += n
	}
	digest := []byte(fmt.Sprintf("%x", h.Sum(nil)))

	digestlen := len(digest)
	if digestlen == 0 {
		return "", nil
	}
	if digestlen%2 != 0 {
		return "", errInvalidFingerprintLength
	}
	res := make([]byte, digestlen>>1+digestlen-1)

	pos := 0
	for i, c := range digest {
		res[pos] = c
		pos++
		if (i)%2 != 0 && i < digestlen-1 {
			res[pos] = byte(':')
			pos++
		}
	}

	return string(res), nil
}
