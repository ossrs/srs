// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// ICEConnectionState indicates signaling state of the ICE Connection.
type ICEConnectionState int

const (
	// ICEConnectionStateNew indicates that any of the ICETransports are
	// in the "new" state and none of them are in the "checking", "disconnected"
	// or "failed" state, or all ICETransports are in the "closed" state, or
	// there are no transports.
	ICEConnectionStateNew ICEConnectionState = iota + 1

	// ICEConnectionStateChecking indicates that any of the ICETransports
	// are in the "checking" state and none of them are in the "disconnected"
	// or "failed" state.
	ICEConnectionStateChecking

	// ICEConnectionStateConnected indicates that all ICETransports are
	// in the "connected", "completed" or "closed" state and at least one of
	// them is in the "connected" state.
	ICEConnectionStateConnected

	// ICEConnectionStateCompleted indicates that all ICETransports are
	// in the "completed" or "closed" state and at least one of them is in the
	// "completed" state.
	ICEConnectionStateCompleted

	// ICEConnectionStateDisconnected indicates that any of the
	// ICETransports are in the "disconnected" state and none of them are
	// in the "failed" state.
	ICEConnectionStateDisconnected

	// ICEConnectionStateFailed indicates that any of the ICETransports
	// are in the "failed" state.
	ICEConnectionStateFailed

	// ICEConnectionStateClosed indicates that the PeerConnection's
	// isClosed is true.
	ICEConnectionStateClosed
)

// This is done this way because of a linter.
const (
	iceConnectionStateNewStr          = "new"
	iceConnectionStateCheckingStr     = "checking"
	iceConnectionStateConnectedStr    = "connected"
	iceConnectionStateCompletedStr    = "completed"
	iceConnectionStateDisconnectedStr = "disconnected"
	iceConnectionStateFailedStr       = "failed"
	iceConnectionStateClosedStr       = "closed"
)

// NewICEConnectionState takes a string and converts it to ICEConnectionState
func NewICEConnectionState(raw string) ICEConnectionState {
	switch raw {
	case iceConnectionStateNewStr:
		return ICEConnectionStateNew
	case iceConnectionStateCheckingStr:
		return ICEConnectionStateChecking
	case iceConnectionStateConnectedStr:
		return ICEConnectionStateConnected
	case iceConnectionStateCompletedStr:
		return ICEConnectionStateCompleted
	case iceConnectionStateDisconnectedStr:
		return ICEConnectionStateDisconnected
	case iceConnectionStateFailedStr:
		return ICEConnectionStateFailed
	case iceConnectionStateClosedStr:
		return ICEConnectionStateClosed
	default:
		return ICEConnectionState(Unknown)
	}
}

func (c ICEConnectionState) String() string {
	switch c {
	case ICEConnectionStateNew:
		return iceConnectionStateNewStr
	case ICEConnectionStateChecking:
		return iceConnectionStateCheckingStr
	case ICEConnectionStateConnected:
		return iceConnectionStateConnectedStr
	case ICEConnectionStateCompleted:
		return iceConnectionStateCompletedStr
	case ICEConnectionStateDisconnected:
		return iceConnectionStateDisconnectedStr
	case ICEConnectionStateFailed:
		return iceConnectionStateFailedStr
	case ICEConnectionStateClosed:
		return iceConnectionStateClosedStr
	default:
		return ErrUnknownType.Error()
	}
}
