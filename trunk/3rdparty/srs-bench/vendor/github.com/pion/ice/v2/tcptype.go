// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import "strings"

// TCPType is the type of ICE TCP candidate as described in
// https://tools.ietf.org/html/rfc6544#section-4.5
type TCPType int

const (
	// TCPTypeUnspecified is the default value. For example UDP candidates do not
	// need this field.
	TCPTypeUnspecified TCPType = iota
	// TCPTypeActive is active TCP candidate, which initiates TCP connections.
	TCPTypeActive
	// TCPTypePassive is passive TCP candidate, only accepts TCP connections.
	TCPTypePassive
	// TCPTypeSimultaneousOpen is like active and passive at the same time.
	TCPTypeSimultaneousOpen
)

// NewTCPType creates a new TCPType from string.
func NewTCPType(value string) TCPType {
	switch strings.ToLower(value) {
	case "active":
		return TCPTypeActive
	case "passive":
		return TCPTypePassive
	case "so":
		return TCPTypeSimultaneousOpen
	default:
		return TCPTypeUnspecified
	}
}

func (t TCPType) String() string {
	switch t {
	case TCPTypeUnspecified:
		return ""
	case TCPTypeActive:
		return "active"
	case TCPTypePassive:
		return "passive"
	case TCPTypeSimultaneousOpen:
		return "so"
	default:
		return ErrUnknownType.Error()
	}
}
