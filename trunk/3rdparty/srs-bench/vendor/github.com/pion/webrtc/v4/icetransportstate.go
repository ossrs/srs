// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import "github.com/pion/ice/v4"

// ICETransportState represents the current state of the ICE transport.
type ICETransportState int

const (
	// ICETransportStateUnknown is the enum's zero-value.
	ICETransportStateUnknown ICETransportState = iota

	// ICETransportStateNew indicates the ICETransport is waiting
	// for remote candidates to be supplied.
	ICETransportStateNew

	// ICETransportStateChecking indicates the ICETransport has
	// received at least one remote candidate, and a local and remote
	// ICECandidateComplete dictionary was not added as the last candidate.
	ICETransportStateChecking

	// ICETransportStateConnected indicates the ICETransport has
	// received a response to an outgoing connectivity check, or has
	// received incoming DTLS/media after a successful response to an
	// incoming connectivity check, but is still checking other candidate
	// pairs to see if there is a better connection.
	ICETransportStateConnected

	// ICETransportStateCompleted indicates the ICETransport tested
	// all appropriate candidate pairs and at least one functioning
	// candidate pair has been found.
	ICETransportStateCompleted

	// ICETransportStateFailed indicates the ICETransport the last
	// candidate was added and all appropriate candidate pairs have either
	// failed connectivity checks or have lost consent.
	ICETransportStateFailed

	// ICETransportStateDisconnected indicates the ICETransport has received
	// at least one local and remote candidate, but the final candidate was
	// received yet and all appropriate candidate pairs thus far have been
	// tested and failed.
	ICETransportStateDisconnected

	// ICETransportStateClosed indicates the ICETransport has shut down
	// and is no longer responding to STUN requests.
	ICETransportStateClosed
)

const (
	iceTransportStateNewStr          = "new"
	iceTransportStateCheckingStr     = "checking"
	iceTransportStateConnectedStr    = "connected"
	iceTransportStateCompletedStr    = "completed"
	iceTransportStateFailedStr       = "failed"
	iceTransportStateDisconnectedStr = "disconnected"
	iceTransportStateClosedStr       = "closed"
)

func newICETransportState(raw string) ICETransportState {
	switch raw {
	case iceTransportStateNewStr:
		return ICETransportStateNew
	case iceTransportStateCheckingStr:
		return ICETransportStateChecking
	case iceTransportStateConnectedStr:
		return ICETransportStateConnected
	case iceTransportStateCompletedStr:
		return ICETransportStateCompleted
	case iceTransportStateFailedStr:
		return ICETransportStateFailed
	case iceTransportStateDisconnectedStr:
		return ICETransportStateDisconnected
	case iceTransportStateClosedStr:
		return ICETransportStateClosed
	default:
		return ICETransportStateUnknown
	}
}

func (c ICETransportState) String() string {
	switch c {
	case ICETransportStateNew:
		return iceTransportStateNewStr
	case ICETransportStateChecking:
		return iceTransportStateCheckingStr
	case ICETransportStateConnected:
		return iceTransportStateConnectedStr
	case ICETransportStateCompleted:
		return iceTransportStateCompletedStr
	case ICETransportStateFailed:
		return iceTransportStateFailedStr
	case ICETransportStateDisconnected:
		return iceTransportStateDisconnectedStr
	case ICETransportStateClosed:
		return iceTransportStateClosedStr
	default:
		return ErrUnknownType.Error()
	}
}

func newICETransportStateFromICE(i ice.ConnectionState) ICETransportState {
	switch i {
	case ice.ConnectionStateNew:
		return ICETransportStateNew
	case ice.ConnectionStateChecking:
		return ICETransportStateChecking
	case ice.ConnectionStateConnected:
		return ICETransportStateConnected
	case ice.ConnectionStateCompleted:
		return ICETransportStateCompleted
	case ice.ConnectionStateFailed:
		return ICETransportStateFailed
	case ice.ConnectionStateDisconnected:
		return ICETransportStateDisconnected
	case ice.ConnectionStateClosed:
		return ICETransportStateClosed
	default:
		return ICETransportStateUnknown
	}
}

func (c ICETransportState) toICE() ice.ConnectionState {
	switch c {
	case ICETransportStateNew:
		return ice.ConnectionStateNew
	case ICETransportStateChecking:
		return ice.ConnectionStateChecking
	case ICETransportStateConnected:
		return ice.ConnectionStateConnected
	case ICETransportStateCompleted:
		return ice.ConnectionStateCompleted
	case ICETransportStateFailed:
		return ice.ConnectionStateFailed
	case ICETransportStateDisconnected:
		return ice.ConnectionStateDisconnected
	case ICETransportStateClosed:
		return ice.ConnectionStateClosed
	default:
		return ice.ConnectionStateUnknown
	}
}

// MarshalText implements encoding.TextMarshaler.
func (c ICETransportState) MarshalText() ([]byte, error) {
	return []byte(c.String()), nil
}

// UnmarshalText implements encoding.TextUnmarshaler.
func (c *ICETransportState) UnmarshalText(b []byte) error {
	*c = newICETransportState(string(b))

	return nil
}
