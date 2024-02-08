// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import "github.com/pion/dtls/v2/pkg/crypto/clientcertificate"

// TLSEcdheRsaWithAes128GcmSha256 implements the TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 CipherSuite
type TLSEcdheRsaWithAes128GcmSha256 struct {
	TLSEcdheEcdsaWithAes128GcmSha256
}

// CertificateType returns what type of certificate this CipherSuite exchanges
func (c *TLSEcdheRsaWithAes128GcmSha256) CertificateType() clientcertificate.Type {
	return clientcertificate.RSASign
}

// ID returns the ID of the CipherSuite
func (c *TLSEcdheRsaWithAes128GcmSha256) ID() ID {
	return TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
}

func (c *TLSEcdheRsaWithAes128GcmSha256) String() string {
	return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"
}
