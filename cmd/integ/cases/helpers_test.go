// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"crypto/rand"
	"encoding/hex"
	"path"
	"regexp"

	"srs-integ/internal/check"
)

var unsafeStreamChars = regexp.MustCompile(`[^A-Za-z0-9_-]+`)

// streamName is a stream name of its own for a case: the case name and a
// random suffix, such as PublishPlay-3fa2c1.
func streamName(c *check.C) string {
	b := make([]byte, 3)
	rand.Read(b)
	return unsafeStreamChars.ReplaceAllString(path.Base(c.Name()), "_") + "-" + hex.EncodeToString(b)
}
