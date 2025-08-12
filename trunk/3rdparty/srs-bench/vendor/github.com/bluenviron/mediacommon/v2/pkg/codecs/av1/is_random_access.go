package av1

import (
	"fmt"
)

// IsRandomAccess2 checks whether a temporal unit can be randomly accessed.
func IsRandomAccess2(tu [][]byte) bool {
	for _, obu := range tu {
		var h OBUHeader
		err := h.Unmarshal(obu)
		if err == nil && h.Type == OBUTypeSequenceHeader {
			return true
		}
	}

	return false
}

// IsRandomAccess checks whether a temporal unit can be randomly accessed.
//
// Deprecated: replaced by IsRandomAccess2.
func IsRandomAccess(tu [][]byte) (bool, error) {
	if len(tu) == 0 {
		return false, fmt.Errorf("temporal unit is empty")
	}

	var h OBUHeader
	err := h.Unmarshal(tu[0])
	if err != nil {
		return false, err
	}

	return (h.Type == OBUTypeSequenceHeader), nil
}
