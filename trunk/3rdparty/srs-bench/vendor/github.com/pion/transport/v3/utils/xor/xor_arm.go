// SPDX-FileCopyrightText: 2022 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !gccgo
// +build !gccgo

// Package xor provides utility functions used by other Pion
// packages. ARM arch.
package xor

import (
	"unsafe"

	"golang.org/x/sys/cpu"
)

const wordSize = int(unsafe.Sizeof(uintptr(0))) // nolint:gosec
var hasNEON = cpu.ARM.HasNEON                   // nolint:gochecknoglobals

func isAligned(a *byte) bool {
	return uintptr(unsafe.Pointer(a))%uintptr(wordSize) == 0
}

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

	if hasNEON {
		xorBytesNEON32(&dst[0], &a[0], &b[0], n)
	} else if isAligned(&dst[0]) && isAligned(&a[0]) && isAligned(&b[0]) {
		xorBytesARM32(&dst[0], &a[0], &b[0], n)
	} else {
		safeXORBytes(dst, a, b, n)
	}
	return n
}

// n needs to be smaller or equal than the length of a and b.
func safeXORBytes(dst, a, b []byte, n int) {
	for i := 0; i < n; i++ {
		dst[i] = a[i] ^ b[i]
	}
}

//go:noescape
func xorBytesARM32(dst, a, b *byte, n int)

//go:noescape
func xorBytesNEON32(dst, a, b *byte, n int)
