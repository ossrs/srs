// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// ICERole describes the role ice.Agent is playing in selecting the
// preferred the candidate pair.
type ICERole int

const (
	// ICERoleUnknown is the enum's zero-value.
	ICERoleUnknown ICERole = iota

	// ICERoleControlling indicates that the ICE agent that is responsible
	// for selecting the final choice of candidate pairs and signaling them
	// through STUN and an updated offer, if needed. In any session, one agent
	// is always controlling. The other is the controlled agent.
	ICERoleControlling

	// ICERoleControlled indicates that an ICE agent that waits for the
	// controlling agent to select the final choice of candidate pairs.
	ICERoleControlled
)

// This is done this way because of a linter.
const (
	iceRoleControllingStr = "controlling"
	iceRoleControlledStr  = "controlled"
)

func newICERole(raw string) ICERole {
	switch raw {
	case iceRoleControllingStr:
		return ICERoleControlling
	case iceRoleControlledStr:
		return ICERoleControlled
	default:
		return ICERoleUnknown
	}
}

func (t ICERole) String() string {
	switch t {
	case ICERoleControlling:
		return iceRoleControllingStr
	case ICERoleControlled:
		return iceRoleControlledStr
	default:
		return ErrUnknownType.Error()
	}
}

// MarshalText implements encoding.TextMarshaler.
func (t ICERole) MarshalText() ([]byte, error) {
	return []byte(t.String()), nil
}

// UnmarshalText implements encoding.TextUnmarshaler.
func (t *ICERole) UnmarshalText(b []byte) error {
	*t = newICERole(string(b))

	return nil
}
