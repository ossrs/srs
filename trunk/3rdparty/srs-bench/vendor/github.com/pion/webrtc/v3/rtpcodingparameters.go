// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// RTPRtxParameters dictionary contains information relating to retransmission (RTX) settings.
// https://draft.ortc.org/#dom-rtcrtprtxparameters
type RTPRtxParameters struct {
	SSRC SSRC `json:"ssrc"`
}

// RTPCodingParameters provides information relating to both encoding and decoding.
// This is a subset of the RFC since Pion WebRTC doesn't implement encoding/decoding itself
// http://draft.ortc.org/#dom-rtcrtpcodingparameters
type RTPCodingParameters struct {
	RID         string           `json:"rid"`
	SSRC        SSRC             `json:"ssrc"`
	PayloadType PayloadType      `json:"payloadType"`
	RTX         RTPRtxParameters `json:"rtx"`
}
