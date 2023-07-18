// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import (
	"github.com/pion/dtls/v2/pkg/crypto/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
)

// NewTLSPskWithAes256Ccm8 returns the TLS_PSK_WITH_AES_256_CCM_8 CipherSuite
func NewTLSPskWithAes256Ccm8() *Aes256Ccm {
	return newAes256Ccm(clientcertificate.Type(0), TLS_PSK_WITH_AES_256_CCM_8, true, ciphersuite.CCMTagLength8, KeyExchangeAlgorithmPsk, false)
}
