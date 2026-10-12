// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"crypto/tls"
	"crypto/x509"
	"os"
	"path/filepath"
	"testing"
)

// TestClientTLS checks the client config trusts the test certificate for
// every address a case dials, and is a new config each time.
func TestClientTLS(t *testing.T) {
	cfg, err := ClientTLS()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.RootCAs == nil || cfg.InsecureSkipVerify {
		t.Fatalf("config %+v, want RootCAs and verification", cfg)
	}
	cert, err := tls.LoadX509KeyPair(filepath.Join(moduleRoot(), "testdata", "server.crt"), filepath.Join(moduleRoot(), "testdata", "server.key"))
	if err != nil {
		t.Fatal(err)
	}
	leaf, err := x509.ParseCertificate(cert.Certificate[0])
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"127.0.0.1", "localhost", "::1"} {
		if _, err := leaf.Verify(x509.VerifyOptions{DNSName: name, Roots: cfg.RootCAs}); err != nil {
			t.Errorf("verify for %v: %v", name, err)
		}
	}

	other, err := ClientTLS()
	if err != nil {
		t.Fatal(err)
	}
	if other == cfg {
		t.Error("ClientTLS returned the same config twice, want a new one a caller may change")
	}
	if _, err := os.Stat(filepath.Join(moduleRoot(), "testdata", "server.crt")); err != nil {
		t.Fatal(err)
	}
}
