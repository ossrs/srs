// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package ciphersuite provides TLS Ciphers as registered with the IANA  https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-4
package ciphersuite

import (
	"errors"
	"fmt"

	"github.com/pion/dtls/v2/internal/ciphersuite/types"
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
	case TLS_PSK_WITH_AES_256_CCM_8:
		return "TLS_PSK_WITH_AES_256_CCM_8"
	case TLS_PSK_WITH_AES_128_GCM_SHA256:
		return "TLS_PSK_WITH_AES_128_GCM_SHA256"
	case TLS_PSK_WITH_AES_128_CBC_SHA256:
		return "TLS_PSK_WITH_AES_128_CBC_SHA256"
	case TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
		return "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"
	case TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
		return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"
	case TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256:
		return "TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256"
	default:
		return fmt.Sprintf("unknown(%v)", uint16(i))
	}
}

// Supported Cipher Suites
const (
	// AES-128-CCM
	TLS_ECDHE_ECDSA_WITH_AES_128_CCM   ID = 0xc0ac //nolint:revive,stylecheck
	TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 ID = 0xc0ae //nolint:revive,stylecheck

	// AES-128-GCM-SHA256
	TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 ID = 0xc02b //nolint:revive,stylecheck
	TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256   ID = 0xc02f //nolint:revive,stylecheck

	TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 ID = 0xc02c //nolint:revive,stylecheck
	TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384   ID = 0xc030 //nolint:revive,stylecheck
	// AES-256-CBC-SHA
	TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA ID = 0xc00a //nolint:revive,stylecheck
	TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA   ID = 0xc014 //nolint:revive,stylecheck

	TLS_PSK_WITH_AES_128_CCM        ID = 0xc0a4 //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_128_CCM_8      ID = 0xc0a8 //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_256_CCM_8      ID = 0xc0a9 //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_128_GCM_SHA256 ID = 0x00a8 //nolint:revive,stylecheck
	TLS_PSK_WITH_AES_128_CBC_SHA256 ID = 0x00ae //nolint:revive,stylecheck

	TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256 ID = 0xC037 //nolint:revive,stylecheck
)

// AuthenticationType controls what authentication method is using during the handshake
type AuthenticationType = types.AuthenticationType

// AuthenticationType Enums
const (
	AuthenticationTypeCertificate  AuthenticationType = types.AuthenticationTypeCertificate
	AuthenticationTypePreSharedKey AuthenticationType = types.AuthenticationTypePreSharedKey
	AuthenticationTypeAnonymous    AuthenticationType = types.AuthenticationTypeAnonymous
)

// KeyExchangeAlgorithm controls what exchange algorithm was chosen.
type KeyExchangeAlgorithm = types.KeyExchangeAlgorithm

// KeyExchangeAlgorithm Bitmask
const (
	KeyExchangeAlgorithmNone  KeyExchangeAlgorithm = types.KeyExchangeAlgorithmNone
	KeyExchangeAlgorithmPsk   KeyExchangeAlgorithm = types.KeyExchangeAlgorithmPsk
	KeyExchangeAlgorithmEcdhe KeyExchangeAlgorithm = types.KeyExchangeAlgorithmEcdhe
)
