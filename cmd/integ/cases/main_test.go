// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package cases holds the black-box cases of the SRS integration suite. See
// cmd/integ/README.md for the rules and the catalog.
package cases

import (
	"os"
	"testing"

	"srs-integ/internal/check"
	"srs-integ/internal/server"
)

func TestMain(m *testing.M) {
	os.Exit(server.Main(m, check.Summary))
}
