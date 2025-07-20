// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build debug
// +build debug

package stun

import "fmt"

// IntegrityErr occurs when computed HMAC differs from expected.
type IntegrityErr struct {
	Expected []byte
	Actual   []byte
}

func (i *IntegrityErr) Error() string {
	return fmt.Sprintf(
		"Integrity check failed: 0x%x (expected) !- 0x%x (actual)",
		i.Expected, i.Actual,
	)
}
