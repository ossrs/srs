// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package sequencenumber provides a sequence number unwrapper
package sequencenumber

const (
	maxSequenceNumberPlusOne = int64(65536)
	breakpoint               = 32768 // half of max uint16
)

// Unwrapper stores an unwrapped sequence number
type Unwrapper struct {
	init          bool
	lastUnwrapped int64
}

func isNewer(value, previous uint16) bool {
	if value-previous == breakpoint {
		return value > previous
	}
	return value != previous && (value-previous) < breakpoint
}

// Unwrap unwraps the next sequencenumber
func (u *Unwrapper) Unwrap(i uint16) int64 {
	if !u.init {
		u.init = true
		u.lastUnwrapped = int64(i)
		return u.lastUnwrapped
	}

	lastWrapped := uint16(u.lastUnwrapped)
	delta := int64(i - lastWrapped)
	if isNewer(i, lastWrapped) {
		if delta < 0 {
			delta += maxSequenceNumberPlusOne
		}
	} else if delta > 0 && u.lastUnwrapped+delta-maxSequenceNumberPlusOne >= 0 {
		delta -= maxSequenceNumberPlusOne
	}

	u.lastUnwrapped += delta
	return u.lastUnwrapped
}
