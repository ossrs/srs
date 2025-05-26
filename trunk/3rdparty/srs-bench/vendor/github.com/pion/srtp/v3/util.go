// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import "bytes"

// Grow the buffer size to the given number of bytes.
func growBufferSize(buf []byte, size int) []byte {
	if size <= cap(buf) {
		return buf[:size]
	}

	buf2 := make([]byte, size)
	copy(buf2, buf)
	return buf2
}

// Check if buffers match, if not allocate a new buffer and return it
func allocateIfMismatch(dst, src []byte) []byte {
	if dst == nil {
		dst = make([]byte, len(src))
		copy(dst, src)
	} else if !bytes.Equal(dst, src) { // bytes.Equal returns on ref equality, no optimization needed
		extraNeeded := len(src) - len(dst)
		if extraNeeded > 0 {
			dst = append(dst, make([]byte, extraNeeded)...)
		} else if extraNeeded < 0 {
			dst = dst[:len(dst)+extraNeeded]
		}

		copy(dst, src)
	}

	return dst
}
