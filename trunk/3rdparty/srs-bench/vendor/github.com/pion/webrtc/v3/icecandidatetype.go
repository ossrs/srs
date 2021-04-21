package webrtc

import (
	"fmt"

	"github.com/pion/ice/v2"
)

// ICECandidateType represents the type of the ICE candidate used.
type ICECandidateType int

const (
	// ICECandidateTypeHost indicates that the candidate is of Host type as
	// described in https://tools.ietf.org/html/rfc8445#section-5.1.1.1. A
	// candidate obtained by binding to a specific port from an IP address on
	// the host. This includes IP addresses on physical interfaces and logical
	// ones, such as ones obtained through VPNs.
	ICECandidateTypeHost ICECandidateType = iota + 1

	// ICECandidateTypeSrflx indicates the the candidate is of Server
	// Reflexive type as described
	// https://tools.ietf.org/html/rfc8445#section-5.1.1.2. A candidate type
	// whose IP address and port are a binding allocated by a NAT for an ICE
	// agent after it sends a packet through the NAT to a server, such as a
	// STUN server.
	ICECandidateTypeSrflx

	// ICECandidateTypePrflx indicates that the candidate is of Peer
	// Reflexive type. A candidate type whose IP address and port are a binding
	// allocated by a NAT for an ICE agent after it sends a packet through the
	// NAT to its peer.
	ICECandidateTypePrflx

	// ICECandidateTypeRelay indicates the the candidate is of Relay type as
	// described in https://tools.ietf.org/html/rfc8445#section-5.1.1.2. A
	// candidate type obtained from a relay server, such as a TURN server.
	ICECandidateTypeRelay
)

// This is done this way because of a linter.
const (
	iceCandidateTypeHostStr  = "host"
	iceCandidateTypeSrflxStr = "srflx"
	iceCandidateTypePrflxStr = "prflx"
	iceCandidateTypeRelayStr = "relay"
)

// NewICECandidateType takes a string and converts it into ICECandidateType
func NewICECandidateType(raw string) (ICECandidateType, error) {
	switch raw {
	case iceCandidateTypeHostStr:
		return ICECandidateTypeHost, nil
	case iceCandidateTypeSrflxStr:
		return ICECandidateTypeSrflx, nil
	case iceCandidateTypePrflxStr:
		return ICECandidateTypePrflx, nil
	case iceCandidateTypeRelayStr:
		return ICECandidateTypeRelay, nil
	default:
		return ICECandidateType(Unknown), fmt.Errorf("%w: %s", errICECandidateTypeUnknown, raw)
	}
}

func (t ICECandidateType) String() string {
	switch t {
	case ICECandidateTypeHost:
		return iceCandidateTypeHostStr
	case ICECandidateTypeSrflx:
		return iceCandidateTypeSrflxStr
	case ICECandidateTypePrflx:
		return iceCandidateTypePrflxStr
	case ICECandidateTypeRelay:
		return iceCandidateTypeRelayStr
	default:
		return ErrUnknownType.Error()
	}
}

func getCandidateType(candidateType ice.CandidateType) (ICECandidateType, error) {
	switch candidateType {
	case ice.CandidateTypeHost:
		return ICECandidateTypeHost, nil
	case ice.CandidateTypeServerReflexive:
		return ICECandidateTypeSrflx, nil
	case ice.CandidateTypePeerReflexive:
		return ICECandidateTypePrflx, nil
	case ice.CandidateTypeRelay:
		return ICECandidateTypeRelay, nil
	default:
		// NOTE: this should never happen[tm]
		err := fmt.Errorf("%w: %s", errICEInvalidConvertCandidateType, candidateType.String())
		return ICECandidateType(Unknown), err
	}
}
