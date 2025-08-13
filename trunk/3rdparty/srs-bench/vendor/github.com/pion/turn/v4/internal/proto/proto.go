// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package proto implements RFC 5766 Traversal Using Relays around NAT.
package proto

import (
	"github.com/pion/stun/v3"
)

// Default ports for TURN from RFC 5766 Section 4.
const (
	// DefaultPort for TURN is same as STUN.
	DefaultPort = stun.DefaultPort
	// DefaultTLSPort is for TURN over TLS and is same as STUN.
	DefaultTLSPort = stun.DefaultTLSPort
)

// CreatePermissionRequest is shorthand for create permission request type.
func CreatePermissionRequest() stun.MessageType {
	return stun.NewType(stun.MethodCreatePermission, stun.ClassRequest)
}

// AllocateRequest is shorthand for allocation request message type.
func AllocateRequest() stun.MessageType { return stun.NewType(stun.MethodAllocate, stun.ClassRequest) }

// SendIndication is shorthand for send indication message type.
func SendIndication() stun.MessageType { return stun.NewType(stun.MethodSend, stun.ClassIndication) }

// RefreshRequest is shorthand for refresh request message type.
func RefreshRequest() stun.MessageType { return stun.NewType(stun.MethodRefresh, stun.ClassRequest) }
