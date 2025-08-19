// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// RTPEncodingParameters provides information relating to both encoding and decoding.
// This is a subset of the RFC since Pion WebRTC doesn't implement encoding itself
// http://draft.ortc.org/#dom-rtcrtpencodingparameters
type RTPEncodingParameters struct {
	RTPCodingParameters
}
