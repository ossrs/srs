package jpeg

import (
	"fmt"
)

// DefineRestartInterval is a DRI marker.
type DefineRestartInterval struct {
	Interval uint16
}

// Unmarshal decodes the marker.
func (m *DefineRestartInterval) Unmarshal(buf []byte) error {
	if len(buf) != 2 {
		return fmt.Errorf("unsupported DRI size of %d", len(buf))
	}

	m.Interval = uint16(buf[0])<<8 | uint16(buf[1])
	return nil
}
