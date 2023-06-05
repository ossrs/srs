// Package obu implements tools for working with the "Open Bitstream Unit"
package obu

import "errors"

const (
	sevenLsbBitmask = uint(0b01111111)
	msbBitmask      = uint(0b10000000)
)

// ErrFailedToReadLEB128 indicates that a buffer ended before a LEB128 value could be successfully read
var ErrFailedToReadLEB128 = errors.New("payload ended before LEB128 was finished")

// EncodeLEB128 encodes a uint as LEB128
func EncodeLEB128(in uint) (out uint) {
	for {
		// Copy seven bits from in and discard
		// what we have copied from in
		out |= (in & sevenLsbBitmask)
		in >>= 7

		// If we have more bits to encode set MSB
		// otherwise we are done
		if in != 0 {
			out |= msbBitmask
			out <<= 8
		} else {
			return out
		}
	}
}

func decodeLEB128(in uint) (out uint) {
	for {
		// Take 7 LSB from in
		out |= (in & sevenLsbBitmask)

		// Discard the MSB
		in >>= 8
		if in == 0 {
			return out
		}

		out <<= 7
	}
}

// ReadLeb128 scans an buffer and decodes a Leb128 value.
// If the end of the buffer is reached and all MSB are set
// an error is returned
func ReadLeb128(in []byte) (uint, uint, error) {
	var encodedLength uint

	for i := range in {
		encodedLength |= uint(in[i])

		if in[i]&byte(msbBitmask) == 0 {
			return decodeLEB128(encodedLength), uint(i + 1), nil
		}

		// Make more room for next read
		encodedLength <<= 8
	}

	return 0, 0, ErrFailedToReadLEB128
}
