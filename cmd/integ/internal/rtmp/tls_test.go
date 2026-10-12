// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// testCert loads the suite's test certificate for localhost, 127.0.0.1 and
// ::1, and a pool that trusts it.
func testCert(t *testing.T) (tls.Certificate, *x509.CertPool) {
	t.Helper()
	dir := filepath.Join("..", "..", "testdata")
	cert, err := tls.LoadX509KeyPair(filepath.Join(dir, "server.crt"), filepath.Join(dir, "server.key"))
	if err != nil {
		t.Fatal(err)
	}
	pem, err := os.ReadFile(filepath.Join(dir, "server.crt"))
	if err != nil {
		t.Fatal(err)
	}
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(pem) {
		t.Fatal("no certificate in server.crt")
	}
	return cert, pool
}

// newTLSFakeServer is a fake server that serves RTMP inside TLS.
func newTLSFakeServer(t *testing.T) (*fakeServer, *x509.CertPool) {
	cert, pool := testCert(t)
	s := newFakeServer(t)
	s.tls = &tls.Config{Certificates: []tls.Certificate{cert}}
	s.start()
	return s, pool
}

func TestDialRTMPS(t *testing.T) {
	s, pool := newTLSFakeServer(t)
	url := strings.Replace(s.url("livestream"), "rtmp://", "rtmps://", 1)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	c, err := Dial(ctx, url, &Options{TLS: &tls.Config{RootCAs: pool}})
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()

	st := c.TLS()
	if st == nil || !st.HandshakeComplete {
		t.Fatalf("TLS() = %v, want a complete handshake", st)
	}
	if len(st.PeerCertificates) == 0 || st.PeerCertificates[0].Subject.CommonName != "localhost" {
		t.Errorf("peer certificates %v, want CN localhost", st.PeerCertificates)
	}
	if !c.Handshake.Complex {
		t.Error("the RTMP handshake inside TLS is simple, want complex")
	}
	if c.URL.TcURL != "rtmps://"+s.ln.Addr().String()+"/live" {
		t.Errorf("tcUrl %v", c.URL.TcURL)
	}
	if _, err := c.Publish(ctx, FMLE); err != nil {
		t.Fatalf("publish over TLS: %v", err)
	}
	if s.command("publish") == nil {
		t.Errorf("the server got no publish: %v", s.commandNames())
	}
}

// TestDialRTMPSVerifies checks a client with no config verifies the server:
// the test certificate isn't in the system roots, so the dial fails.
func TestDialRTMPSVerifies(t *testing.T) {
	s, _ := newTLSFakeServer(t)
	url := strings.Replace(s.url("livestream"), "rtmp://", "rtmps://", 1)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	c, err := Dial(ctx, url, nil)
	if err == nil {
		c.Close()
		t.Fatal("dial with no TLS config succeeded, want a certificate error")
	}
	var unknown x509.UnknownAuthorityError
	var verify *tls.CertificateVerificationError
	if !errors.As(err, &unknown) && !errors.As(err, &verify) {
		t.Errorf("dial error %v, want a certificate verification error", err)
	}
}

// TestDialRTMPSServerName checks the server name defaults to the URL's host,
// so a config that trusts the pool verifies the certificate for 127.0.0.1.
func TestDialRTMPSServerName(t *testing.T) {
	s, pool := newTLSFakeServer(t)
	url := strings.Replace(s.url("livestream"), "rtmp://", "rtmps://", 1)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	cfg := &tls.Config{RootCAs: pool}
	c, err := Dial(ctx, url, &Options{TLS: cfg})
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if cfg.ServerName != "" {
		t.Errorf("Dial changed the caller's config: ServerName %q", cfg.ServerName)
	}
	if got := c.TLS().ServerName; got != "" && got != "127.0.0.1" {
		t.Errorf("server name %q", got)
	}
}

// TestDialRTMPSPlainServer checks a TLS client to a plain RTMP server fails
// the dial rather than hanging.
func TestDialRTMPSPlainServer(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	url := strings.Replace(s.url("livestream"), "rtmp://", "rtmps://", 1)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	start := time.Now()
	c, err := Dial(ctx, url, &Options{TLS: &tls.Config{InsecureSkipVerify: true}, Timeout: time.Second})
	if err == nil {
		c.Close()
		t.Fatal("a TLS dial to a plain server succeeded")
	}
	if time.Since(start) > 1500*time.Millisecond {
		t.Errorf("the dial took %v, want it bounded by the timeout", time.Since(start))
	}
}

func TestPlainClientHasNoTLS(t *testing.T) {
	s := newFakeServer(t)
	s.start()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	c, err := Dial(ctx, s.url("livestream"), nil)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if c.TLS() != nil {
		t.Errorf("TLS() = %v for rtmp://, want nil", c.TLS())
	}
}
