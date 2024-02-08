// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// RTPSendParameters contains the RTP stack settings used by receivers
type RTPSendParameters struct {
	RTPParameters
	Encodings []RTPEncodingParameters
}
