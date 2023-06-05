// SPDX-FileCopyrightText: 2020 The Go Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

//go:build !gccgo
// +build !gccgo

// Package xor provides utility functions used by other Pion
// packages. ARM64 arch.
package xor

// XorBytes xors the bytes in a and b. The destination should have enough
// space, otherwise xorBytes will panic. Returns the number of bytes xor'd.
//
//revive:disable-next-line
func XorBytes(dst, a, b []byte) int {
	n := len(a)
	if len(b) < n {
		n = len(b)
	}
	if n == 0 {
		return 0
	}
	// make sure dst has enough space
	_ = dst[n-1]

	xorBytesARM64(&dst[0], &a[0], &b[0], n)
	return n
}

//go:noescape
func xorBytesARM64(dst, a, b *byte, n int)
