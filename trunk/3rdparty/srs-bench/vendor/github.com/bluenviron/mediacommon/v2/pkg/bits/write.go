package bits

// WriteBitsUnsafe writes N bits.
func WriteBitsUnsafe(buf []byte, pos *int, v uint64, n int) {
	res := 8 - (*pos & 0x07)
	if n < res {
		buf[*pos>>0x03] |= byte(v << (res - n))
		*pos += n
		return
	}

	buf[*pos>>3] |= byte(v >> (n - res))
	*pos += res
	n -= res

	for n >= 8 {
		buf[*pos>>3] = byte(v >> (n - 8))
		*pos += 8
		n -= 8
	}

	if n > 0 {
		buf[*pos>>3] = byte((v & (1<<n - 1)) << (8 - n))
		*pos += n
	}
}
