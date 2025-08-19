// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package packetio

import (
	"errors"
)

// netError implements net.Error
type netError struct {
	error
	timeout, temporary bool
}

func (e *netError) Timeout() bool {
	return e.timeout
}

func (e *netError) Temporary() bool {
	return e.temporary
}

var (
	// ErrFull is returned when the buffer has hit the configured limits.
	ErrFull = errors.New("packetio.Buffer is full, discarding write")

	// ErrTimeout is returned when a deadline has expired
	ErrTimeout = errors.New("i/o timeout")
)
