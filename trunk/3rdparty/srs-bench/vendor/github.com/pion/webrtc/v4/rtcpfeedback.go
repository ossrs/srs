// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

const (
	// TypeRTCPFBTransportCC ..
	TypeRTCPFBTransportCC = "transport-cc"

	// TypeRTCPFBGoogREMB ..
	TypeRTCPFBGoogREMB = "goog-remb"

	// TypeRTCPFBACK ..
	TypeRTCPFBACK = "ack"

	// TypeRTCPFBCCM ..
	TypeRTCPFBCCM = "ccm"

	// TypeRTCPFBNACK ..
	TypeRTCPFBNACK = "nack"
)

// RTCPFeedback signals the connection to use additional RTCP packet types.
// https://draft.ortc.org/#dom-rtcrtcpfeedback
type RTCPFeedback struct {
	// Type is the type of feedback.
	// see: https://draft.ortc.org/#dom-rtcrtcpfeedback
	// valid: ack, ccm, nack, goog-remb, transport-cc
	Type string

	// The parameter value depends on the type.
	// For example, type="nack" parameter="pli" will send Picture Loss Indicator packets.
	Parameter string
}
