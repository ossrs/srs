package auth

import (
	"crypto/rand"
	"encoding/hex"
)

// GenerateNonce generates a nonce that can be used in Validate().
func GenerateNonce() (string, error) {
	byts := make([]byte, 16)
	_, err := rand.Read(byts)
	if err != nil {
		return "", err
	}

	return hex.EncodeToString(byts), nil
}
