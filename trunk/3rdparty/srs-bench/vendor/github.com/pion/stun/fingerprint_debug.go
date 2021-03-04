// +build debug

package stun

import "fmt"

// CRCMismatch represents CRC check error.
type CRCMismatch struct {
	Expected uint32
	Actual   uint32
}

func (m CRCMismatch) Error() string {
	return fmt.Sprintf("CRC mismatch: %x (expected) != %x (actual)",
		m.Expected,
		m.Actual,
	)
}
