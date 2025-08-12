package jpeg

import (
	"fmt"
)

// QuantizationTable is a DQT quantization table.
type QuantizationTable struct {
	ID        uint8
	Precision uint8
	Data      []byte
}

// DefineQuantizationTable is a DQT marker.
type DefineQuantizationTable struct {
	Tables []QuantizationTable
}

// Unmarshal decodes the marker.
func (m *DefineQuantizationTable) Unmarshal(buf []byte) error {
	for len(buf) != 0 {
		id := buf[0] & 0x0F
		precision := buf[0] >> 4
		buf = buf[1:]
		if precision != 0 {
			return fmt.Errorf("Precision %d is not supported", precision)
		}

		if len(buf) < 64 {
			return fmt.Errorf("image is too short")
		}

		m.Tables = append(m.Tables, QuantizationTable{
			ID:        id,
			Precision: precision,
			Data:      buf[:64],
		})
		buf = buf[64:]
	}

	return nil
}

// Marshal encodes the marker.
func (m DefineQuantizationTable) Marshal(buf []byte) []byte {
	buf = append(buf, []byte{0xFF, MarkerDefineQuantizationTable}...)

	// length
	s := 2
	for _, t := range m.Tables {
		s += 1 + len(t.Data)
	}
	buf = append(buf, []byte{byte(s >> 8), byte(s)}...)

	for _, t := range m.Tables {
		buf = append(buf, []byte{(t.ID)}...)
		buf = append(buf, t.Data...)
	}

	return buf
}
