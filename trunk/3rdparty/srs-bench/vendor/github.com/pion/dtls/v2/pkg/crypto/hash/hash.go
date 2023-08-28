// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package hash provides TLS HashAlgorithm as defined in TLS 1.2
package hash

import ( //nolint:gci
	"crypto"
	"crypto/md5"  //nolint:gosec
	"crypto/sha1" //nolint:gosec
	"crypto/sha256"
	"crypto/sha512"
)

// Algorithm is used to indicate the hash algorithm used
// https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-18
type Algorithm uint16

// Supported hash algorithms
const (
	None    Algorithm = 0 // Blacklisted
	MD5     Algorithm = 1 // Blacklisted
	SHA1    Algorithm = 2 // Blacklisted
	SHA224  Algorithm = 3
	SHA256  Algorithm = 4
	SHA384  Algorithm = 5
	SHA512  Algorithm = 6
	Ed25519 Algorithm = 8
)

// String makes hashAlgorithm printable
func (a Algorithm) String() string {
	switch a {
	case None:
		return "none"
	case MD5:
		return "md5" // [RFC3279]
	case SHA1:
		return "sha-1" // [RFC3279]
	case SHA224:
		return "sha-224" // [RFC4055]
	case SHA256:
		return "sha-256" // [RFC4055]
	case SHA384:
		return "sha-384" // [RFC4055]
	case SHA512:
		return "sha-512" // [RFC4055]
	case Ed25519:
		return "null"
	default:
		return "unknown or unsupported hash algorithm"
	}
}

// Digest performs a digest on the passed value
func (a Algorithm) Digest(b []byte) []byte {
	switch a {
	case None:
		return nil
	case MD5:
		hash := md5.Sum(b) // #nosec
		return hash[:]
	case SHA1:
		hash := sha1.Sum(b) // #nosec
		return hash[:]
	case SHA224:
		hash := sha256.Sum224(b)
		return hash[:]
	case SHA256:
		hash := sha256.Sum256(b)
		return hash[:]
	case SHA384:
		hash := sha512.Sum384(b)
		return hash[:]
	case SHA512:
		hash := sha512.Sum512(b)
		return hash[:]
	default:
		return nil
	}
}

// Insecure returns if the given HashAlgorithm is considered secure in DTLS 1.2
func (a Algorithm) Insecure() bool {
	switch a {
	case None, MD5, SHA1:
		return true
	default:
		return false
	}
}

// CryptoHash returns the crypto.Hash implementation for the given HashAlgorithm
func (a Algorithm) CryptoHash() crypto.Hash {
	switch a {
	case None:
		return crypto.Hash(0)
	case MD5:
		return crypto.MD5
	case SHA1:
		return crypto.SHA1
	case SHA224:
		return crypto.SHA224
	case SHA256:
		return crypto.SHA256
	case SHA384:
		return crypto.SHA384
	case SHA512:
		return crypto.SHA512
	case Ed25519:
		return crypto.Hash(0)
	default:
		return crypto.Hash(0)
	}
}

// Algorithms returns all the supported Hash Algorithms
func Algorithms() map[Algorithm]struct{} {
	return map[Algorithm]struct{}{
		None:    {},
		MD5:     {},
		SHA1:    {},
		SHA224:  {},
		SHA256:  {},
		SHA384:  {},
		SHA512:  {},
		Ed25519: {},
	}
}
