// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ciphersuite

import (
	"crypto/sha512"
	"hash"
)

// TLSEcdheEcdsaWithAes256GcmSha384  represents a TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 CipherSuite
type TLSEcdheEcdsaWithAes256GcmSha384 struct {
	TLSEcdheEcdsaWithAes128GcmSha256
}

// ID returns the ID of the CipherSuite
func (c *TLSEcdheEcdsaWithAes256GcmSha384) ID() ID {
	return TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
}

func (c *TLSEcdheEcdsaWithAes256GcmSha384) String() string {
	return "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"
}

// HashFunc returns the hashing func for this CipherSuite
func (c *TLSEcdheEcdsaWithAes256GcmSha384) HashFunc() func() hash.Hash {
	return sha512.New384
}

// Init initializes the internal Cipher with keying material
func (c *TLSEcdheEcdsaWithAes256GcmSha384) Init(masterSecret, clientRandom, serverRandom []byte, isClient bool) error {
	const (
		prfMacLen = 0
		prfKeyLen = 32
		prfIvLen  = 4
	)

	return c.init(masterSecret, clientRandom, serverRandom, isClient, prfMacLen, prfKeyLen, prfIvLen, c.HashFunc())
}
