// Package util contains small helpers used across the repo
package util

import (
	"encoding/binary"
)

// BigEndianUint24 returns the value of a big endian uint24
func BigEndianUint24(raw []byte) uint32 {
	if len(raw) < 3 {
		return 0
	}

	rawCopy := make([]byte, 4)
	copy(rawCopy[1:], raw)
	return binary.BigEndian.Uint32(rawCopy)
}

// PutBigEndianUint24 encodes a uint24 and places into out
func PutBigEndianUint24(out []byte, in uint32) {
	tmp := make([]byte, 4)
	binary.BigEndian.PutUint32(tmp, in)
	copy(out, tmp[1:])
}

// PutBigEndianUint48 encodes a uint64 and places into out
func PutBigEndianUint48(out []byte, in uint64) {
	tmp := make([]byte, 8)
	binary.BigEndian.PutUint64(tmp, in)
	copy(out, tmp[2:])
}

// Max returns the larger value
func Max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
