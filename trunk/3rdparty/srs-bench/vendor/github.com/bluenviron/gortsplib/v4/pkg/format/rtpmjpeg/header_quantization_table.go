package rtpmjpeg

import (
	"fmt"
)

type headerQuantizationTable struct {
	MBZ       uint8
	Precision uint8
	Tables    [][]byte
}

func (h *headerQuantizationTable) unmarshal(byts []byte) (int, error) {
	if len(byts) < 4 {
		return 0, fmt.Errorf("buffer is too short")
	}

	h.MBZ = byts[0]
	h.Precision = byts[1]
	if h.Precision != 0 {
		return 0, fmt.Errorf("precision %d is not supported", h.Precision)
	}

	length := int(byts[2])<<8 | int(byts[3])
	switch length {
	case 64, 128:
	default:
		return 0, fmt.Errorf("table length %d is not supported", length)
	}

	if (len(byts) - 4) < length {
		return 0, fmt.Errorf("buffer is too short")
	}

	tableCount := length / 64
	h.Tables = make([][]byte, tableCount)
	n := 0

	for i := 0; i < tableCount; i++ {
		h.Tables[i] = byts[4+n : 4+64+n]
		n += 64
	}

	return 4 + length, nil
}

func (h headerQuantizationTable) marshal(byts []byte) []byte {
	byts = append(byts, h.MBZ)
	byts = append(byts, h.Precision)

	l := len(h.Tables) * 64
	byts = append(byts, []byte{byte(l >> 8), byte(l)}...)

	for i := 0; i < len(h.Tables); i++ {
		byts = append(byts, h.Tables[i]...)
	}

	return byts
}
