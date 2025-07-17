package auth

import (
	"fmt"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
	"github.com/bluenviron/gortsplib/v4/pkg/headers"
)

// NewSender allocates a Sender.
//
// Deprecated: replaced by Sender.Initialize().
func NewSender(wwwAuth base.HeaderValue, user string, pass string) (*Sender, error) {
	s := &Sender{
		WWWAuth: wwwAuth,
		User:    user,
		Pass:    pass,
	}
	err := s.Initialize()
	return s, err
}

// Sender allows to send credentials.
// It requires a WWW-Authenticate header (provided by the server)
// and a set of credentials.
type Sender struct {
	WWWAuth base.HeaderValue
	User    string
	Pass    string

	authHeader *headers.Authenticate
}

// Initialize initializes a Sender.
func (se *Sender) Initialize() error {
	for _, v := range se.WWWAuth {
		var auth headers.Authenticate
		err := auth.Unmarshal(base.HeaderValue{v})
		if err != nil {
			continue // ignore unrecognized headers
		}

		if se.authHeader == nil ||
			(auth.Algorithm != nil && *auth.Algorithm == headers.AuthAlgorithmSHA256) ||
			(se.authHeader.Method == headers.AuthMethodBasic) {
			se.authHeader = &auth
		}
	}

	if se.authHeader == nil {
		return fmt.Errorf("no authentication methods available")
	}

	return nil
}

// AddAuthorization adds the Authorization header to a Request.
func (se *Sender) AddAuthorization(req *base.Request) {
	urStr := req.URL.CloneWithoutCredentials().String()

	h := headers.Authorization{
		Method: se.authHeader.Method,
	}

	h.Username = se.User

	if se.authHeader.Method == headers.AuthMethodBasic {
		h.BasicPass = se.Pass
	} else { // digest
		h.Realm = se.authHeader.Realm
		h.Nonce = se.authHeader.Nonce
		h.URI = urStr
		h.Algorithm = se.authHeader.Algorithm

		if se.authHeader.Algorithm == nil || *se.authHeader.Algorithm == headers.AuthAlgorithmMD5 {
			h.Response = md5Hex(md5Hex(se.User+":"+se.authHeader.Realm+":"+se.Pass) + ":" +
				se.authHeader.Nonce + ":" + md5Hex(string(req.Method)+":"+urStr))
		} else { // sha256
			h.Response = sha256Hex(sha256Hex(se.User+":"+se.authHeader.Realm+":"+se.Pass) + ":" +
				se.authHeader.Nonce + ":" + sha256Hex(string(req.Method)+":"+urStr))
		}
	}

	if req.Header == nil {
		req.Header = make(base.Header)
	}

	req.Header["Authorization"] = h.Marshal()
}
