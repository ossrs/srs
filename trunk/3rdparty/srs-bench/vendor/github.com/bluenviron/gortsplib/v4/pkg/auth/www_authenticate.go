package auth

import (
	"github.com/bluenviron/gortsplib/v4/pkg/base"
	"github.com/bluenviron/gortsplib/v4/pkg/headers"
)

// GenerateWWWAuthenticate generates a WWW-Authenticate header.
func GenerateWWWAuthenticate(methods []ValidateMethod, realm string, nonce string) base.HeaderValue {
	if methods == nil {
		// disable VerifyMethodDigestSHA256 unless explicitly set
		// since it prevents FFmpeg from authenticating
		methods = []VerifyMethod{VerifyMethodBasic, VerifyMethodDigestMD5}
	}

	var ret base.HeaderValue

	for _, m := range methods {
		var a base.HeaderValue

		switch m {
		case ValidateMethodBasic:
			a = headers.Authenticate{
				Method: headers.AuthMethodBasic,
				Realm:  realm,
			}.Marshal()

		case ValidateMethodDigestMD5:
			aa := headers.AuthAlgorithmMD5
			a = headers.Authenticate{
				Method:    headers.AuthMethodDigest,
				Realm:     realm,
				Nonce:     nonce,
				Algorithm: &aa,
			}.Marshal()

		default: // sha256
			aa := headers.AuthAlgorithmSHA256
			a = headers.Authenticate{
				Method:    headers.AuthMethodDigest,
				Realm:     realm,
				Nonce:     nonce,
				Algorithm: &aa,
			}.Marshal()
		}

		ret = append(ret, a...)
	}

	return ret
}
