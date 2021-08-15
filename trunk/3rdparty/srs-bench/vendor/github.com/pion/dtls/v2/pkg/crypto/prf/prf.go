// Package prf implements TLS 1.2 Pseudorandom functions
package prf

import ( //nolint:gci
	ellipticStdlib "crypto/elliptic"
	"crypto/hmac"
	"encoding/binary"
	"errors"
	"fmt"
	"hash"
	"math"

	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
	"github.com/pion/dtls/v2/pkg/protocol"
	"golang.org/x/crypto/curve25519"
)

const (
	masterSecretLabel         = "master secret"
	extendedMasterSecretLabel = "extended master secret"
	keyExpansionLabel         = "key expansion"
	verifyDataClientLabel     = "client finished"
	verifyDataServerLabel     = "server finished"
)

// HashFunc allows callers to decide what hash is used in PRF
type HashFunc func() hash.Hash

// EncryptionKeys is all the state needed for a TLS CipherSuite
type EncryptionKeys struct {
	MasterSecret   []byte
	ClientMACKey   []byte
	ServerMACKey   []byte
	ClientWriteKey []byte
	ServerWriteKey []byte
	ClientWriteIV  []byte
	ServerWriteIV  []byte
}

var errInvalidNamedCurve = &protocol.FatalError{Err: errors.New("invalid named curve")} //nolint:goerr113

func (e *EncryptionKeys) String() string {
	return fmt.Sprintf(`encryptionKeys:
- masterSecret: %#v
- clientMACKey: %#v
- serverMACKey: %#v
- clientWriteKey: %#v
- serverWriteKey: %#v
- clientWriteIV: %#v
- serverWriteIV: %#v
`,
		e.MasterSecret,
		e.ClientMACKey,
		e.ServerMACKey,
		e.ClientWriteKey,
		e.ServerWriteKey,
		e.ClientWriteIV,
		e.ServerWriteIV)
}

// PSKPreMasterSecret generates the PSK Premaster Secret
// The premaster secret is formed as follows: if the PSK is N octets
// long, concatenate a uint16 with the value N, N zero octets, a second
// uint16 with the value N, and the PSK itself.
//
// https://tools.ietf.org/html/rfc4279#section-2
func PSKPreMasterSecret(psk []byte) []byte {
	pskLen := uint16(len(psk))

	out := append(make([]byte, 2+pskLen+2), psk...)
	binary.BigEndian.PutUint16(out, pskLen)
	binary.BigEndian.PutUint16(out[2+pskLen:], pskLen)

	return out
}

// PreMasterSecret implements TLS 1.2 Premaster Secret generation given a keypair and a curve
func PreMasterSecret(publicKey, privateKey []byte, curve elliptic.Curve) ([]byte, error) {
	switch curve {
	case elliptic.X25519:
		return curve25519.X25519(privateKey, publicKey)
	case elliptic.P256:
		return ellipticCurvePreMasterSecret(publicKey, privateKey, ellipticStdlib.P256(), ellipticStdlib.P256())
	case elliptic.P384:
		return ellipticCurvePreMasterSecret(publicKey, privateKey, ellipticStdlib.P384(), ellipticStdlib.P384())
	default:
		return nil, errInvalidNamedCurve
	}
}

func ellipticCurvePreMasterSecret(publicKey, privateKey []byte, c1, c2 ellipticStdlib.Curve) ([]byte, error) {
	x, y := ellipticStdlib.Unmarshal(c1, publicKey)
	if x == nil || y == nil {
		return nil, errInvalidNamedCurve
	}

	result, _ := c2.ScalarMult(x, y, privateKey)
	preMasterSecret := make([]byte, (c2.Params().BitSize+7)>>3)
	resultBytes := result.Bytes()
	copy(preMasterSecret[len(preMasterSecret)-len(resultBytes):], resultBytes)
	return preMasterSecret, nil
}

