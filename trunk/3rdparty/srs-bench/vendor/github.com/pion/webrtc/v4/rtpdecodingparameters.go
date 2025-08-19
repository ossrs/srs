// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// RTPDecodingParameters provides information relating to both encoding and decoding.
// This is a subset of the RFC since Pion WebRTC doesn't implement decoding itself
// http://draft.ortc.org/#dom-rtcrtpdecodingparameters
type RTPDecodingParameters struct {
	RTPCodingParameters
}
