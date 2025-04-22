package jpeg

import (
	"fmt"
)

// StartOfScan is a SOS marker.
type StartOfScan struct{}

// Unmarshal decodes the marker.
func (StartOfScan) Unmarshal(buf []byte) error {
	if len(buf) != 10 {
		return fmt.Errorf("unsupported SOS size of %d", len(buf))
	}
	return nil
}

// Marshal encodes the marker.
func (StartOfScan) Marshal(buf []byte) []byte {
	buf = append(buf, []byte{0xFF, MarkerStartOfScan}...)
	buf = append(buf, []byte{0, 12}...)   // length
	buf = append(buf, []byte{3}...)       // components
	buf = append(buf, []byte{0, 0}...)    // component 0
	buf = append(buf, []byte{1, 0x11}...) // component 1
	buf = append(buf, []byte{2, 0x11}...) // component 2
	buf = append(buf, []byte{0, 63, 0}...)
	return buf
}
