// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package logger

import (
	"context"
	"encoding/hex"
	"testing"
)

func TestGenerateContextID_LengthAndHex(t *testing.T) {
	cid := GenerateContextID()
	if len(cid) != 7 {
		t.Fatalf("len(cid) = %d, want 7", len(cid))
	}
	if _, err := hex.DecodeString(cid + "0"); err != nil {
		t.Fatalf("cid %q is not hex: %v", cid, err)
	}
}

func TestGenerateContextID_Unique(t *testing.T) {
	seen := make(map[string]struct{}, 1000)
	for i := range 1000 {
		cid := GenerateContextID()
		if _, dup := seen[cid]; dup {
			t.Fatalf("duplicate cid %q at iteration %d", cid, i)
		}
		seen[cid] = struct{}{}
	}
}

func TestWithContext_AttachesCID(t *testing.T) {
	ctx := WithContext(context.Background())
	cid := ContextID(ctx)
	if len(cid) != 7 {
		t.Fatalf("ContextID length = %d, want 7", len(cid))
	}
}

func TestWithContext_IndependentCIDs(t *testing.T) {
	c1 := WithContext(context.Background())
	c2 := WithContext(context.Background())
	if ContextID(c1) == ContextID(c2) {
		t.Fatalf("expected distinct cids, got %q twice", ContextID(c1))
	}
}

func TestContextID_Missing(t *testing.T) {
	if got := ContextID(context.Background()); got != "" {
		t.Fatalf("ContextID on empty ctx = %q, want \"\"", got)
	}
}

func TestContextID_WrongTypeReturnsEmpty(t *testing.T) {
	ctx := context.WithValue(context.Background(), cidKey, 42)
	if got := ContextID(ctx); got != "" {
		t.Fatalf("ContextID with int value = %q, want \"\"", got)
	}
}

func TestWithContextID_RoundTrip(t *testing.T) {
	ctx := withContextID(context.Background(), "abcdef1")
	if got := ContextID(ctx); got != "abcdef1" {
		t.Fatalf("ContextID = %q, want %q", got, "abcdef1")
	}
}

func TestWithContextID_Overwrite(t *testing.T) {
	ctx := withContextID(context.Background(), "first00")
	ctx = withContextID(ctx, "second1")
	if got := ContextID(ctx); got != "second1" {
		t.Fatalf("ContextID after overwrite = %q, want %q", got, "second1")
	}
}

func TestCIDKey_NotCollidingWithPlainString(t *testing.T) {
	ctx := context.WithValue(context.Background(), string(cidKey), "plain")
	if got := ContextID(ctx); got != "" {
		t.Fatalf("ContextID leaked through string key = %q, want \"\"", got)
	}
}
