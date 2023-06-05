// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"fmt"
	"sync/atomic"

	"github.com/pion/webrtc/v3/pkg/rtcerr"
)

type stateChangeOp int

const (
	stateChangeOpSetLocal stateChangeOp = iota + 1
	stateChangeOpSetRemote
)

func (op stateChangeOp) String() string {
	switch op {
	case stateChangeOpSetLocal:
		return "SetLocal"
	case stateChangeOpSetRemote:
		return "SetRemote"
	default:
		return "Unknown State Change Operation"
	}
}

// SignalingState indicates the signaling state of the offer/answer process.
type SignalingState int32

const (
	// SignalingStateStable indicates there is no offer/answer exchange in
	// progress. This is also the initial state, in which case the local and
	// remote descriptions are nil.
	SignalingStateStable SignalingState = iota + 1

	// SignalingStateHaveLocalOffer indicates that a local description, of
	// type "offer", has been successfully applied.
	SignalingStateHaveLocalOffer

	// SignalingStateHaveRemoteOffer indicates that a remote description, of
	// type "offer", has been successfully applied.
	SignalingStateHaveRemoteOffer

	// SignalingStateHaveLocalPranswer indicates that a remote description
	// of type "offer" has been successfully applied and a local description
	// of type "pranswer" has been successfully applied.
	SignalingStateHaveLocalPranswer

	// SignalingStateHaveRemotePranswer indicates that a local description
	// of type "offer" has been successfully applied and a remote description
	// of type "pranswer" has been successfully applied.
	SignalingStateHaveRemotePranswer

	// SignalingStateClosed indicates The PeerConnection has been closed.
	SignalingStateClosed
)

// This is done this way because of a linter.
const (
	signalingStateStableStr             = "stable"
	signalingStateHaveLocalOfferStr     = "have-local-offer"
	signalingStateHaveRemoteOfferStr    = "have-remote-offer"
	signalingStateHaveLocalPranswerStr  = "have-local-pranswer"
	signalingStateHaveRemotePranswerStr = "have-remote-pranswer"
	signalingStateClosedStr             = "closed"
)

func newSignalingState(raw string) SignalingState {
	switch raw {
	case signalingStateStableStr:
		return SignalingStateStable
	case signalingStateHaveLocalOfferStr:
		return SignalingStateHaveLocalOffer
	case signalingStateHaveRemoteOfferStr:
		return SignalingStateHaveRemoteOffer
	case signalingStateHaveLocalPranswerStr:
		return SignalingStateHaveLocalPranswer
	case signalingStateHaveRemotePranswerStr:
		return SignalingStateHaveRemotePranswer
	case signalingStateClosedStr:
		return SignalingStateClosed
	default:
		return SignalingState(Unknown)
	}
}

func (t SignalingState) String() string {
	switch t {
	case SignalingStateStable:
		return signalingStateStableStr
	case SignalingStateHaveLocalOffer:
		return signalingStateHaveLocalOfferStr
	case SignalingStateHaveRemoteOffer:
		return signalingStateHaveRemoteOfferStr
	case SignalingStateHaveLocalPranswer:
		return signalingStateHaveLocalPranswerStr
	case SignalingStateHaveRemotePranswer:
		return signalingStateHaveRemotePranswerStr
	case SignalingStateClosed:
		return signalingStateClosedStr
	default:
		return ErrUnknownType.Error()
	}
}

// Get thread safe read value
func (t *SignalingState) Get() SignalingState {
	return SignalingState(atomic.LoadInt32((*int32)(t)))
}

// Set thread safe write value
func (t *SignalingState) Set(state SignalingState) {
	atomic.StoreInt32((*int32)(t), int32(state))
}

func checkNextSignalingState(cur, next SignalingState, op stateChangeOp, sdpType SDPType) (SignalingState, error) { // nolint:gocognit
	// Special case for rollbacks
	if sdpType == SDPTypeRollback && cur == SignalingStateStable {
		return cur, &rtcerr.InvalidModificationError{
			Err: errSignalingStateCannotRollback,
		}
	}

	// 4.3.1 valid state transitions
	switch cur { // nolint:exhaustive
	case SignalingStateStable:
		switch op {
		case stateChangeOpSetLocal:
			// stable->SetLocal(offer)->have-local-offer
			if sdpType == SDPTypeOffer && next == SignalingStateHaveLocalOffer {
				return next, nil
			}
		case stateChangeOpSetRemote:
			// stable->SetRemote(offer)->have-remote-offer
			if sdpType == SDPTypeOffer && next == SignalingStateHaveRemoteOffer {
				return next, nil
			}
		}
	case SignalingStateHaveLocalOffer:
		if op == stateChangeOpSetRemote {
			switch sdpType { // nolint:exhaustive
			// have-local-offer->SetRemote(answer)->stable
			case SDPTypeAnswer:
				if next == SignalingStateStable {
					return next, nil
				}
			// have-local-offer->SetRemote(pranswer)->have-remote-pranswer
			case SDPTypePranswer:
				if next == SignalingStateHaveRemotePranswer {
					return next, nil
				}
			}
		}
	case SignalingStateHaveRemotePranswer:
		if op == stateChangeOpSetRemote && sdpType == SDPTypeAnswer {
			// have-remote-pranswer->SetRemote(answer)->stable
			if next == SignalingStateStable {
				return next, nil
			}
		}
	case SignalingStateHaveRemoteOffer:
		if op == stateChangeOpSetLocal {
			switch sdpType { // nolint:exhaustive
			// have-remote-offer->SetLocal(answer)->stable
			case SDPTypeAnswer:
				if next == SignalingStateStable {
					return next, nil
				}
			// have-remote-offer->SetLocal(pranswer)->have-local-pranswer
			case SDPTypePranswer:
				if next == SignalingStateHaveLocalPranswer {
					return next, nil
				}
			}
		}
	case SignalingStateHaveLocalPranswer:
		if op == stateChangeOpSetLocal && sdpType == SDPTypeAnswer {
			// have-local-pranswer->SetLocal(answer)->stable
			if next == SignalingStateStable {
				return next, nil
			}
		}
	}
	return cur, &rtcerr.InvalidModificationError{
		Err: fmt.Errorf("%w: %s->%s(%s)->%s", errSignalingStateProposedTransitionInvalid, cur, op, sdpType, next),
	}
}
