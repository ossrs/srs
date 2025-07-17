package rtpmjpeg

import (
	"fmt"
)

type headerJPEG struct {
	TypeSpecific   uint8
	FragmentOffset uint32
	Type           uint8
	Quantization   uint8
	Width          int
	Height         int
}

func (h *headerJPEG) unmarshal(byts []byte) (int, error) {
	if len(byts) < 8 {
		return 0, fmt.Errorf("buffer is too short")
	}

	h.TypeSpecific = byts[0]
	h.FragmentOffset = uint32(byts[1])<<16 | uint32(byts[2])<<8 | uint32(byts[3])

	h.Type = byts[4]
	if h.Type > 63 {
		return 0, fmt.Errorf("type %d is not supported", h.Type)
	}

	h.Quantization = byts[5]
	if h.Quantization == 0 ||
		(h.Quantization > 99 && h.Quantization < 127) {
		return 0, fmt.Errorf("quantization %d is invalid", h.Quantization)
	}

	h.Width = int(byts[6]) * 8
	h.Height = int(byts[7]) * 8

	return 8, nil
}

func (h headerJPEG) marshal(byts []byte) []byte {
	byts = append(byts, h.TypeSpecific)
	byts = append(byts, []byte{byte(h.FragmentOffset >> 16), byte(h.FragmentOffset >> 8), byte(h.FragmentOffset)}...)
	byts = append(byts, h.Type)
	byts = append(byts, h.Quantization)
	byts = append(byts, byte(h.Width/8))
	byts = append(byts, byte(h.Height/8))
	return byts
}
