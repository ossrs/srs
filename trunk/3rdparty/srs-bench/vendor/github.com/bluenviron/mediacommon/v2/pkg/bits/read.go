// Package bits contains functions to read/write bits from/to buffers.
package bits

import (
	"fmt"
)

// HasSpace checks whether buffer has space for N bits.
func HasSpace(buf []byte, pos int, n int) error {
	if n > ((len(buf) * 8) - pos) {
		return fmt.Errorf("not enough bits")
	}
	return nil
}

// ReadBits reads N bits.
func ReadBits(buf []byte, pos *int, n int) (uint64, error) {
	err := HasSpace(buf, *pos, n)
	if err != nil {
		return 0, err
	}

	return ReadBitsUnsafe(buf, pos, n), nil
}

// ReadBitsUnsafe reads N bits.
func ReadBitsUnsafe(buf []byte, pos *int, n int) uint64 {
	res := 8 - (*pos & 0x07)
	if n < res {
		v := uint64((buf[*pos>>0x03] >> (res - n)) & (1<<n - 1))
		*pos += n
		return v
	}

	v := uint64(buf[*pos>>0x03] & (1<<res - 1))
	*pos += res
	n -= res

	for n >= 8 {
		v = (v << 8) | uint64(buf[*pos>>0x03])
		*pos += 8
		n -= 8
	}

	if n > 0 {
		v = (v << n) | uint64(buf[*pos>>0x03]>>(8-n))
		*pos += n
	}

	return v
}

// ReadGolombUnsigned reads an unsigned golomb-encoded value.
func ReadGolombUnsigned(buf []byte, pos *int) (uint32, error) {
	buflen := len(buf)
	leadingZeros := uint32(0)

	for {
		if (buflen*8 - *pos) == 0 {
			return 0, fmt.Errorf("not enough bits")
		}

		b := (buf[*pos>>0x03] >> (7 - (*pos & 0x07))) & 0x01
		*pos++
		if b != 0 {
			break
		}

		leadingZeros++
		if leadingZeros > 32 {
			return 0, fmt.Errorf("invalid value")
		}
	}

	if (buflen*8 - *pos) < int(leadingZeros) {
		return 0, fmt.Errorf("not enough bits")
	}

	codeNum := uint32(0)

	for n := leadingZeros; n > 0; n-- {
		b := (buf[*pos>>0x03] >> (7 - (*pos & 0x07))) & 0x01
		*pos++
		codeNum |= uint32(b) << (n - 1)
	}

	codeNum = (1 << leadingZeros) - 1 + codeNum

	return codeNum, nil
}

// ReadGolombSigned reads a signed golomb-encoded value.
func ReadGolombSigned(buf []byte, pos *int) (int32, error) {
	v, err := ReadGolombUnsigned(buf, pos)
	if err != nil {
		return 0, err
	}

	vi := int32(v)
	if (vi & 0x01) != 0 {
		return (vi + 1) / 2, nil
	}
	return -vi / 2, nil
}

// ReadFlag reads a boolean flag.
func ReadFlag(buf []byte, pos *int) (bool, error) {
	err := HasSpace(buf, *pos, 1)
	if err != nil {
		return false, err
	}

	return ReadFlagUnsafe(buf, pos), nil
}

// ReadFlagUnsafe reads a boolean flag.
func ReadFlagUnsafe(buf []byte, pos *int) bool {
	b := (buf[*pos>>0x03] >> (7 - (*pos & 0x07))) & 0x01
	*pos++
	return b == 1
}
