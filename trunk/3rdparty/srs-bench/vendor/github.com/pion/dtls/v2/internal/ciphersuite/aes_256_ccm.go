// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import (
	"github.com/pion/dtls/v2/pkg/crypto/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
)

// Aes256Ccm is a base class used by multiple AES-CCM Ciphers
type Aes256Ccm struct {
	AesCcm
}

func newAes256Ccm(clientCertificateType clientcertificate.Type, id ID, psk bool, cryptoCCMTagLen ciphersuite.CCMTagLen, keyExchangeAlgorithm KeyExchangeAlgorithm, ecc bool) *Aes256Ccm {
	return &Aes256Ccm{
		AesCcm: AesCcm{
			clientCertificateType: clientCertificateType,
			id:                    id,
			psk:                   psk,
			cryptoCCMTagLen:       cryptoCCMTagLen,
			keyExchangeAlgorithm:  keyExchangeAlgorithm,
			ecc:                   ecc,
		},
	}
}

// Init initializes the internal Cipher with keying material
func (c *Aes256Ccm) Init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error {
	const prfKeyLen = 32
	return c.AesCcm.Init(masterSecret, clientRandom, serverRandom, isClient, prfKeyLen)
}
