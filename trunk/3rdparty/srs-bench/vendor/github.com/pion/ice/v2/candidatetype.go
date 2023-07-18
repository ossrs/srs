// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

// CandidateType represents the type of candidate
type CandidateType byte

// CandidateType enum
const (
	CandidateTypeUnspecified CandidateType = iota
	CandidateTypeHost
	CandidateTypeServerReflexive
	CandidateTypePeerReflexive
	CandidateTypeRelay
)

// String makes CandidateType printable
func (c CandidateType) String() string {
	switch c {
	case CandidateTypeHost:
		return "host"
	case CandidateTypeServerReflexive:
		return "srflx"
	case CandidateTypePeerReflexive:
		return "prflx"
	case CandidateTypeRelay:
		return "relay"
	case CandidateTypeUnspecified:
		return "Unknown candidate type"
	}
	return "Unknown candidate type"
}

// Preference returns the preference weight of a CandidateType
//
// 4.1.2.2.  Guidelines for Choosing Type and Local Preferences
// The RECOMMENDED values are 126 for host candidates, 100
// for server reflexive candidates, 110 for peer reflexive candidates,
// and 0 for relayed candidates.
func (c CandidateType) Preference() uint16 {
	switch c {
	case CandidateTypeHost:
		return 126
	case CandidateTypePeerReflexive:
		return 110
	case CandidateTypeServerReflexive:
		return 100
	case CandidateTypeRelay, CandidateTypeUnspecified:
		return 0
	}
	return 0
}

func containsCandidateType(candidateType CandidateType, candidateTypeList []CandidateType) bool {
	if candidateTypeList == nil {
		return false
	}
	for _, ct := range candidateTypeList {
		if ct == candidateType {
			return true
		}
	}
	return false
}
