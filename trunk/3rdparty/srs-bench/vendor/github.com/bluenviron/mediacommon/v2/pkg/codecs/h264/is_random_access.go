package h264

// IsRandomAccess checks whether the access unit can be randomly accessed.
func IsRandomAccess(au [][]byte) bool {
	for _, nalu := range au {
		typ := NALUType(nalu[0] & 0x1F)
		if typ == NALUTypeIDR {
			return true
		}
	}
	return false
}
