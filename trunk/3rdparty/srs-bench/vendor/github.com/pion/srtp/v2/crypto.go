// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"crypto/cipher"

	"github.com/pion/transport/v2/utils/xor"
)

// incrementCTR increments a big-endian integer of arbitrary size.
func incrementCTR(ctr []byte) {
	for i := len(ctr) - 1; i >= 0; i-- {
		ctr[i]++
		if ctr[i] != 0 {
			break
		}
	}
}

// xorBytesCTR performs CTR encryption and decryption.
// It is equivalent to cipher.NewCTR followed by XORKeyStream.
func xorBytesCTR(block cipher.Block, iv []byte, dst, src []byte) error {
	if len(iv) != block.BlockSize() {
		return errBadIVLength
	}

	ctr := make([]byte, len(iv))
	copy(ctr, iv)
	bs := block.BlockSize()
	stream := make([]byte, bs)

	i := 0
	for i < len(src) {
		block.Encrypt(stream, ctr)
		incrementCTR(ctr)
		n := xor.XorBytes(dst[i:], src[i:], stream)
		if n == 0 {
			break
		}
		i += n
	}
	return nil
}
