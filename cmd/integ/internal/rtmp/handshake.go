// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math/big"
	"time"
)

// Handshake is how the client handshakes.
type Handshake int

const (
	// Complex signs C1 with the Flash Player key, like Flash and ffmpeg. A
	// server that can't validate it answers with the simple handshake.
	Complex Handshake = iota
	// Simple sends a random C1 with a zero version.
	Simple
)

func (h Handshake) String() string {
	if h == Simple {
		return "simple"
	}
	return "complex"
}

// HandshakeResult is what the server's answer showed.
type HandshakeResult struct {
	// Complex is set when the server answered with S1 and S2 signed for our
	// C1, and not with the simple handshake.
	Complex bool
}

// fpKey signs the client's C1 and C2: its first 30 bytes for C1, all of it
// for C2. fmsKey signs the server's S1 (36 bytes) and S2.
var (
	fpKey   = append([]byte("Genuine Adobe Flash Player 001"), keyTail...)
	fmsKey  = append([]byte("Genuine Adobe Flash Media Server 001"), keyTail...)
	keyTail = []byte{
		0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8,
		0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
		0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
		0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE,
	}
)

// dhPrime is the 1024-bit MODP group of RFC 2409, whose public keys the C1
// key block carries.
var dhPrime, _ = new(big.Int).SetString(
	"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"+
		"29024E088A67CC74020BBEA63B139B22514A08798E3404DD"+
		"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"+
		"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"+
		"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381"+
		"FFFFFFFFFFFFFFFF", 16)

// The C1 and S1 layout: time (4), version (4), then a 764-byte key block and
// a 764-byte digest block. Schema 0 puts the key block first, schema 1 the
// digest block first.
const (
	c1Size    = 1536
	blockSize = 764
)

func blockStarts(schema int) (key, digest int) {
	if schema == 0 {
		return 8, 8 + blockSize
	}
	return 8 + blockSize, 8
}

// digestOffset is where the 32-byte digest is in a C1 or S1: the digest block
// starts with 4 bytes whose sum, modulo 728, places it.
func digestOffset(b []byte, schema int) int {
	_, d := blockStarts(schema)
	sum := int(b[d]) + int(b[d+1]) + int(b[d+2]) + int(b[d+3])
	return d + 4 + sum%(blockSize-32-4)
}

// keyOffset is where the 128-byte key is: the key block ends with 4 bytes
// whose sum, modulo 632, places it.
func keyOffset(b []byte, schema int) int {
	k, _ := blockStarts(schema)
	e := k + blockSize - 4
	sum := int(b[e]) + int(b[e+1]) + int(b[e+2]) + int(b[e+3])
	return k + sum%(blockSize-128-4)
}

// calcDigest is the HMAC-SHA256 of a C1 or S1 without its digest.
func calcDigest(b []byte, schema int, key []byte) []byte {
	off := digestOffset(b, schema)
	m := hmac.New(sha256.New, key)
	m.Write(b[:off])
	m.Write(b[off+32:])
	return m.Sum(nil)
}

// signDigest puts the digest into a C1 or S1.
func signDigest(b []byte, schema int, key []byte) {
	copy(b[digestOffset(b, schema):], calcDigest(b, schema, key))
}

// digestValid returns the digest of a C1 or S1, and whether it is signed
// with key.
func digestValid(b []byte, schema int, key []byte) ([]byte, bool) {
	off := digestOffset(b, schema)
	digest := b[off : off+32]
	return digest, hmac.Equal(digest, calcDigest(b, schema, key))
}

// makeComplexC1 makes a schema 1 C1, the schema FMS requires, with a DH
// public key in its key block and signed with the Flash Player key.
func makeComplexC1() ([]byte, error) {
	c1 := make([]byte, c1Size)
	if _, err := rand.Read(c1); err != nil {
		return nil, err
	}
	binary.BigEndian.PutUint32(c1[0:4], uint32(time.Now().Unix()))
	copy(c1[4:8], []byte{0x80, 0x00, 0x07, 0x02})

	x, err := rand.Int(rand.Reader, new(big.Int).Sub(dhPrime, big.NewInt(3)))
	if err != nil {
		return nil, err
	}
	pub := new(big.Int).Exp(big.NewInt(2), x.Add(x, big.NewInt(2)), dhPrime)
	pub.FillBytes(c1[keyOffset(c1, 1) : keyOffset(c1, 1)+128])

	signDigest(c1, 1, fpKey[:30])
	return c1, nil
}

// clientHandshake runs the client side of the handshake.
func clientHandshake(rw io.ReadWriter, kind Handshake) (HandshakeResult, error) {
	var c1 []byte
	if kind == Complex {
		var err error
		if c1, err = makeComplexC1(); err != nil {
			return HandshakeResult{}, err
		}
	} else {
		c1 = make([]byte, c1Size)
		if _, err := rand.Read(c1); err != nil {
			return HandshakeResult{}, err
		}
		binary.BigEndian.PutUint32(c1[0:4], uint32(time.Now().Unix()))
		copy(c1[4:8], []byte{0, 0, 0, 0})
	}
	if _, err := rw.Write(append([]byte{3}, c1...)); err != nil {
		return HandshakeResult{}, fmt.Errorf("write C0C1: %w", err)
	}

	s0s1s2 := make([]byte, 1+2*c1Size)
	if _, err := io.ReadFull(rw, s0s1s2); err != nil {
		return HandshakeResult{}, fmt.Errorf("read S0S1S2: %w", err)
	}
	if s0s1s2[0] != 3 {
		return HandshakeResult{}, fmt.Errorf("S0 version %v, want 3, plain RTMP", s0s1s2[0])
	}
	s1, s2 := s0s1s2[1:1+c1Size], s0s1s2[1+c1Size:]

	var res HandshakeResult
	c2 := s1
	if kind == Complex {
		if s1Digest, ok := digestValid(s1, 1, fmsKey[:36]); ok {
			c1Digest := c1[digestOffset(c1, 1) : digestOffset(c1, 1)+32]
			if !s2Valid(s2, c1Digest) {
				return HandshakeResult{}, errors.New("S2 is not signed for C1")
			}
			res.Complex = true
			c2 = makeC2(s1Digest)
		}
	}
	if _, err := rw.Write(c2); err != nil {
		return HandshakeResult{}, fmt.Errorf("write C2: %w", err)
	}
	return res, nil
}

// s2Valid checks the S2 of the complex handshake: its last 32 bytes are the
// HMAC of the rest, keyed by the HMAC of the C1 digest with the server key.
func s2Valid(s2, c1Digest []byte) bool {
	return hmac.Equal(s2[c1Size-32:], signRandom(s2[:c1Size-32], fmsKey, c1Digest))
}

// makeC2 makes the C2 of the complex handshake, signed for the S1 digest.
func makeC2(s1Digest []byte) []byte {
	c2 := make([]byte, c1Size)
	rand.Read(c2[:c1Size-32])
	copy(c2[c1Size-32:], signRandom(c2[:c1Size-32], fpKey, s1Digest))
	return c2
}

func signRandom(random, key, digest []byte) []byte {
	m := hmac.New(sha256.New, key)
	m.Write(digest)
	m = hmac.New(sha256.New, m.Sum(nil))
	m.Write(random)
	return m.Sum(nil)
}

// Kind is the handshake the server answered with, complex or simple.
func (r HandshakeResult) Kind() Handshake {
	if r.Complex {
		return Complex
	}
	return Simple
}
