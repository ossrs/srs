package dtls

import ( //nolint:gci
	"crypto/elliptic"
	"crypto/hmac"
	"crypto/sha1" //nolint:gosec
	"encoding/binary"
	"fmt"
	"hash"
	"math"

	"golang.org/x/crypto/curve25519"
)

const (
	prfMasterSecretLabel         = "master secret"
	prfExtendedMasterSecretLabel = "extended master secret"
	prfKeyExpansionLabel         = "key expansion"
	prfVerifyDataClientLabel     = "client finished"
	prfVerifyDataServerLabel     = "server finished"
)

type hashFunc func() hash.Hash

type encryptionKeys struct {
	masterSecret   []byte
	clientMACKey   []byte
	serverMACKey   []byte
	clientWriteKey []byte
	serverWriteKey []byte
	clientWriteIV  []byte
	serverWriteIV  []byte
}

func (e *encryptionKeys) String() string {
	return fmt.Sprintf(`encryptionKeys:
- masterSecret: %#v
- clientMACKey: %#v
- serverMACKey: %#v
- clientWriteKey: %#v
- serverWriteKey: %#v
- clientWriteIV: %#v
- serverWriteIV: %#v
`,
		e.masterSecret,
		e.clientMACKey,
		e.serverMACKey,
		e.clientWriteKey,
		e.serverWriteKey,
		e.clientWriteIV,
		e.serverWriteIV)
}

// The premaster secret is formed as follows: if the PSK is N octets
// long, concatenate a uint16 with the value N, N zero octets, a second
// uint16 with the value N, and the PSK itself.
//
// https://tools.ietf.org/html/rfc4279#section-2
func prfPSKPreMasterSecret(psk []byte) []byte {
	pskLen := uint16(len(psk))

	out := append(make([]byte, 2+pskLen+2), psk...)
	binary.BigEndian.PutUint16(out, pskLen)
	binary.BigEndian.PutUint16(out[2+pskLen:], pskLen)

	return out
}

func prfPreMasterSecret(publicKey, privateKey []byte, curve namedCurve) ([]byte, error) {
	switch curve {
	case namedCurveX25519:
		return curve25519.X25519(privateKey, publicKey)
	case namedCurveP256:
		return ellipticCurvePreMasterSecret(publicKey, privateKey, elliptic.P256(), elliptic.P256())
	case namedCurveP384:
		return ellipticCurvePreMasterSecret(publicKey, privateKey, elliptic.P384(), elliptic.P384())
	default:
		return nil, errInvalidNamedCurve
	}
}

func ellipticCurvePreMasterSecret(publicKey, privateKey []byte, c1, c2 elliptic.Curve) ([]byte, error) {
	x, y := elliptic.Unmarshal(c1, publicKey)
	if x == nil || y == nil {
		return nil, errInvalidNamedCurve
	}

	result, _ := c2.ScalarMult(x, y, privateKey)
	preMasterSecret := make([]byte, (c2.Params().BitSize+7)>>3)
	resultBytes := result.Bytes()
	copy(preMasterSecret[len(preMasterSecret)-len(resultBytes):], resultBytes)
	return preMasterSecret, nil
}

