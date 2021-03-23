package dtls

import (
	"crypto/elliptic"
	"crypto/rand"

	"golang.org/x/crypto/curve25519"
)

// https://www.iana.org/assignments/tls-parameters/tls-parameters.xml#tls-parameters-8
type namedCurve uint16

type namedCurveKeypair struct {
	curve      namedCurve
	publicKey  []byte
	privateKey []byte
}

const (
	namedCurveP256   namedCurve = 0x0017
	namedCurveP384   namedCurve = 0x0018
	namedCurveX25519 namedCurve = 0x001d
)

func namedCurves() map[namedCurve]bool {
	return map[namedCurve]bool{
		namedCurveX25519: true,
		namedCurveP256:   true,
		namedCurveP384:   true,
	}
}

func generateKeypair(c namedCurve) (*namedCurveKeypair, error) {
	switch c { //nolint:golint
	case namedCurveX25519:
		tmp := make([]byte, 32)
		if _, err := rand.Read(tmp); err != nil {
			return nil, err
		}

		var public, private [32]byte
		copy(private[:], tmp)

		curve25519.ScalarBaseMult(&public, &private)
		return &namedCurveKeypair{namedCurveX25519, public[:], private[:]}, nil
	case namedCurveP256:
		return ellipticCurveKeypair(namedCurveP256, elliptic.P256(), elliptic.P256())
	case namedCurveP384:
		return ellipticCurveKeypair(namedCurveP384, elliptic.P384(), elliptic.P384())
	default:
		return nil, errInvalidNamedCurve
	}
}

func ellipticCurveKeypair(nc namedCurve, c1, c2 elliptic.Curve) (*namedCurveKeypair, error) {
	privateKey, x, y, err := elliptic.GenerateKey(c1, rand.Reader)
	if err != nil {
		return nil, err
	}

	return &namedCurveKeypair{nc, elliptic.Marshal(c2, x, y), privateKey}, nil
}
