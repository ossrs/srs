// Package ciphersuite provides TLS Ciphers as registered with the IANA  https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-4
package ciphersuite

import (
	"errors"
	"fmt"

	"github.com/pion/dtls/v2/pkg/protocol"
)

var errCipherSuiteNotInit = &protocol.TemporaryError{Err: errors.New("CipherSuite has not been initialized")} //nolint:goerr113

// ID is an ID for our supported CipherSuites
type ID uint16

func (i ID) String() string {
	switch i {
	case TLS_ECDHE_ECDSA_WITH_AES_128_CCM:
		return "TLS_ECDHE_ECDSA_WITH_AES_128_CCM"
	case TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8:
		return "TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8"
	case TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
		return "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"
	case TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
		return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"
	case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA:
		return "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA"
	case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA:
		return "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA"
	case TLS_PSK_WITH_AES_128_CCM:
		return "TLS_PSK_WITH_AES_128_CCM"
	case TLS_PSK_WITH_AES_128_CCM_8:
		return "TLS_PSK_WITH_AES_128_CCM_8"
	case TLS_PSK_WITH_AES_128_GCM_SHA256:
		return "TLS_PSK_WITH_AES_128_GCM_SHA256"
	case TLS_PSK_WITH_AES_128_CBC_SHA256:
		return "TLS_PSK_WITH_AES_128_CBC_SHA256"
	default:
		return fmt.Sprintf("unknown(%v)", uint16(i))
	}
}

// Supported Cipher Suites
const (
	// AES-128-CCM
	TLS_ECDHE_ECDSA_WITH_AES_128_CCM   ID = 0xc0ac //nolint:golint,stylecheck
	TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 ID = 0xc0ae //nolint:golint,stylecheck

	// AES-128-GCM-SHA256
	TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 ID = 0xc02b //nolint:golint,stylecheck
	TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256   ID = 0xc02f //nolint:golint,stylecheck

	// AES-256-CBC-SHA
	TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA ID = 0xc00a //nolint:golint,stylecheck
	TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA   ID = 0xc014 //nolint:golint,stylecheck

	TLS_PSK_WITH_AES_128_CCM        ID = 0xc0a4 //nolint:golint,stylecheck
	TLS_PSK_WITH_AES_128_CCM_8      ID = 0xc0a8 //nolint:golint,stylecheck
	TLS_PSK_WITH_AES_128_GCM_SHA256 ID = 0x00a8 //nolint:golint,stylecheck
	TLS_PSK_WITH_AES_128_CBC_SHA256 ID = 0x00ae //nolint:golint,stylecheck
)

// AuthenticationType controls what authentication method is using during the handshake
type AuthenticationType int

// AuthenticationType Enums
const (
	AuthenticationTypeCertificate AuthenticationType = iota + 1
	AuthenticationTypePreSharedKey
	AuthenticationTypeAnonymous
)
