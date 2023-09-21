// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import "github.com/pion/dtls/v2/pkg/crypto/clientcertificate"

// TLSEcdheRsaWithAes256GcmSha384 implements the TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 CipherSuite
type TLSEcdheRsaWithAes256GcmSha384 struct {
	TLSEcdheEcdsaWithAes256GcmSha384
}

// CertificateType returns what type of certificate this CipherSuite exchanges
func (c *TLSEcdheRsaWithAes256GcmSha384) CertificateType() clientcertificate.Type {
	return clientcertificate.RSASign
}

// ID returns the ID of the CipherSuite
func (c *TLSEcdheRsaWithAes256GcmSha384) ID() ID {
	return TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
}

func (c *TLSEcdheRsaWithAes256GcmSha384) String() string {
	return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"
}
