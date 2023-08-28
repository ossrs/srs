// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import (
	"github.com/pion/dtls/v2/pkg/crypto/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
)

// NewTLSPskWithAes128Ccm returns the TLS_PSK_WITH_AES_128_CCM CipherSuite
func NewTLSPskWithAes128Ccm() *Aes128Ccm {
	return newAes128Ccm(clientcertificate.Type(0), TLS_PSK_WITH_AES_128_CCM, true, ciphersuite.CCMTagLength, KeyExchangeAlgorithmPsk, false)
}
