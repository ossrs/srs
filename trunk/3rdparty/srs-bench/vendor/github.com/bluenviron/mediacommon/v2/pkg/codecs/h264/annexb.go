package h264

import (
	"errors"
	"fmt"
)

// ErrAnnexBNoNALUs is returned by AnnexBUnmarshal when no NALUs have been decoded.
var ErrAnnexBNoNALUs = errors.New("Annex-B unit doesn't contain any NALU")

// ErrAnnexBNoInitialDelimiter is returned by AnnexBUnmarshal when the initial delimiter is not found.
var ErrAnnexBNoInitialDelimiter = errors.New("initial delimiter not found")

// countNalUnits counts the number of NAL units in the Annex-B stream.
func countNalUnits(buf []byte) (int, error) {
	n := 0
	i := 0
	start := 0
	auSize := 0

	for i < len(buf) {
		lim := 4
		if lim > len(buf)-i {
			lim = len(buf) - i
		}
		data := buf[i : i+lim]

		switch {
		case len(data) >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01:
			if i > start {
				auSize += i - start
				if auSize > MaxAccessUnitSize {
					return 0, fmt.Errorf("access unit size (%d) is too big, maximum is %d", auSize, MaxAccessUnitSize)
				}
				n++
			}
			i += 3
			start = i
		case len(data) >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01:
			if i > start {
				auSize += i - start
				if auSize > MaxAccessUnitSize {
					return 0, fmt.Errorf("access unit size (%d) is too big, maximum is %d", auSize, MaxAccessUnitSize)
				}
				n++
			}
			i += 4
			start = i
		default:
			i++
		}
	}

	if i > start {
		if (auSize + i - start) > MaxAccessUnitSize {
			return 0, fmt.Errorf("access unit size (%d) is too big, maximum is %d", auSize+i-start, MaxAccessUnitSize)
		}
		n++
	}

	return n, nil
}

func hasInitialDelimiter(buf []byte) bool {
	if len(buf) < 4 {
		return false
	}
	return buf[0] == 0x00 && buf[1] == 0x00 && (buf[2] == 0x00 && buf[3] == 0x01) || (buf[2] == 0x01)
}

// AnnexB is an access unit that can be decoded/encoded from/to the Annex-B stream format.
// Specification: ITU-T Rec. H.264, Annex B
type AnnexB [][]byte

// Unmarshal decodes an access unit from the Annex-B stream format.
func (a *AnnexB) Unmarshal(buf []byte) error {
	count, err := countNalUnits(buf)
	if err != nil {
		return err
	}

	if count == 0 {
		return ErrAnnexBNoNALUs
	}

	if !hasInitialDelimiter(buf) {
		return ErrAnnexBNoInitialDelimiter
	}

	*a = make([][]byte, 0, count)
	i := 0
	start := 0

	for i < len(buf) {
		lim := 4
		if lim > len(buf)-i {
			lim = len(buf) - i
		}
		data := buf[i : i+lim]

		switch {
		case len(data) >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01:
			// Is this a NALU with a 3 byte start code prefix
			if i > start {
				*a = append(*a, buf[start:i])
			}
			i += 3
			start = i
		case len(data) >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01:
			// OR is this a NALU with a 4 byte start code prefix
			if i > start {
				*a = append(*a, buf[start:i])
			}
			i += 4
			start = i
		default:
			i++
		}
	}

	if i > start {
		*a = append(*a, buf[start:i])
	}

	return nil
}

func (a AnnexB) marshalSize() int {
	n := 0
	for _, nalu := range a {
		n += 4 + len(nalu)
	}
	return n
}

// Marshal encodes an access unit into the Annex-B stream format.
func (a AnnexB) Marshal() ([]byte, error) {
	buf := make([]byte, a.marshalSize())
	pos := 0

	for _, nalu := range a {
		pos += copy(buf[pos:], []byte{0x00, 0x00, 0x00, 0x01})
		pos += copy(buf[pos:], nalu)
	}

	return buf, nil
}
