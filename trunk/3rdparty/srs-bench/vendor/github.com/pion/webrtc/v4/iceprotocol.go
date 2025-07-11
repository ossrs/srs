// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"fmt"
	"strings"
)

// ICEProtocol indicates the transport protocol type that is used in the
// ice.URL structure.
type ICEProtocol int

const (
	// ICEProtocolUnknown is the enum's zero-value.
	ICEProtocolUnknown ICEProtocol = iota

	// ICEProtocolUDP indicates the URL uses a UDP transport.
	ICEProtocolUDP

	// ICEProtocolTCP indicates the URL uses a TCP transport.
	ICEProtocolTCP
)

// This is done this way because of a linter.
const (
	iceProtocolUDPStr = "udp"
	iceProtocolTCPStr = "tcp"
)

// NewICEProtocol takes a string and converts it to ICEProtocol.
func NewICEProtocol(raw string) (ICEProtocol, error) {
	switch {
	case strings.EqualFold(iceProtocolUDPStr, raw):
		return ICEProtocolUDP, nil
	case strings.EqualFold(iceProtocolTCPStr, raw):
		return ICEProtocolTCP, nil
	default:
		return ICEProtocolUnknown, fmt.Errorf("%w: %s", errICEProtocolUnknown, raw)
	}
}

func (t ICEProtocol) String() string {
	switch t {
	case ICEProtocolUDP:
		return iceProtocolUDPStr
	case ICEProtocolTCP:
		return iceProtocolTCPStr
	default:
		return ErrUnknownType.Error()
	}
}
