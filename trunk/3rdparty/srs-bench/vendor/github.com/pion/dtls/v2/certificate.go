// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"bytes"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"strings"
)

// ClientHelloInfo contains information from a ClientHello message in order to
// guide application logic in the GetCertificate.
type ClientHelloInfo struct {
	// ServerName indicates the name of the server requested by the client
	// in order to support virtual hosting. ServerName is only set if the
	// client is using SNI (see RFC 4366, Section 3.1).
	ServerName string

	// CipherSuites lists the CipherSuites supported by the client (e.g.
	// TLS_AES_128_GCM_SHA256, TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256).
	CipherSuites []CipherSuiteID
}

// CertificateRequestInfo contains information from a server's
// CertificateRequest message, which is used to demand a certificate and proof
// of control from a client.
type CertificateRequestInfo struct {
	// AcceptableCAs contains zero or more, DER-encoded, X.501
	// Distinguished Names. These are the names of root or intermediate CAs
	// that the server wishes the returned certificate to be signed by. An
	// empty slice indicates that the server has no preference.
	AcceptableCAs [][]byte
}

// SupportsCertificate returns nil if the provided certificate is supported by
// the server that sent the CertificateRequest. Otherwise, it returns an error
// describing the reason for the incompatibility.
// NOTE: original src: https://github.com/golang/go/blob/29b9a328d268d53833d2cc063d1d8b4bf6852675/src/crypto/tls/common.go#L1273
func (cri *CertificateRequestInfo) SupportsCertificate(c *tls.Certificate) error {
	if len(cri.AcceptableCAs) == 0 {
		return nil
	}

	for j, cert := range c.Certificate {
		x509Cert := c.Leaf
		// Parse the certificate if this isn't the leaf node, or if
		// chain.Leaf was nil.
		if j != 0 || x509Cert == nil {
			var err error
			if x509Cert, err = x509.ParseCertificate(cert); err != nil {
				return fmt.Errorf("failed to parse certificate #%d in the chain: %w", j, err)
			}
		}

		for _, ca := range cri.AcceptableCAs {
			if bytes.Equal(x509Cert.RawIssuer, ca) {
				return nil
			}
		}
	}
	return errNotAcceptableCertificateChain
}

func (c *handshakeConfig) setNameToCertificateLocked() {
	nameToCertificate := make(map[string]*tls.Certificate)
	for i := range c.localCertificates {
		cert := &c.localCertificates[i]
		x509Cert := cert.Leaf
		if x509Cert == nil {
			var parseErr error
			x509Cert, parseErr = x509.ParseCertificate(cert.Certificate[0])
			if parseErr != nil {
				continue
			}
		}
		if len(x509Cert.Subject.CommonName) > 0 {
			nameToCertificate[strings.ToLower(x509Cert.Subject.CommonName)] = cert
		}
		for _, san := range x509Cert.DNSNames {
			nameToCertificate[strings.ToLower(san)] = cert
		}
	}
	c.nameToCertificate = nameToCertificate
}

func (c *handshakeConfig) getCertificate(clientHelloInfo *ClientHelloInfo) (*tls.Certificate, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.localGetCertificate != nil &&
		(len(c.localCertificates) == 0 || len(clientHelloInfo.ServerName) > 0) {
		cert, err := c.localGetCertificate(clientHelloInfo)
		if cert != nil || err != nil {
			return cert, err
		}
	}

	if c.nameToCertificate == nil {
		c.setNameToCertificateLocked()
	}

	if len(c.localCertificates) == 0 {
		return nil, errNoCertificates
	}

	if len(c.localCertificates) == 1 {
		// There's only one choice, so no point doing any work.
		return &c.localCertificates[0], nil
	}

	if len(clientHelloInfo.ServerName) == 0 {
		return &c.localCertificates[0], nil
	}

	name := strings.TrimRight(strings.ToLower(clientHelloInfo.ServerName), ".")

	if cert, ok := c.nameToCertificate[name]; ok {
		return cert, nil
	}

	// try replacing labels in the name with wildcards until we get a
	// match.
	labels := strings.Split(name, ".")
	for i := range labels {
		labels[i] = "*"
		candidate := strings.Join(labels, ".")
		if cert, ok := c.nameToCertificate[candidate]; ok {
			return cert, nil
		}
	}

	// If nothing matches, return the first certificate.
	return &c.localCertificates[0], nil
}

// NOTE: original src: https://github.com/golang/go/blob/29b9a328d268d53833d2cc063d1d8b4bf6852675/src/crypto/tls/handshake_client.go#L974
func (c *handshakeConfig) getClientCertificate(cri *CertificateRequestInfo) (*tls.Certificate, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.localGetClientCertificate != nil {
		return c.localGetClientCertificate(cri)
	}

	for i := range c.localCertificates {
		chain := c.localCertificates[i]
		if err := cri.SupportsCertificate(&chain); err != nil {
			continue
		}
		return &chain, nil
	}

	// No acceptable certificate found. Don't send a certificate.
	return new(tls.Certificate), nil
}
