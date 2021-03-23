package dtls

import (
	"encoding/binary"
)

// Parse a big endian uint24
func bigEndianUint24(raw []byte) uint32 {
	if len(raw) < 3 {
		return 0
	}

	rawCopy := make([]byte, 4)
	copy(rawCopy[1:], raw)
	return binary.BigEndian.Uint32(rawCopy)
}

func putBigEndianUint24(out []byte, in uint32) {
	tmp := make([]byte, 4)
	binary.BigEndian.PutUint32(tmp, in)
	copy(out, tmp[1:])
}

func putBigEndianUint48(out []byte, in uint64) {
	tmp := make([]byte, 8)
	binary.BigEndian.PutUint64(tmp, in)
	copy(out, tmp[2:])
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

// examinePadding returns, in constant time, the length of the padding to remove
// from the end of payload. It also returns a byte which is equal to 255 if the
// padding was valid and 0 otherwise. See RFC 2246, Section 6.2.3.2.
//
// https://github.com/golang/go/blob/039c2081d1178f90a8fa2f4e6958693129f8de33/src/crypto/tls/conn.go#L245
func examinePadding(payload []byte) (toRemove int, good byte) {
	if len(payload) < 1 {
		return 0, 0
	}

	paddingLen := payload[len(payload)-1]
	t := uint(len(payload)-1) - uint(paddingLen)
	// if len(payload) >= (paddingLen - 1) then the MSB of t is zero
	good = byte(int32(^t) >> 31)

	// The maximum possible padding length plus the actual length field
	toCheck := 256
	// The length of the padded data is public, so we can use an if here
	if toCheck > len(payload) {
		toCheck = len(payload)
	}

	for i := 0; i < toCheck; i++ {
		t := uint(paddingLen) - uint(i)
		// if i <= paddingLen then the MSB of t is zero
		mask := byte(int32(^t) >> 31)
		b := payload[len(payload)-1-i]
		good &^= mask&paddingLen ^ mask&b
	}

	// We AND together the bits of good and replicate the result across
	// all the bits.
	good &= good << 4
	good &= good << 2
	good &= good << 1
	good = uint8(int8(good) >> 7)

	toRemove = int(paddingLen) + 1

	return toRemove, good
}

func findMatchingSRTPProfile(a, b []SRTPProtectionProfile) (SRTPProtectionProfile, bool) {
	for _, aProfile := range a {
		for _, bProfile := range b {
			if aProfile == bProfile {
				return aProfile, true
			}
		}
	}
	return 0, false
}

func findMatchingCipherSuite(a, b []cipherSuite) (cipherSuite, bool) { //nolint
	for _, aSuite := range a {
		for _, bSuite := range b {
			if aSuite.ID() == bSuite.ID() {
				return aSuite, true
			}
		}
	}
	return nil, false
}

func splitBytes(bytes []byte, splitLen int) [][]byte {
	splitBytes := make([][]byte, 0)
	numBytes := len(bytes)
	for i := 0; i < numBytes; i += splitLen {
		j := i + splitLen
		if j > numBytes {
			j = numBytes
		}

		splitBytes = append(splitBytes, bytes[i:j])
	}

	return splitBytes
}
