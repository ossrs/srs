// Package headers contains various RTSP headers.
package headers

import (
	"fmt"
	"strings"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
)

// AuthMethod is an authentication method.
type AuthMethod int

// authentication methods.
const (
	AuthMethodBasic AuthMethod = iota
	AuthMethodDigest
)

// AuthAlgorithm is a digest algorithm.
type AuthAlgorithm int

// digest algorithms.
const (
	AuthAlgorithmMD5 AuthAlgorithm = iota
	AuthAlgorithmSHA256
)

func parseAuthAlgorithm(v string) (AuthAlgorithm, error) {
	switch {
	case strings.ToLower(v) == "md5":
		return AuthAlgorithmMD5, nil

	case strings.ToLower(v) == "sha-256":
		return AuthAlgorithmSHA256, nil

	default:
		return 0, fmt.Errorf("unrecognized algorithm: %v", v)
	}
}

// Authenticate is a WWW-Authenticate header.
type Authenticate struct {
	// authentication method
	Method AuthMethod

	// realm
	Realm string

	//
	// Digest authentication fields
	//

	// nonce
	Nonce string

	// opaque
	Opaque *string

	// stale
	Stale *string

	// algorithm
	Algorithm *AuthAlgorithm
}

// Unmarshal decodes a WWW-Authenticate header.
func (h *Authenticate) Unmarshal(v base.HeaderValue) error {
	if len(v) == 0 {
		return fmt.Errorf("value not provided")
	}

	if len(v) > 1 {
		return fmt.Errorf("value provided multiple times (%v)", v)
	}

	v0 := v[0]

	i := strings.IndexByte(v0, ' ')
	if i < 0 {
		return fmt.Errorf("unable to split between method and keys (%v)", v0)
	}
	method, v0 := v0[:i], v0[i+1:]

	switch method {
	case "Basic":
		h.Method = AuthMethodBasic

	case "Digest":
		h.Method = AuthMethodDigest

	default:
		return fmt.Errorf("invalid method (%s)", method)
	}

	if h.Method == AuthMethodBasic {
		kvs, err := keyValParse(v0, ',')
		if err != nil {
			return err
		}

		realmReceived := false

		for k, rv := range kvs {
			v := rv

			if k == "realm" {
				h.Realm = v
				realmReceived = true
			}
		}

		if !realmReceived {
			return fmt.Errorf("realm is missing")
		}
	} else { // digest
		kvs, err := keyValParse(v0, ',')
		if err != nil {
			return err
		}

		realmReceived := false
		nonceReceived := false

		for k, rv := range kvs {
			v := rv

			switch k {
			case "realm":
				h.Realm = v
				realmReceived = true

			case "nonce":
				h.Nonce = v
				nonceReceived = true

			case "opaque":
				h.Opaque = &v

			case "stale":
				h.Stale = &v

			case "algorithm":
				a, err := parseAuthAlgorithm(v)
				if err != nil {
					return err
				}
				h.Algorithm = &a
			}
		}

		if !realmReceived || !nonceReceived {
			return fmt.Errorf("one or more digest fields are missing")
		}
	}

	return nil
}

// Marshal encodes a WWW-Authenticate header.
func (h Authenticate) Marshal() base.HeaderValue {
	if h.Method == AuthMethodBasic {
		return base.HeaderValue{"Basic " +
			"realm=\"" + h.Realm + "\""}
	}

	ret := "Digest realm=\"" + h.Realm + "\", nonce=\"" + h.Nonce + "\""

	if h.Opaque != nil {
		ret += ", opaque=\"" + *h.Opaque + "\""
	}

	if h.Stale != nil {
		ret += ", stale=\"" + *h.Stale + "\""
	}

	if h.Algorithm != nil {
		if *h.Algorithm == AuthAlgorithmMD5 {
			ret += ", algorithm=\"MD5\""
		} else {
			ret += ", algorithm=\"SHA-256\""
		}
	}

	return base.HeaderValue{ret}
}
