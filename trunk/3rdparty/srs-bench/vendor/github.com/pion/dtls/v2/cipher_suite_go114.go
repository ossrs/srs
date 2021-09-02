// +build go1.14

package dtls

import (
	"crypto/tls"
)

// VersionDTLS12 is the DTLS version in the same style as
// VersionTLSXX from crypto/tls
const VersionDTLS12 = 0xfefd

// Convert from our cipherSuite interface to a tls.CipherSuite struct
func toTLSCipherSuite(c CipherSuite) *tls.CipherSuite {
	return &tls.CipherSuite{
		ID:                uint16(c.ID()),
		Name:              c.String(),
		SupportedVersions: []uint16{VersionDTLS12},
		Insecure:          false,
	}
}

// CipherSuites returns a list of cipher suites currently implemented by this
// package, excluding those with security issues, which are returned by
// InsecureCipherSuites.
func CipherSuites() []*tls.CipherSuite {
	suites := allCipherSuites()
	res := make([]*tls.CipherSuite, len(suites))
	for i, c := range suites {
		res[i] = toTLSCipherSuite(c)
	}
	return res
}

// InsecureCipherSuites returns a list of cipher suites currently implemented by
// this package and which have security issues.
func InsecureCipherSuites() []*tls.CipherSuite {
	var res []*tls.CipherSuite
	return res
}
