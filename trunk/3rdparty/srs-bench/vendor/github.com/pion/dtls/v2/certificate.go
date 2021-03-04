package dtls

import (
	"crypto/tls"
	"crypto/x509"
	"strings"
)

func (c *handshakeConfig) getCertificate(serverName string) (*tls.Certificate, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.nameToCertificate == nil {
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

	if len(c.localCertificates) == 0 {
		return nil, errNoCertificates
	}

	if len(c.localCertificates) == 1 {
		// There's only one choice, so no point doing any work.
		return &c.localCertificates[0], nil
	}

	if len(serverName) == 0 {
		return &c.localCertificates[0], nil
	}

	name := strings.TrimRight(strings.ToLower(serverName), ".")

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
