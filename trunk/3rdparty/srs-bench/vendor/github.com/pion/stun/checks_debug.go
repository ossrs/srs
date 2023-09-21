// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build debug
// +build debug

package stun

import "github.com/pion/stun/internal/hmac"

// CheckSize returns *AttrLengthError if got is not equal to expected.
func CheckSize(a AttrType, got, expected int) error {
	if got == expected {
		return nil
	}
	return &AttrLengthErr{
		Got:      got,
		Expected: expected,
		Attr:     a,
	}
}

func checkHMAC(got, expected []byte) error {
	if hmac.Equal(got, expected) {
		return nil
	}
	return &IntegrityErr{
		Expected: expected,
		Actual:   got,
	}
}

func checkFingerprint(got, expected uint32) error {
	if got == expected {
		return nil
	}
	return &CRCMismatch{
		Actual:   got,
		Expected: expected,
	}
}

// IsAttrSizeInvalid returns true if error means that attribute size is invalid.
func IsAttrSizeInvalid(err error) bool {
	_, ok := err.(*AttrLengthErr)
	return ok
}

// CheckOverflow returns *AttrOverflowErr if got is bigger that max.
func CheckOverflow(t AttrType, got, max int) error {
	if got <= max {
		return nil
	}
	return &AttrOverflowErr{
		Type: t,
		Got:  got,
		Max:  max,
	}
}

// IsAttrSizeOverflow returns true if error means that attribute size is too big.
func IsAttrSizeOverflow(err error) bool {
	_, ok := err.(*AttrOverflowErr)
	return ok
}
