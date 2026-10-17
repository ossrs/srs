// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"io"
	"math/big"
	"net"
	"strings"
	"testing"
)

// fakeHandshake is how the fake server answers a C0C1.
type fakeHandshake int

const (
	// fakeLikeSRS answers a signed C1 with a signed S1 and S2, and any other
	// C1 with the simple handshake, like SRS.
	fakeLikeSRS fakeHandshake = iota
	// fakeSimpleOnly always answers with the simple handshake.
	fakeSimpleOnly
	// fakeBadVersion answers with S0 version 6, RTMPE.
	fakeBadVersion
	// fakeBadS2 answers a signed C1 with a signed S1 and a random S2.
	fakeBadS2
)

// serveHandshake is the server side of a handshake, for the tests. It returns
// the C1 it read and whether it answered with the complex handshake.
func serveHandshake(rw io.ReadWriter, how fakeHandshake) (c1 []byte, complex bool, err error) {
	c0c1 := make([]byte, 1537)
	if _, err := io.ReadFull(rw, c0c1); err != nil {
		return nil, false, err
	}
	c1 = c0c1[1:]

	s0s1s2 := make([]byte, 3073)
	rand.Read(s0s1s2)
	s0s1s2[0] = 3
	if how == fakeBadVersion {
		s0s1s2[0] = 6
	}
	s1, s2 := s0s1s2[1:1537], s0s1s2[1537:]

	var digest []byte
	schema := -1
	if how == fakeLikeSRS || how == fakeBadS2 {
		for _, sc := range []int{0, 1} {
			if d, ok := digestValid(c1, sc, fpKey[:30]); ok {
				digest, schema = d, sc
				break
			}
		}
	}
	if schema < 0 {
		copy(s2, c1)
	} else {
		copy(s1[4:8], []byte{0x01, 0x02, 0x03, 0x04})
		signDigest(s1, schema, fmsKey[:36])
		if how != fakeBadS2 {
			signS2(s2, fmsKey, digest)
		}
		complex = true
	}

	if _, err := rw.Write(s0s1s2); err != nil {
		return nil, false, err
	}
	c2 := make([]byte, 1536)
	if _, err := io.ReadFull(rw, c2); err != nil {
		return nil, false, err
	}
	return c1, complex, nil
}

// signS2 makes the S2 of the complex handshake: its last 32 bytes are the
// HMAC of the rest, keyed by the HMAC of the C1 digest.
func signS2(s2, key, c1Digest []byte) {
	m := hmac.New(sha256.New, key)
	m.Write(c1Digest)
	temp := m.Sum(nil)
	m = hmac.New(sha256.New, temp)
	m.Write(s2[:1504])
	copy(s2[1504:], m.Sum(nil))
}

func handshakeOver(t *testing.T, kind Handshake, how fakeHandshake) (HandshakeResult, []byte, bool, error) {
	t.Helper()
	cc, sc := net.Pipe()
	defer cc.Close()
	type served struct {
		c1      []byte
		complex bool
		err     error
	}
	ch := make(chan served, 1)
	go func() {
		defer sc.Close()
		c1, complex, err := serveHandshake(sc, how)
		ch <- served{c1, complex, err}
	}()
	res, err := clientHandshake(cc, kind)
	if err != nil {
		cc.Close()
	}
	s := <-ch
	if err == nil && s.err != nil {
		t.Fatalf("server: %v", s.err)
	}
	return res, s.c1, s.complex, err
}

func TestHandshakeComplex(t *testing.T) {
	res, c1, served, err := handshakeOver(t, Complex, fakeLikeSRS)
	if err != nil {
		t.Fatalf("handshake: %v", err)
	}
	if !served || !res.Complex {
		t.Errorf("server answered complex %v, client saw complex %v; want both", served, res.Complex)
	}
	if _, ok := digestValid(c1, 1, fpKey[:30]); !ok {
		t.Errorf("C1 has no valid digest in schema 1, the schema FMS requires")
	}
	if bytes.Equal(c1[4:8], []byte{0, 0, 0, 0}) {
		t.Errorf("C1 version is zero; a complex C1 needs a version")
	}
}

// TestHandshakeComplexKeyIsDHPublicKey checks the key in the C1 key block is a
// public key of the 1024-bit DH group, which the server computes a shared key
// from.
func TestHandshakeComplexKeyIsDHPublicKey(t *testing.T) {
	c1, err := makeComplexC1()
	if err != nil {
		t.Fatalf("C1: %v", err)
	}
	// Schema 1: the key block is the second 764 bytes, its offset in the
	// last 4 bytes.
	block := c1[8+764 : 8+764+764]
	off := (int(block[760]) + int(block[761]) + int(block[762]) + int(block[763])) % 632
	key := new(big.Int).SetBytes(block[off : off+128])
	p := new(big.Int).Sub(dhPrime, big.NewInt(1))
	if key.Cmp(big.NewInt(1)) <= 0 || key.Cmp(p) >= 0 {
		t.Errorf("key %x is not in (1, p-1)", key)
	}
}

func TestHandshakeSimple(t *testing.T) {
	res, c1, served, err := handshakeOver(t, Simple, fakeLikeSRS)
	if err != nil {
		t.Fatalf("handshake: %v", err)
	}
	if served || res.Complex {
		t.Errorf("server answered complex %v, client saw complex %v; want neither", served, res.Complex)
	}
	if !bytes.Equal(c1[4:8], []byte{0, 0, 0, 0}) {
		t.Errorf("simple C1 version % x, want zero", c1[4:8])
	}
}

// TestHandshakeComplexFallsBack checks a complex C1 that the server answers
// with the simple handshake still succeeds, and reports it.
func TestHandshakeComplexFallsBack(t *testing.T) {
	res, _, _, err := handshakeOver(t, Complex, fakeSimpleOnly)
	if err != nil {
		t.Fatalf("handshake: %v", err)
	}
	if res.Complex {
		t.Errorf("client saw complex, want simple")
	}
}

func TestHandshakeBadVersion(t *testing.T) {
	_, _, _, err := handshakeOver(t, Complex, fakeBadVersion)
	if err == nil || !strings.Contains(err.Error(), "version") {
		t.Errorf("got %v, want an error about the S0 version", err)
	}
}

func TestHandshakeBadS2(t *testing.T) {
	_, _, _, err := handshakeOver(t, Complex, fakeBadS2)
	if err == nil || !strings.Contains(err.Error(), "S2") {
		t.Errorf("got %v, want an error about S2", err)
	}
}

func TestDigestSignAndValidate(t *testing.T) {
	for _, schema := range []int{0, 1} {
		b := make([]byte, 1536)
		rand.Read(b)
		signDigest(b, schema, fpKey[:30])
		if _, ok := digestValid(b, schema, fpKey[:30]); !ok {
			t.Errorf("schema %v: a signed block doesn't validate", schema)
		}
		if _, ok := digestValid(b, schema, fmsKey[:36]); ok {
			t.Errorf("schema %v: validates with the wrong key", schema)
		}
		b[digestOffset(b, schema)] ^= 0xff
		if _, ok := digestValid(b, schema, fpKey[:30]); ok {
			t.Errorf("schema %v: validates after the digest changed", schema)
		}
	}
}

func TestHandshakeResultKind(t *testing.T) {
	if k := (HandshakeResult{Complex: true}).Kind(); k != Complex || k.String() != "complex" {
		t.Errorf("complex result is %v", k)
	}
	if k := (HandshakeResult{}).Kind(); k != Simple || k.String() != "simple" {
		t.Errorf("simple result is %v", k)
	}
}
