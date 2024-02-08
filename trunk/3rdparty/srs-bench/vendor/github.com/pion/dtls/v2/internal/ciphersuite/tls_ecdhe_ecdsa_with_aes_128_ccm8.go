// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import (
	"github.com/pion/dtls/v2/pkg/crypto/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
)

// NewTLSEcdheEcdsaWithAes128Ccm8 creates a new TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 CipherSuite
func NewTLSEcdheEcdsaWithAes128Ccm8() *Aes128Ccm {
	return newAes128Ccm(clientcertificate.ECDSASign, TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8, false, ciphersuite.CCMTagLength8, KeyExchangeAlgorithmEcdhe, true)
}