// PHash is PRF is the SHA-256 hash function is used for all cipher suites
// defined in this TLS 1.2 document and in TLS documents published prior to this
// document when TLS 1.2 is negotiated.  New cipher suites MUST explicitly
// specify a PRF and, in general, SHOULD use the TLS PRF with SHA-256 or a
// stronger standard hash function.
//
//    P_hash(secret, seed) = HMAC_hash(secret, A(1) + seed) +
//                           HMAC_hash(secret, A(2) + seed) +
//                           HMAC_hash(secret, A(3) + seed) + ...
//
// A() is defined as:
//
//    A(0) = seed
//    A(i) = HMAC_hash(secret, A(i-1))
//
// P_hash can be iterated as many times as necessary to produce the
// required quantity of data.  For example, if P_SHA256 is being used to
// create 80 bytes of data, it will have to be iterated three times
// (through A(3)), creating 96 bytes of output data; the last 16 bytes
// of the final iteration will then be discarded, leaving 80 bytes of
// output data.
//
// https://tools.ietf.org/html/rfc4346w
func PHash(secret, seed []byte, requestedLength int, h HashFunc) ([]byte, error) {
	hmacSHA256 := func(key, data []byte) ([]byte, error) {
		mac := hmac.New(h, key)
		if _, err := mac.Write(data); err != nil {
			return nil, err
		}
		return mac.Sum(nil), nil
	}

	var err error
	lastRound := seed
	out := []byte{}

	iterations := int(math.Ceil(float64(requestedLength) / float64(h().Size())))
	for i := 0; i < iterations; i++ {
		lastRound, err = hmacSHA256(secret, lastRound)
		if err != nil {
			return nil, err
		}
		withSecret, err := hmacSHA256(secret, append(lastRound, seed...))
		if err != nil {
			return nil, err
		}
		out = append(out, withSecret...)
	}

	return out[:requestedLength], nil
}

// ExtendedMasterSecret generates a Extended MasterSecret as defined in
// https://tools.ietf.org/html/rfc7627
func ExtendedMasterSecret(preMasterSecret, sessionHash []byte, h HashFunc) ([]byte, error) {
	seed := append([]byte(extendedMasterSecretLabel), sessionHash...)
	return PHash(preMasterSecret, seed, 48, h)
}

// MasterSecret generates a TLS 1.2 MasterSecret
func MasterSecret(preMasterSecret, clientRandom, serverRandom []byte, h HashFunc) ([]byte, error) {
	seed := append(append([]byte(masterSecretLabel), clientRandom...), serverRandom...)
	return PHash(preMasterSecret, seed, 48, h)
}

// GenerateEncryptionKeys is the final step TLS 1.2 PRF. Given all state generated so far generates
// the final keys need for encryption
func GenerateEncryptionKeys(masterSecret, clientRandom, serverRandom []byte, macLen, keyLen, ivLen int, h HashFunc) (*EncryptionKeys, error) {
	seed := append(append([]byte(keyExpansionLabel), serverRandom...), clientRandom...)
	keyMaterial, err := PHash(masterSecret, seed, (2*macLen)+(2*keyLen)+(2*ivLen), h)
	if err != nil {
		return nil, err
	}

	clientMACKey := keyMaterial[:macLen]
	keyMaterial = keyMaterial[macLen:]

	serverMACKey := keyMaterial[:macLen]
	keyMaterial = keyMaterial[macLen:]

	clientWriteKey := keyMaterial[:keyLen]
	keyMaterial = keyMaterial[keyLen:]

	serverWriteKey := keyMaterial[:keyLen]
	keyMaterial = keyMaterial[keyLen:]

	clientWriteIV := keyMaterial[:ivLen]
	keyMaterial = keyMaterial[ivLen:]

	serverWriteIV := keyMaterial[:ivLen]

	return &EncryptionKeys{
		MasterSecret:   masterSecret,
		ClientMACKey:   clientMACKey,
		ServerMACKey:   serverMACKey,
		ClientWriteKey: clientWriteKey,
		ServerWriteKey: serverWriteKey,
		ClientWriteIV:  clientWriteIV,
		ServerWriteIV:  serverWriteIV,
	}, nil
}

func prfVerifyData(masterSecret, handshakeBodies []byte, label string, hashFunc HashFunc) ([]byte, error) {
	h := hashFunc()
	if _, err := h.Write(handshakeBodies); err != nil {
		return nil, err
	}

	seed := append([]byte(label), h.Sum(nil)...)
	return PHash(masterSecret, seed, 12, hashFunc)
}

// VerifyDataClient is caled on the Client Side to either verify or generate the VerifyData message
func VerifyDataClient(masterSecret, handshakeBodies []byte, h HashFunc) ([]byte, error) {
	return prfVerifyData(masterSecret, handshakeBodies, verifyDataClientLabel, h)
}

// VerifyDataServer is caled on the Server Side to either verify or generate the VerifyData message
func VerifyDataServer(masterSecret, handshakeBodies []byte, h HashFunc) ([]byte, error) {
	return prfVerifyData(masterSecret, handshakeBodies, verifyDataServerLabel, h)
}
