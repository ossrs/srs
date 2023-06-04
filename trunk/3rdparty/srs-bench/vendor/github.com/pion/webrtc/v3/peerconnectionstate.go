// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// PeerConnectionState indicates the state of the PeerConnection.
type PeerConnectionState int

const (
	// PeerConnectionStateNew indicates that any of the ICETransports or
	// DTLSTransports are in the "new" state and none of the transports are
	// in the "connecting", "checking", "failed" or "disconnected" state, or
	// all transports are in the "closed" state, or there are no transports.
	PeerConnectionStateNew PeerConnectionState = iota + 1

	// PeerConnectionStateConnecting indicates that any of the
	// ICETransports or DTLSTransports are in the "connecting" or
	// "checking" state and none of them is in the "failed" state.
	PeerConnectionStateConnecting

	// PeerConnectionStateConnected indicates that all ICETransports and
	// DTLSTransports are in the "connected", "completed" or "closed" state
	// and at least one of them is in the "connected" or "completed" state.
	PeerConnectionStateConnected

	// PeerConnectionStateDisconnected indicates that any of the
	// ICETransports or DTLSTransports are in the "disconnected" state
	// and none of them are in the "failed" or "connecting" or "checking" state.
	PeerConnectionStateDisconnected

	// PeerConnectionStateFailed indicates that any of the ICETransports
	// or DTLSTransports are in a "failed" state.
	PeerConnectionStateFailed

	// PeerConnectionStateClosed indicates the peer connection is closed
	// and the isClosed member variable of PeerConnection is true.
	PeerConnectionStateClosed
)

// This is done this way because of a linter.
const (
	peerConnectionStateNewStr          = "new"
	peerConnectionStateConnectingStr   = "connecting"
	peerConnectionStateConnectedStr    = "connected"
	peerConnectionStateDisconnectedStr = "disconnected"
	peerConnectionStateFailedStr       = "failed"
	peerConnectionStateClosedStr       = "closed"
)

func newPeerConnectionState(raw string) PeerConnectionState {
	switch raw {
	case peerConnectionStateNewStr:
		return PeerConnectionStateNew
	case peerConnectionStateConnectingStr:
		return PeerConnectionStateConnecting
	case peerConnectionStateConnectedStr:
		return PeerConnectionStateConnected
	case peerConnectionStateDisconnectedStr:
		return PeerConnectionStateDisconnected
	case peerConnectionStateFailedStr:
		return PeerConnectionStateFailed
	case peerConnectionStateClosedStr:
		return PeerConnectionStateClosed
	default:
		return PeerConnectionState(Unknown)
	}
}

func (t PeerConnectionState) String() string {
	switch t {
	case PeerConnectionStateNew:
		return peerConnectionStateNewStr
	case PeerConnectionStateConnecting:
		return peerConnectionStateConnectingStr
	case PeerConnectionStateConnected:
		return peerConnectionStateConnectedStr
	case PeerConnectionStateDisconnected:
		return peerConnectionStateDisconnectedStr
	case PeerConnectionStateFailed:
		return peerConnectionStateFailedStr
	case PeerConnectionStateClosed:
		return peerConnectionStateClosedStr
	default:
		return ErrUnknownType.Error()
	}
}

type negotiationNeededState int

const (
	// NegotiationNeededStateEmpty not running and queue is empty
	negotiationNeededStateEmpty = iota
	// NegotiationNeededStateEmpty running and queue is empty
	negotiationNeededStateRun
	// NegotiationNeededStateEmpty running and queue
	negotiationNeededStateQueue
)
