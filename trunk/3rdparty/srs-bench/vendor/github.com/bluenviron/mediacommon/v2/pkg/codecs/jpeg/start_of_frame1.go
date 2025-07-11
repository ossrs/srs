package jpeg

import (
	"fmt"
)

// StartOfFrame1 is a SOF1 marker.
type StartOfFrame1 struct {
	Type                   uint8
	Width                  int
	Height                 int
	QuantizationTableCount uint8 // write only
}

// Unmarshal decodes the marker.
func (m *StartOfFrame1) Unmarshal(buf []byte) error {
	if len(buf) != 15 {
		return fmt.Errorf("unsupported SOF size of %d", len(buf))
	}

	precision := buf[0]
	if precision != 8 {
		return fmt.Errorf("precision %d is not supported", precision)
	}

	m.Height = int(buf[1])<<8 | int(buf[2])
	m.Width = int(buf[3])<<8 | int(buf[4])

	components := buf[5]
	if components != 3 {
		return fmt.Errorf("number of components = %d is not supported", components)
	}

	samp0 := buf[7]
	switch samp0 {
	case 0x21:
		m.Type = 0

	case 0x22:
		m.Type = 1

	default:
		return fmt.Errorf("samp0 %x is not supported", samp0)
	}

	samp1 := buf[10]
	if samp1 != 0x11 {
		return fmt.Errorf("samp1 %x is not supported", samp1)
	}

	samp2 := buf[13]
	if samp2 != 0x11 {
		return fmt.Errorf("samp2 %x is not supported", samp2)
	}

	return nil
}

// Marshal encodes the marker.
func (m StartOfFrame1) Marshal(buf []byte) []byte {
	buf = append(buf, []byte{0xFF, MarkerStartOfFrame1}...)
	buf = append(buf, []byte{0, 17}...)                               // length
	buf = append(buf, []byte{8}...)                                   // precision
	buf = append(buf, []byte{byte(m.Height >> 8), byte(m.Height)}...) // height
	buf = append(buf, []byte{byte(m.Width >> 8), byte(m.Width)}...)   // width
	buf = append(buf, []byte{3}...)                                   // components
	if (m.Type & 0x3f) == 0 {                                         // component 0
		buf = append(buf, []byte{0x00, 0x21, 0}...)
	} else {
		buf = append(buf, []byte{0x00, 0x22, 0}...)
	}

	var secondQuantizationTable byte
	if m.QuantizationTableCount == 2 {
		secondQuantizationTable = 1
	} else {
		secondQuantizationTable = 0
	}

	buf = append(buf, []byte{1, 0x11, secondQuantizationTable}...) // component 1
	buf = append(buf, []byte{2, 0x11, secondQuantizationTable}...) // component 2
	return buf
}
