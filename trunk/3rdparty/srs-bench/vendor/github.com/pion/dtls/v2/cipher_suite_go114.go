// +build go1.14

package dtls

import (
	"crypto/tls"
)

// Convert from our cipherSuite interface to a tls.CipherSuite struct
func toTLSCipherSuite(c cipherSuite) *tls.CipherSuite {
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
