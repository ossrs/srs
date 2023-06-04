// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

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

func findMatchingCipherSuite(a, b []CipherSuite) (CipherSuite, bool) {
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