//  This PRF with the SHA-256 hash function is used for all cipher suites
//  defined in this document and in TLS documents published prior to this
//  document when TLS 1.2 is negotiated.  New cipher suites MUST explicitly
//  specify a PRF and, in general, SHOULD use the TLS PRF with SHA-256 or a
//  stronger standard hash function.
//
//     P_hash(secret, seed) = HMAC_hash(secret, A(1) + seed) +
//                            HMAC_hash(secret, A(2) + seed) +
//                            HMAC_hash(secret, A(3) + seed) + ...
//
//  A() is defined as:
//
//     A(0) = seed
//     A(i) = HMAC_hash(secret, A(i-1))
//
//  P_hash can be iterated as many times as necessary to produce the
//  required quantity of data.  For example, if P_SHA256 is being used to
//  create 80 bytes of data, it will have to be iterated three times
//  (through A(3)), creating 96 bytes of output data; the last 16 bytes
//  of the final iteration will then be discarded, leaving 80 bytes of
//  output data.
//
// https://tools.ietf.org/html/rfc4346w
func prfPHash(secret, seed []byte, requestedLength int, h hashFunc) ([]byte, error) {
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

func prfExtendedMasterSecret(preMasterSecret, sessionHash []byte, h hashFunc) ([]byte, error) {
	seed := append([]byte(prfExtendedMasterSecretLabel), sessionHash...)
	return prfPHash(preMasterSecret, seed, 48, h)
}

func prfMasterSecret(preMasterSecret, clientRandom, serverRandom []byte, h hashFunc) ([]byte, error) {
	seed := append(append([]byte(prfMasterSecretLabel), clientRandom...), serverRandom...)
	return prfPHash(preMasterSecret, seed, 48, h)
}

func prfEncryptionKeys(masterSecret, clientRandom, serverRandom []byte, prfMacLen, prfKeyLen, prfIvLen int, h hashFunc) (*encryptionKeys, error) {
	seed := append(append([]byte(prfKeyExpansionLabel), serverRandom...), clientRandom...)
	keyMaterial, err := prfPHash(masterSecret, seed, (2*prfMacLen)+(2*prfKeyLen)+(2*prfIvLen), h)
	if err != nil {
		return nil, err
	}

	clientMACKey := keyMaterial[:prfMacLen]
	keyMaterial = keyMaterial[prfMacLen:]

	serverMACKey := keyMaterial[:prfMacLen]
	keyMaterial = keyMaterial[prfMacLen:]

	clientWriteKey := keyMaterial[:prfKeyLen]
	keyMaterial = keyMaterial[prfKeyLen:]

	serverWriteKey := keyMaterial[:prfKeyLen]
	keyMaterial = keyMaterial[prfKeyLen:]

	clientWriteIV := keyMaterial[:prfIvLen]
	keyMaterial = keyMaterial[prfIvLen:]

	serverWriteIV := keyMaterial[:prfIvLen]

	return &encryptionKeys{
		masterSecret:   masterSecret,
		clientMACKey:   clientMACKey,
		serverMACKey:   serverMACKey,
		clientWriteKey: clientWriteKey,
		serverWriteKey: serverWriteKey,
		clientWriteIV:  clientWriteIV,
		serverWriteIV:  serverWriteIV,
	}, nil
}

func prfVerifyData(masterSecret, handshakeBodies []byte, label string, hashFunc hashFunc) ([]byte, error) {
	h := hashFunc()
	if _, err := h.Write(handshakeBodies); err != nil {
		return nil, err
	}

	seed := append([]byte(label), h.Sum(nil)...)
	return prfPHash(masterSecret, seed, 12, hashFunc)
}

func prfVerifyDataClient(masterSecret, handshakeBodies []byte, h hashFunc) ([]byte, error) {
	return prfVerifyData(masterSecret, handshakeBodies, prfVerifyDataClientLabel, h)
}

func prfVerifyDataServer(masterSecret, handshakeBodies []byte, h hashFunc) ([]byte, error) {
	return prfVerifyData(masterSecret, handshakeBodies, prfVerifyDataServerLabel, h)
}

// compute the MAC using HMAC-SHA1
func prfMac(epoch uint16, sequenceNumber uint64, contentType contentType, protocolVersion protocolVersion, payload []byte, key []byte) ([]byte, error) {
	h := hmac.New(sha1.New, key)

	msg := make([]byte, 13)

	binary.BigEndian.PutUint16(msg, epoch)
	putBigEndianUint48(msg[2:], sequenceNumber)
	msg[8] = byte(contentType)
	msg[9] = protocolVersion.major
	msg[10] = protocolVersion.minor
	binary.BigEndian.PutUint16(msg[11:], uint16(len(payload)))

	if _, err := h.Write(msg); err != nil {
		return nil, err
	} else if _, err := h.Write(payload); err != nil {
		return nil, err
	}

	return h.Sum(nil), nil
}
