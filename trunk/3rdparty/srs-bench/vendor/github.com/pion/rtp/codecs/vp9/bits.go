// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vp9

import "errors"

var errNotEnoughBits = errors.New("not enough bits")

func hasSpace(buf []byte, pos int, n int) error {
	if n > ((len(buf) * 8) - pos) {
		return errNotEnoughBits
	}

	return nil
}

func readFlag(buf []byte, pos *int) (bool, error) {
	err := hasSpace(buf, *pos, 1)
	if err != nil {
		return false, err
	}

	return readFlagUnsafe(buf, pos), nil
}

func readFlagUnsafe(buf []byte, pos *int) bool {
	b := (buf[*pos>>0x03] >> (7 - (*pos & 0x07))) & 0x01
	*pos++

	return b == 1
}

func readBits(buf []byte, pos *int, n int) (uint64, error) {
	err := hasSpace(buf, *pos, n)
	if err != nil {
		return 0, err
	}

	return readBitsUnsafe(buf, pos, n), nil
}

func readBitsUnsafe(buf []byte, pos *int, n int) uint64 {
	res := 8 - (*pos & 0x07)
	if n < res {
		bits := uint64((buf[*pos>>0x03] >> (res - n)) & (1<<n - 1))
		*pos += n

		return bits
	}

	bits := uint64(buf[*pos>>0x03] & (1<<res - 1))
	*pos += res
	n -= res

	for n >= 8 {
		bits = (bits << 8) | uint64(buf[*pos>>0x03])
		*pos += 8
		n -= 8
	}

	if n > 0 {
		bits = (bits << n) | uint64(buf[*pos>>0x03]>>(8-n))
		*pos += n
	}

	return bits
}
