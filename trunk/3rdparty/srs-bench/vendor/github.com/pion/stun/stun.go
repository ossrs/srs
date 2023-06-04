// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package stun implements Session Traversal Utilities for NAT (STUN) RFC 5389.
//
// The stun package is intended to use by package that implements extension
// to STUN (e.g. TURN) or client/server applications.
//
// Most methods are designed to be zero allocations. If it is not enough,
// low-level methods are available. On other hand, there are helpers that
// reduce code repeat.
//
// See examples for Message for basic usage, or https://github.com/pion/turn
// package for example of stun extension implementation.
package stun

import (
	"encoding/binary"
	"io"
)

// bin is shorthand to binary.BigEndian.
var bin = binary.BigEndian //nolint:gochecknoglobals

func readFullOrPanic(r io.Reader, v []byte) int {
	n, err := io.ReadFull(r, v)
	if err != nil {
		panic(err) //nolint
	}
	return n
}

func writeOrPanic(w io.Writer, v []byte) int {
	n, err := w.Write(v)
	if err != nil {
		panic(err) //nolint
	}
	return n
}

// IANA assigned ports for "stun" protocol.
const (
	DefaultPort    = 3478
	DefaultTLSPort = 5349
)

type transactionIDSetter struct{}

func (transactionIDSetter) AddTo(m *Message) error {
	return m.NewTransactionID()
}

// TransactionID is Setter for m.TransactionID.
var TransactionID Setter = transactionIDSetter{} //nolint:gochecknoglobals
