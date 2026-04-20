// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package version

import (
	"fmt"
	"testing"
)

func TestVersionComponents(t *testing.T) {
	if got := VersionMajor(); got != 7 {
		t.Fatalf("VersionMajor = %d, want 7", got)
	}
	if got := VersionMinor(); got != 0 {
		t.Fatalf("VersionMinor = %d, want 0", got)
	}
	if got := VersionRevision(); got <= 0 {
		t.Fatalf("VersionRevision = %d, want > 0", got)
	}
}

func TestVersion_FormatsMajorMinorRevision(t *testing.T) {
	want := fmt.Sprintf("%d.%d.%d", VersionMajor(), VersionMinor(), VersionRevision())
	if got := Version(); got != want {
		t.Fatalf("Version = %q, want %q", got, want)
	}
}

func TestSignature(t *testing.T) {
	if got := Signature(); got != "SRSX" {
		t.Fatalf("Signature = %q, want SRSX", got)
	}
}
