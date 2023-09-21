// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package types provides types for TLS Ciphers
package types

// KeyExchangeAlgorithm controls what exchange algorithm was chosen.
type KeyExchangeAlgorithm int

// KeyExchangeAlgorithm Bitmask
const (
	KeyExchangeAlgorithmNone KeyExchangeAlgorithm = 0
	KeyExchangeAlgorithmPsk  KeyExchangeAlgorithm = iota << 1
	KeyExchangeAlgorithmEcdhe
)

// Has check if keyExchangeAlgorithm is supported.
func (a KeyExchangeAlgorithm) Has(v KeyExchangeAlgorithm) bool {
	return (a & v) == v
}
