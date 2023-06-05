// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"encoding/json"
)

// RTCPMuxPolicy affects what ICE candidates are gathered to support
// non-multiplexed RTCP.
type RTCPMuxPolicy int

const (
	// RTCPMuxPolicyNegotiate indicates to gather ICE candidates for both
	// RTP and RTCP candidates. If the remote-endpoint is capable of
	// multiplexing RTCP, multiplex RTCP on the RTP candidates. If it is not,
	// use both the RTP and RTCP candidates separately.
	RTCPMuxPolicyNegotiate RTCPMuxPolicy = iota + 1

	// RTCPMuxPolicyRequire indicates to gather ICE candidates only for
	// RTP and multiplex RTCP on the RTP candidates. If the remote endpoint is
	// not capable of rtcp-mux, session negotiation will fail.
	RTCPMuxPolicyRequire
)

// This is done this way because of a linter.
const (
	rtcpMuxPolicyNegotiateStr = "negotiate"
	rtcpMuxPolicyRequireStr   = "require"
)

func newRTCPMuxPolicy(raw string) RTCPMuxPolicy {
	switch raw {
	case rtcpMuxPolicyNegotiateStr:
		return RTCPMuxPolicyNegotiate
	case rtcpMuxPolicyRequireStr:
		return RTCPMuxPolicyRequire
	default:
		return RTCPMuxPolicy(Unknown)
	}
}

func (t RTCPMuxPolicy) String() string {
	switch t {
	case RTCPMuxPolicyNegotiate:
		return rtcpMuxPolicyNegotiateStr
	case RTCPMuxPolicyRequire:
		return rtcpMuxPolicyRequireStr
	default:
		return ErrUnknownType.Error()
	}
}

// UnmarshalJSON parses the JSON-encoded data and stores the result
func (t *RTCPMuxPolicy) UnmarshalJSON(b []byte) error {
	var val string
	if err := json.Unmarshal(b, &val); err != nil {
		return err
	}

	*t = newRTCPMuxPolicy(val)
	return nil
}

// MarshalJSON returns the JSON encoding
func (t RTCPMuxPolicy) MarshalJSON() ([]byte, error) {
	return json.Marshal(t.String())
}
