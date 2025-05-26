// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtcp

// getPadding Returns the padding required to make the length a multiple of 4
func getPadding(packetLen int) int {
	if packetLen%4 == 0 {
		return 0
	}
	return 4 - (packetLen % 4)
}

// setNBitsOfUint16 will truncate the value to size, left-shift to startIndex position and set
func setNBitsOfUint16(src, size, startIndex, val uint16) (uint16, error) {
	if startIndex+size > 16 {
		return 0, errInvalidSizeOrStartIndex
	}

	// truncate val to size bits
	val &= (1 << size) - 1

	return src | (val << (16 - size - startIndex)), nil
}

// appendBit32 will left-shift and append n bits of val
func appendNBitsToUint32(src, n, val uint32) uint32 {
	return (src << n) | (val & (0xFFFFFFFF >> (32 - n)))
}

// getNBit get n bits from 1 byte, begin with a position
func getNBitsFromByte(b byte, begin, n uint16) uint16 {
	endShift := 8 - (begin + n)
	mask := (0xFF >> begin) & uint8(0xFF<<endShift)
	return uint16(b&mask) >> endShift
}

// get24BitFromBytes get 24bits from `[3]byte` slice
func get24BitsFromBytes(b []byte) uint32 {
	return uint32(b[0])<<16 + uint32(b[1])<<8 + uint32(b[2])
}
