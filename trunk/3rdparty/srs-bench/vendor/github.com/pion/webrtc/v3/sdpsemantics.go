// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"encoding/json"
)

// SDPSemantics determines which style of SDP offers and answers
// can be used
type SDPSemantics int

const (
	// SDPSemanticsUnifiedPlan uses unified-plan offers and answers
	// (the default in Chrome since M72)
	// https://tools.ietf.org/html/draft-roach-mmusic-unified-plan-00
	SDPSemanticsUnifiedPlan SDPSemantics = iota

	// SDPSemanticsPlanB uses plan-b offers and answers
	// NB: This format should be considered deprecated
	// https://tools.ietf.org/html/draft-uberti-rtcweb-plan-00
	SDPSemanticsPlanB

	// SDPSemanticsUnifiedPlanWithFallback prefers unified-plan
	// offers and answers, but will respond to a plan-b offer
	// with a plan-b answer
	SDPSemanticsUnifiedPlanWithFallback
)

const (
	sdpSemanticsUnifiedPlanWithFallback = "unified-plan-with-fallback"
	sdpSemanticsUnifiedPlan             = "unified-plan"
	sdpSemanticsPlanB                   = "plan-b"
)

func newSDPSemantics(raw string) SDPSemantics {
	switch raw {
	case sdpSemanticsUnifiedPlan:
		return SDPSemanticsUnifiedPlan
	case sdpSemanticsPlanB:
		return SDPSemanticsPlanB
	case sdpSemanticsUnifiedPlanWithFallback:
		return SDPSemanticsUnifiedPlanWithFallback
	default:
		return SDPSemantics(Unknown)
	}
}

func (s SDPSemantics) String() string {
	switch s {
	case SDPSemanticsUnifiedPlanWithFallback:
		return sdpSemanticsUnifiedPlanWithFallback
	case SDPSemanticsUnifiedPlan:
		return sdpSemanticsUnifiedPlan
	case SDPSemanticsPlanB:
		return sdpSemanticsPlanB
	default:
		return ErrUnknownType.Error()
	}
}

// UnmarshalJSON parses the JSON-encoded data and stores the result
func (s *SDPSemantics) UnmarshalJSON(b []byte) error {
	var val string
	if err := json.Unmarshal(b, &val); err != nil {
		return err
	}

	*s = newSDPSemantics(val)
	return nil
}

// MarshalJSON returns the JSON encoding
func (s SDPSemantics) MarshalJSON() ([]byte, error) {
	return json.Marshal(s.String())
}
