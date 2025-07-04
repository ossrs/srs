// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package util implements utilities to better support Fec decoding / encoding.
package util

// BitArray provides support for bitmask manipulations.
type BitArray struct {
	Lo uint64 // leftmost 64 bits
	Hi uint64 // rightmost 64 bits
}

// SetBit sets a bit to the specified bit value on the bitmask.
func (b *BitArray) SetBit(bitIndex uint32) {
	if bitIndex < 64 {
		b.Lo |= uint64(0b1) << (63 - bitIndex)
	} else {
		hiBitIndex := bitIndex - 64
		b.Hi |= uint64(0b1) << (63 - hiBitIndex)
	}
}

// Reset clears the bitmask.
func (b *BitArray) Reset() {
	b.Lo = 0
	b.Hi = 0
}

// GetBit returns the bit value at a specified index of the bitmask.
func (b *BitArray) GetBit(bitIndex uint32) uint8 {
	if bitIndex < 64 {
		result := (b.Lo & (uint64(0b1) << (63 - bitIndex)))
		if result > 0 {
			return 1
		}

		return 0
	}

	hiBitIndex := bitIndex - 64
	result := (b.Hi & (uint64(0b1) << (63 - hiBitIndex)))
	if result > 0 {
		return 1
	}

	return 0
}
