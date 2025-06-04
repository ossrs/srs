// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package server

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"time"
)

const (
	nonceLifetime  = time.Hour // See: https://tools.ietf.org/html/rfc5766#section-4
	nonceLength    = 40
	nonceKeyLength = 64
)

// NewNonceHash creates a NonceHash
func NewNonceHash() (*NonceHash, error) {
	key := make([]byte, nonceKeyLength)
	if _, err := rand.Read(key); err != nil {
		return nil, err
	}

	return &NonceHash{key}, nil
}

// NonceHash is used to create and verify nonces
type NonceHash struct {
	key []byte
}

// Generate a nonce
func (n *NonceHash) Generate() (string, error) {
	nonce := make([]byte, 8, nonceLength)
	binary.BigEndian.PutUint64(nonce, uint64(time.Now().UnixMilli()))

	hash := hmac.New(sha256.New, n.key)
	if _, err := hash.Write(nonce[:8]); err != nil {
		return "", fmt.Errorf("%w: %v", errFailedToGenerateNonce, err) //nolint:errorlint
	}
	nonce = hash.Sum(nonce)

	return hex.EncodeToString(nonce), nil
}

// Validate checks that nonce is signed and is not expired
func (n *NonceHash) Validate(nonce string) error {
	b, err := hex.DecodeString(nonce)
	if err != nil || len(b) != nonceLength {
		return fmt.Errorf("%w: %v", errInvalidNonce, err) //nolint:errorlint
	}

	if ts := time.UnixMilli(int64(binary.BigEndian.Uint64(b))); time.Since(ts) > nonceLifetime {
		return errInvalidNonce
	}

	hash := hmac.New(sha256.New, n.key)
	if _, err = hash.Write(b[:8]); err != nil {
		return fmt.Errorf("%w: %v", errInvalidNonce, err) //nolint:errorlint
	}
	if !hmac.Equal(b[8:], hash.Sum(nil)) {
		return errInvalidNonce
	}

	return nil
}
