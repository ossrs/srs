// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package replaydetector

import (
	"fmt"
)

// fixedBigInt is the fix-sized multi-word integer.
type fixedBigInt struct {
	bits    []uint64
	n       uint
	msbMask uint64
}

// newFixedBigInt creates a new fix-sized multi-word int.
func newFixedBigInt(n uint) *fixedBigInt {
	chunkSize := (n + 63) / 64
	if chunkSize == 0 {
		chunkSize = 1
	}
	return &fixedBigInt{
		bits:    make([]uint64, chunkSize),
		n:       n,
		msbMask: (1 << (64 - n%64)) - 1,
	}
}

// Lsh is the left shift operation.
func (s *fixedBigInt) Lsh(n uint) {
	if n == 0 {
		return
	}
	nChunk := int(n / 64)
	nN := n % 64

	for i := len(s.bits) - 1; i >= 0; i-- {
		var carry uint64
		if i-nChunk >= 0 {
			carry = s.bits[i-nChunk] << nN
			if i-nChunk-1 >= 0 {
				carry |= s.bits[i-nChunk-1] >> (64 - nN)
			}
		}
		s.bits[i] = (s.bits[i] << n) | carry
	}
	s.bits[len(s.bits)-1] &= s.msbMask
}

// Bit returns i-th bit of the fixedBigInt.
func (s *fixedBigInt) Bit(i uint) uint {
	if i >= s.n {
		return 0
	}
	chunk := i / 64
	pos := i % 64
	if s.bits[chunk]&(1<<pos) != 0 {
		return 1
	}
	return 0
}

// SetBit sets i-th bit to 1.
func (s *fixedBigInt) SetBit(i uint) {
	if i >= s.n {
		return
	}
	chunk := i / 64
	pos := i % 64
	s.bits[chunk] |= 1 << pos
}

// String returns string representation of fixedBigInt.
func (s *fixedBigInt) String() string {
	var out string
	for i := len(s.bits) - 1; i >= 0; i-- {
		out += fmt.Sprintf("%016X", s.bits[i])
	}
	return out
}
