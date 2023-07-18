// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package mux

// MatchFunc allows custom logic for mapping packets to an Endpoint
type MatchFunc func([]byte) bool

// MatchAll always returns true
func MatchAll([]byte) bool {
	return true
}

// MatchRange returns true if the first byte of buf is in [lower..upper]
func MatchRange(lower, upper byte, buf []byte) bool {
	if len(buf) < 1 {
		return false
	}
	b := buf[0]
	return b >= lower && b <= upper
}

// MatchFuncs as described in RFC7983
// https://tools.ietf.org/html/rfc7983
//              +----------------+
//              |        [0..3] -+--> forward to STUN
//              |                |
//              |      [16..19] -+--> forward to ZRTP
//              |                |
//  packet -->  |      [20..63] -+--> forward to DTLS
//              |                |
//              |      [64..79] -+--> forward to TURN Channel
//              |                |
//              |    [128..191] -+--> forward to RTP/RTCP
//              +----------------+

// MatchDTLS is a MatchFunc that accepts packets with the first byte in [20..63]
// as defied in RFC7983
func MatchDTLS(b []byte) bool {
	return MatchRange(20, 63, b)
}

// MatchSRTPOrSRTCP is a MatchFunc that accepts packets with the first byte in [128..191]
// as defied in RFC7983
func MatchSRTPOrSRTCP(b []byte) bool {
	return MatchRange(128, 191, b)
}

func isRTCP(buf []byte) bool {
	// Not long enough to determine RTP/RTCP
	if len(buf) < 4 {
		return false
	}
	return buf[1] >= 192 && buf[1] <= 223
}

// MatchSRTP is a MatchFunc that only matches SRTP and not SRTCP
func MatchSRTP(buf []byte) bool {
	return MatchSRTPOrSRTCP(buf) && !isRTCP(buf)
}

// MatchSRTCP is a MatchFunc that only matches SRTCP and not SRTP
func MatchSRTCP(buf []byte) bool {
	return MatchSRTPOrSRTCP(buf) && isRTCP(buf)
}
