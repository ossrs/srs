package sip

import (
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"regexp"
	"strings"
)

// currently only Digest and MD5
type Authorization struct {
	realm     string
	nonce     string
	algorithm string
	username  string
	password  string
	uri       string
	response  string
	method    string
	qop       string
	nc        string
	cnonce    string
	other     map[string]string
}

func AuthFromValue(value string) *Authorization {
	auth := &Authorization{
		algorithm: "MD5",
		other:     make(map[string]string),
	}

	re := regexp.MustCompile(`([\w]+)="([^"]+)"`)
	matches := re.FindAllStringSubmatch(value, -1)
	for _, match := range matches {
		switch match[1] {
		case "realm":
			auth.realm = match[2]
		case "algorithm":
			auth.algorithm = match[2]
		case "nonce":
			auth.nonce = match[2]
		case "username":
			auth.username = match[2]
		case "uri":
			auth.uri = match[2]
		case "response":
			auth.response = match[2]
		case "qop":
			for _, v := range strings.Split(match[2], ",") {
				v = strings.Trim(v, " ")
				if v == "auth" || v == "auth-int" {
					auth.qop = "auth"
					break
				}
			}
		case "nc":
			auth.nc = match[2]
		case "cnonce":
			auth.cnonce = match[2]
		default:
			auth.other[match[1]] = match[2]
		}
	}

	return auth
}

func (auth *Authorization) Realm() string {
	return auth.realm
}

func (auth *Authorization) Nonce() string {
	return auth.nonce
}

func (auth *Authorization) Algorithm() string {
	return auth.algorithm
}

func (auth *Authorization) Username() string {
	return auth.username
}

func (auth *Authorization) SetUsername(username string) *Authorization {
	auth.username = username

	return auth
}

func (auth *Authorization) SetPassword(password string) *Authorization {
	auth.password = password

	return auth
}

func (auth *Authorization) Uri() string {
	return auth.uri
}

func (auth *Authorization) SetUri(uri string) *Authorization {
	auth.uri = uri

	return auth
}

func (auth *Authorization) SetMethod(method string) *Authorization {
	auth.method = method

	return auth
}

func (auth *Authorization) Response() string {
	return auth.response
}

func (auth *Authorization) SetResponse(response string) {
	auth.response = response
}

func (auth *Authorization) Qop() string {
	return auth.qop
}

func (auth *Authorization) SetQop(qop string) {
	auth.qop = qop
}

func (auth *Authorization) Nc() string {
	return auth.nc
}

func (auth *Authorization) SetNc(nc string) {
	auth.nc = nc
}

func (auth *Authorization) CNonce() string {
	return auth.cnonce
}

func (auth *Authorization) SetCNonce(cnonce string) {
	auth.cnonce = cnonce
}

func (auth *Authorization) CalcResponse() string {
	return calcResponse(
		auth.username,
		auth.realm,
		auth.password,
		auth.method,
		auth.uri,
		auth.nonce,
		auth.qop,
		auth.cnonce,
		auth.nc,
	)
}

func (auth *Authorization) String() string {
	if auth == nil {
		return "<nil>"
	}

	str := fmt.Sprintf(
		`Digest realm="%s",algorithm=%s,nonce="%s",username="%s",uri="%s",response="%s"`,
		auth.realm,
		auth.algorithm,
		auth.nonce,
		auth.username,
		auth.uri,
		auth.response,
	)
	if auth.qop == "auth" {
		str += fmt.Sprintf(`,qop=%s,nc=%s,cnonce="%s"`, auth.qop, auth.nc, auth.cnonce)
	}

	return str
}

// calculates Authorization response https://www.ietf.org/rfc/rfc2617.txt
func calcResponse(username, realm, password, method, uri, nonce, qop, cnonce, nc string) string {
	calcA1 := func() string {
		encoder := md5.New()
		encoder.Write([]byte(username + ":" + realm + ":" + password))

		return hex.EncodeToString(encoder.Sum(nil))
	}
	calcA2 := func() string {
		encoder := md5.New()
		encoder.Write([]byte(method + ":" + uri))

		return hex.EncodeToString(encoder.Sum(nil))
	}

	encoder := md5.New()
	encoder.Write([]byte(calcA1() + ":" + nonce + ":"))
	if qop != "" {
		encoder.Write([]byte(nc + ":" + cnonce + ":" + qop + ":"))
	}
	encoder.Write([]byte(calcA2()))

	return hex.EncodeToString(encoder.Sum(nil))
}

func AuthorizeRequest(request Request, response Response, user, password MaybeString) error {
	if user == nil {
		return fmt.Errorf("authorize request: user is nil")
	}

	var authenticateHeaderName, authorizeHeaderName string
	if response.StatusCode() == 401 {
		// on 401 Unauthorized increase request seq num, add Authorization header and send once again
		authenticateHeaderName = "WWW-Authenticate"
		authorizeHeaderName = "Authorization"
	} else {
		// 407 Proxy authentication
		authenticateHeaderName = "Proxy-Authenticate"
		authorizeHeaderName = "Proxy-Authorization"
	}

	if hdrs := response.GetHeaders(authenticateHeaderName); len(hdrs) > 0 {
		authenticateHeader := hdrs[0].(*GenericHeader)
		auth := AuthFromValue(authenticateHeader.Contents).
			SetMethod(string(request.Method())).
			SetUri(request.Recipient().String()).
			SetUsername(user.String())
		if password != nil {
			auth.SetPassword(password.String())
		}
		if auth.Qop() == "auth" {
			auth.SetNc("00000001")
			encoder := md5.New()
			encoder.Write([]byte(user.String() + request.Recipient().String()))
			if password != nil {
				encoder.Write([]byte(password.String()))
			}
			auth.SetCNonce(hex.EncodeToString(encoder.Sum(nil)))
		}
		auth.SetResponse(auth.CalcResponse())

		if hdrs = request.GetHeaders(authorizeHeaderName); len(hdrs) > 0 {
			authorizationHeader := hdrs[0].Clone().(*GenericHeader)
			authorizationHeader.Contents = auth.String()
			request.ReplaceHeaders(authorizationHeader.Name(), []Header{authorizationHeader})
		} else {
			request.AppendHeader(&GenericHeader{
				HeaderName: authorizeHeaderName,
				Contents:   auth.String(),
			})
		}
	} else {
		return fmt.Errorf("authorize request: header '%s' not found in response", authenticateHeaderName)
	}

	if viaHop, ok := request.ViaHop(); ok {
		viaHop.Params.Add("branch", String{Str: GenerateBranch()})
	}

	if cseq, ok := request.CSeq(); ok {
		cseq := cseq.Clone().(*CSeq)
		cseq.SeqNo++
		request.ReplaceHeaders(cseq.Name(), []Header{cseq})
	}

	return nil
}

type Authorizer interface {
	AuthorizeRequest(request Request, response Response) error
}

type DefaultAuthorizer struct {
	User     MaybeString
	Password MaybeString
}

func (auth *DefaultAuthorizer) AuthorizeRequest(request Request, response Response) error {
	return AuthorizeRequest(request, response, auth.User, auth.Password)
}
