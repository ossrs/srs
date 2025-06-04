// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package client implements the API for a TURN client
package client

import (
	"net"

	"github.com/pion/stun/v3"
)

// Client is an interface for the public turn.Client in order to break cyclic dependencies
type Client interface {
	WriteTo(data []byte, to net.Addr) (int, error)
	PerformTransaction(msg *stun.Message, to net.Addr, dontWait bool) (TransactionResult, error)
	OnDeallocated(relayedAddr net.Addr)
}
