package headers

import (
	"encoding/base64"
	"fmt"
	"strings"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
)

// Authorization is an Authorization header.
type Authorization struct {
	// authentication method
	Method AuthMethod

	// username
	Username string

	//
	// Basic authentication fields
	//

	// user
	//
	// Deprecated: replaced by Username.
	BasicUser string

	// password
	BasicPass string

	//
	// Digest authentication fields
	//

	// realm
	Realm string

	// nonce
	Nonce string

	// URI
	URI string

	// response
	Response string

	// opaque
	Opaque *string

	// algorithm
	Algorithm *AuthAlgorithm
}

// Unmarshal decodes an Authorization header.
func (h *Authorization) Unmarshal(v base.HeaderValue) error {
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
		tmp, err := base64.StdEncoding.DecodeString(v0)
		if err != nil {
			return fmt.Errorf("invalid value")
		}

		tmp2 := strings.Split(string(tmp), ":")
		if len(tmp2) != 2 {
			return fmt.Errorf("invalid value")
		}

		h.Username, h.BasicPass = tmp2[0], tmp2[1]
		h.BasicUser = h.Username
	} else { // digest
		kvs, err := keyValParse(v0, ',')
		if err != nil {
			return err
		}

		realmReceived := false
		usernameReceived := false
		nonceReceived := false
		uriReceived := false
		responseReceived := false

		for k, rv := range kvs {
			v := rv

			switch k {
			case "realm":
				h.Realm = v
				realmReceived = true

			case "username":
				h.Username = v
				usernameReceived = true

			case "nonce":
				h.Nonce = v
				nonceReceived = true

			case "uri":
				h.URI = v
				uriReceived = true

			case "response":
				h.Response = v
				responseReceived = true

			case "opaque":
				h.Opaque = &v

			case "algorithm":
				a, err := parseAuthAlgorithm(v)
				if err != nil {
					return err
				}
				h.Algorithm = &a
			}
		}

		if !realmReceived || !usernameReceived || !nonceReceived || !uriReceived || !responseReceived {
			return fmt.Errorf("one or more digest fields are missing")
		}
	}

	return nil
}

// Marshal encodes an Authorization header.
func (h Authorization) Marshal() base.HeaderValue {
	if h.Method == AuthMethodBasic {
		if h.BasicUser != "" {
			h.Username = h.BasicUser
		}
		return base.HeaderValue{"Basic " +
			base64.StdEncoding.EncodeToString([]byte(h.Username+":"+h.BasicPass))}
	}

	ret := "Digest " +
		"username=\"" + h.Username + "\", realm=\"" + h.Realm + "\", " +
		"nonce=\"" + h.Nonce + "\", uri=\"" + h.URI + "\", response=\"" + h.Response + "\""

	if h.Opaque != nil {
		ret += ", opaque=\"" + *h.Opaque + "\""
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
