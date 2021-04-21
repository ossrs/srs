package webrtc

import (
	"fmt"
	"strings"
)

// ICEProtocol indicates the transport protocol type that is used in the
// ice.URL structure.
type ICEProtocol int

const (
	// ICEProtocolUDP indicates the URL uses a UDP transport.
	ICEProtocolUDP ICEProtocol = iota + 1

	// ICEProtocolTCP indicates the URL uses a TCP transport.
	ICEProtocolTCP
)

// This is done this way because of a linter.
const (
	iceProtocolUDPStr = "udp"
	iceProtocolTCPStr = "tcp"
)

// NewICEProtocol takes a string and converts it to ICEProtocol
func NewICEProtocol(raw string) (ICEProtocol, error) {
	switch {
	case strings.EqualFold(iceProtocolUDPStr, raw):
		return ICEProtocolUDP, nil
	case strings.EqualFold(iceProtocolTCPStr, raw):
		return ICEProtocolTCP, nil
	default:
		return ICEProtocol(Unknown), fmt.Errorf("%w: %s", errICEProtocolUnknown, raw)
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
