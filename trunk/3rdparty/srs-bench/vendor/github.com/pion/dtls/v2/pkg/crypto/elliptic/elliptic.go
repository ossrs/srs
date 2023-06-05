// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package elliptic provides elliptic curve cryptography for DTLS
package elliptic

import (
	"crypto/elliptic"
	"crypto/rand"
	"errors"
	"fmt"

	"golang.org/x/crypto/curve25519"
)

var errInvalidNamedCurve = errors.New("invalid named curve")

// CurvePointFormat is used to represent the IANA registered curve points
//
// https://www.iana.org/assignments/tls-parameters/tls-parameters.xml#tls-parameters-9
type CurvePointFormat byte

// CurvePointFormat enums
const (
	CurvePointFormatUncompressed CurvePointFormat = 0
)

// Keypair is a Curve with a Private/Public Keypair
type Keypair struct {
	Curve      Curve
	PublicKey  []byte
	PrivateKey []byte
}

// CurveType is used to represent the IANA registered curve types for TLS
//
// https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-10
type CurveType byte

// CurveType enums
const (
	CurveTypeNamedCurve CurveType = 0x03
)

// CurveTypes returns all known curves
func CurveTypes() map[CurveType]struct{} {
	return map[CurveType]struct{}{
		CurveTypeNamedCurve: {},
	}
}

// Curve is used to represent the IANA registered curves for TLS
//
// https://www.iana.org/assignments/tls-parameters/tls-parameters.xml#tls-parameters-8
type Curve uint16

// Curve enums
const (
	P256   Curve = 0x0017
	P384   Curve = 0x0018
	X25519 Curve = 0x001d
)

func (c Curve) String() string {
	switch c {
	case P256:
		return "P-256"
	case P384:
		return "P-384"
	case X25519:
		return "X25519"
	}
	return fmt.Sprintf("%#x", uint16(c))
}

// Curves returns all curves we implement
func Curves() map[Curve]bool {
	return map[Curve]bool{
		X25519: true,
		P256:   true,
		P384:   true,
	}
}

// GenerateKeypair generates a keypair for the given Curve
func GenerateKeypair(c Curve) (*Keypair, error) {
	switch c { //nolint:revive
	case X25519:
		tmp := make([]byte, 32)
		if _, err := rand.Read(tmp); err != nil {
			return nil, err
		}

		var public, private [32]byte
		copy(private[:], tmp)

		curve25519.ScalarBaseMult(&public, &private)
		return &Keypair{X25519, public[:], private[:]}, nil
	case P256:
		return ellipticCurveKeypair(P256, elliptic.P256(), elliptic.P256())
	case P384:
		return ellipticCurveKeypair(P384, elliptic.P384(), elliptic.P384())
	default:
		return nil, errInvalidNamedCurve
	}
}

func ellipticCurveKeypair(nc Curve, c1, c2 elliptic.Curve) (*Keypair, error) {
	privateKey, x, y, err := elliptic.GenerateKey(c1, rand.Reader)
	if err != nil {
		return nil, err
	}

	return &Keypair{nc, elliptic.Marshal(c2, x, y), privateKey}, nil
}
