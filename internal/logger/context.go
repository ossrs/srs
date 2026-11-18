// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package logger

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
)

type key string

var cidKey key = "cid.srsx.ossrs.org"

func GenerateContextID() string {
	randomBytes := make([]byte, 32)
	_, _ = rand.Read(randomBytes)
	hash := sha256.Sum256(randomBytes)
	hashString := hex.EncodeToString(hash[:])
	cid := hashString[:7]
	return cid
}

func WithContext(ctx context.Context) context.Context {
	return withContextID(ctx, GenerateContextID())
}

func withContextID(ctx context.Context, cid string) context.Context {
	return context.WithValue(ctx, cidKey, cid)
}

func ContextID(ctx context.Context) string {
	if cid, ok := ctx.Value(cidKey).(string); ok {
		return cid
	}
	return ""
}
