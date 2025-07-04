// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"unsafe"
)

// growBufferSize grows the buffer size to the given number of bytes.
func growBufferSize(buf []byte, size int) []byte {
	if size <= cap(buf) {
		return buf[:size]
	}

	buf2 := make([]byte, size)
	copy(buf2, buf)

	return buf2
}

// isSameBuffer returns true if slices a and b share the same underlying buffer.
func isSameBuffer(a, b []byte) bool {
	// If both are nil, they are technically the same (no buffer)
	if a == nil && b == nil {
		return true
	}

	// If either is nil, or both have 0 capacity, they can't share backing buffer
	if cap(a) == 0 || cap(b) == 0 {
		return false
	}

	// Create a slice of length 1 from each if possible
	aPtr := unsafe.Pointer(&a[:1][0]) // nolint:gosec
	bPtr := unsafe.Pointer(&b[:1][0]) // nolint:gosec

	return aPtr == bPtr
}
