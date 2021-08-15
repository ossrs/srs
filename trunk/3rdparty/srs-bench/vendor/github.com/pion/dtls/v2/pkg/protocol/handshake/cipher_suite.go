package handshake

import "encoding/binary"

func decodeCipherSuiteIDs(buf []byte) ([]uint16, error) {
	if len(buf) < 2 {
		return nil, errBufferTooSmall
	}
	cipherSuitesCount := int(binary.BigEndian.Uint16(buf[0:])) / 2
	rtrn := make([]uint16, cipherSuitesCount)
	for i := 0; i < cipherSuitesCount; i++ {
		if len(buf) < (i*2 + 4) {
			return nil, errBufferTooSmall
		}

		rtrn[i] = binary.BigEndian.Uint16(buf[(i*2)+2:])
	}
	return rtrn, nil
}

func encodeCipherSuiteIDs(cipherSuiteIDs []uint16) []byte {
	out := []byte{0x00, 0x00}
	binary.BigEndian.PutUint16(out[len(out)-2:], uint16(len(cipherSuiteIDs)*2))
	for _, id := range cipherSuiteIDs {
		out = append(out, []byte{0x00, 0x00}...)
		binary.BigEndian.PutUint16(out[len(out)-2:], id)
	}
	return out
}
