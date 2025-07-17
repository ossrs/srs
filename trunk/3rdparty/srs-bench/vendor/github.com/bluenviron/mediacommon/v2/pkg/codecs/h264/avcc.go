package h264

import (
	"errors"
	"fmt"
)

// ErrAVCCNoNALUs is returned by AVCCUnmarshal when no NALUs have been decoded.
var ErrAVCCNoNALUs = errors.New("AVCC unit doesn't contain any NALU")

// AVCC is an access unit that can be decoded/encoded from/to the Annex-B stream format.
// Specification: ISO 14496-15, section 5.3.4.2.1
type AVCC [][]byte

// Unmarshal decodes an access unit from the AVCC stream format.
func (a *AVCC) Unmarshal(buf []byte) error {
	bl := len(buf)
	pos := 0
	n := 0
	auSize := 0

	for {
		if (bl - pos) < 4 {
			return fmt.Errorf("invalid length")
		}

		l := int(uint32(buf[pos])<<24 | uint32(buf[pos+1])<<16 | uint32(buf[pos+2])<<8 | uint32(buf[pos+3]))
		pos += 4

		if l != 0 {
			if (auSize + l) > MaxAccessUnitSize {
				return fmt.Errorf("access unit size (%d) is too big, maximum is %d", auSize+l, MaxAccessUnitSize)
			}

			if (bl - pos) < l {
				return fmt.Errorf("invalid length")
			}

			auSize += l
			n++
			pos += l
		}

		if (bl - pos) == 0 {
			break
		}
	}

	if n == 0 {
		return ErrAVCCNoNALUs
	}

	if n > MaxNALUsPerAccessUnit {
		return fmt.Errorf("NALU count (%d) exceeds maximum allowed (%d)",
			n, MaxNALUsPerAccessUnit)
	}

	*a = make([][]byte, n)
	pos = 0

	for i := 0; i < n; {
		l := int(uint32(buf[pos])<<24 | uint32(buf[pos+1])<<16 | uint32(buf[pos+2])<<8 | uint32(buf[pos+3]))
		pos += 4

		if l != 0 {
			(*a)[i] = buf[pos : pos+l]
			pos += l
			i++
		}
	}

	return nil
}

func (a AVCC) marshalSize() int {
	n := 0
	for _, nalu := range a {
		n += 4 + len(nalu)
	}
	return n
}

// Marshal encodes an access unit into the AVCC stream format.
func (a AVCC) Marshal() ([]byte, error) {
	buf := make([]byte, a.marshalSize())
	pos := 0

	for _, nalu := range a {
		naluLen := len(nalu)
		buf[pos] = byte(naluLen >> 24)
		buf[pos+1] = byte(naluLen >> 16)
		buf[pos+2] = byte(naluLen >> 8)
		buf[pos+3] = byte(naluLen)
		pos += 4

		pos += copy(buf[pos:], nalu)
	}

	return buf, nil
}
