// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"os"
	"testing"
	"time"

	"srsx/internal/clock"
)

func TestMain(m *testing.M) {
	clock.Resolution = time.Millisecond
	os.Exit(m.Run())
}
