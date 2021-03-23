package webrtc

import (
	"encoding/json"
	"strings"
)

// SDPType describes the type of an SessionDescription.
type SDPType int

const (
	// SDPTypeOffer indicates that a description MUST be treated as an SDP
	// offer.
	SDPTypeOffer SDPType = iota + 1

	// SDPTypePranswer indicates that a description MUST be treated as an
	// SDP answer, but not a final answer. A description used as an SDP
	// pranswer may be applied as a response to an SDP offer, or an update to
	// a previously sent SDP pranswer.
	SDPTypePranswer

	// SDPTypeAnswer indicates that a description MUST be treated as an SDP
	// final answer, and the offer-answer exchange MUST be considered complete.
	// A description used as an SDP answer may be applied as a response to an
	// SDP offer or as an update to a previously sent SDP pranswer.
	SDPTypeAnswer

	// SDPTypeRollback indicates that a description MUST be treated as
	// canceling the current SDP negotiation and moving the SDP offer and
	// answer back to what it was in the previous stable state. Note the
	// local or remote SDP descriptions in the previous stable state could be
	// null if there has not yet been a successful offer-answer negotiation.
	SDPTypeRollback
)

// This is done this way because of a linter.
const (
	sdpTypeOfferStr    = "offer"
	sdpTypePranswerStr = "pranswer"
	sdpTypeAnswerStr   = "answer"
	sdpTypeRollbackStr = "rollback"
)

// NewSDPType creates an SDPType from a string
func NewSDPType(raw string) SDPType {
	switch raw {
	case sdpTypeOfferStr:
		return SDPTypeOffer
	case sdpTypePranswerStr:
		return SDPTypePranswer
	case sdpTypeAnswerStr:
		return SDPTypeAnswer
	case sdpTypeRollbackStr:
		return SDPTypeRollback
	default:
		return SDPType(Unknown)
	}
}

func (t SDPType) String() string {
	switch t {
	case SDPTypeOffer:
		return sdpTypeOfferStr
	case SDPTypePranswer:
		return sdpTypePranswerStr
	case SDPTypeAnswer:
		return sdpTypeAnswerStr
	case SDPTypeRollback:
		return sdpTypeRollbackStr
	default:
		return ErrUnknownType.Error()
	}
}

// MarshalJSON enables JSON marshaling of a SDPType
func (t SDPType) MarshalJSON() ([]byte, error) {
	return json.Marshal(t.String())
}

// UnmarshalJSON enables JSON unmarshaling of a SDPType
func (t *SDPType) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return err
	}
	switch strings.ToLower(s) {
	default:
		return ErrUnknownType
	case "offer":
		*t = SDPTypeOffer
	case "pranswer":
		*t = SDPTypePranswer
	case "answer":
		*t = SDPTypeAnswer
	case "rollback":
		*t = SDPTypeRollback
	}

	return nil
}
