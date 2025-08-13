package auth

import (
	"github.com/bluenviron/gortsplib/v4/pkg/base"
)

// ValidateMethod is a validation method.
//
// Deprecated: replaced by VerifyMethod
type ValidateMethod = VerifyMethod

// validation methods.
//
// Deprecated.
const (
	ValidateMethodBasic     = VerifyMethodBasic
	ValidateMethodDigestMD5 = VerifyMethodDigestMD5
	ValidateMethodSHA256    = VerifyMethodDigestSHA256
)

// Validate validates a request sent by a client.
//
// Deprecated: replaced by Verify.
func Validate(
	req *base.Request,
	user string,
	pass string,
	methods []ValidateMethod,
	realm string,
	nonce string,
) error {
	return Verify(req, user, pass, methods, realm, nonce)
}
