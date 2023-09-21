// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import "github.com/pion/dtls/v2/pkg/crypto/clientcertificate"

// TLSEcdheRsaWithAes256CbcSha implements the TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA CipherSuite
type TLSEcdheRsaWithAes256CbcSha struct {
	TLSEcdheEcdsaWithAes256CbcSha
}

// CertificateType returns what type of certificate this CipherSuite exchanges
func (c *TLSEcdheRsaWithAes256CbcSha) CertificateType() clientcertificate.Type {
	return clientcertificate.RSASign
}

// ID returns the ID of the CipherSuite
func (c *TLSEcdheRsaWithAes256CbcSha) ID() ID {
	return TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA
}

func (c *TLSEcdheRsaWithAes256CbcSha) String() string {
	return "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA"
}
