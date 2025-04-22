package h264

// EmulationPreventionRemove removes emulation prevention bytes from a NALU.
// Specification: ITU-T Rec. H.264, 7.4.1 NAL unit semantics
func EmulationPreventionRemove(nalu []byte) []byte {
	// 0x00 0x00 0x03 0x00 -> 0x00 0x00 0x00
	// 0x00 0x00 0x03 0x01 -> 0x00 0x00 0x01
	// 0x00 0x00 0x03 0x02 -> 0x00 0x00 0x02
	// 0x00 0x00 0x03 0x03 -> 0x00 0x00 0x03

	l := len(nalu)
	n := l

	for i := 2; i < l; i++ {
		if nalu[i-2] == 0 && nalu[i-1] == 0 && nalu[i] == 3 {
			n--
			i += 2
		}
	}

	ret := make([]byte, n)
	pos := 0
	start := 0

	for i := 2; i < l; i++ {
		if nalu[i-2] == 0 && nalu[i-1] == 0 && nalu[i] == 3 {
			pos += copy(ret[pos:], nalu[start:i])
			start = i + 1
			i += 2
		}
	}

	copy(ret[pos:], nalu[start:])

	return ret
}
