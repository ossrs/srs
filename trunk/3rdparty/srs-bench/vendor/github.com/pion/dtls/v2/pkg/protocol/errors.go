// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package protocol

import (
	"errors"
	"fmt"
	"net"
)

var (
	errBufferTooSmall    = &TemporaryError{Err: errors.New("buffer is too small")} //nolint:goerr113
	errInvalidCipherSpec = &FatalError{Err: errors.New("cipher spec invalid")}     //nolint:goerr113
)

// FatalError indicates that the DTLS connection is no longer available.
// It is mainly caused by wrong configuration of server or client.
type FatalError struct {
	Err error
}

// InternalError indicates and internal error caused by the implementation, and the DTLS connection is no longer available.
// It is mainly caused by bugs or tried to use unimplemented features.
type InternalError struct {
	Err error
}

// TemporaryError indicates that the DTLS connection is still available, but the request was failed temporary.
type TemporaryError struct {
	Err error
}

// TimeoutError indicates that the request was timed out.
type TimeoutError struct {
	Err error
}

// HandshakeError indicates that the handshake failed.
type HandshakeError struct {
	Err error
}

// Timeout implements net.Error.Timeout()
func (*FatalError) Timeout() bool { return false }

// Temporary implements net.Error.Temporary()
func (*FatalError) Temporary() bool { return false }

// Unwrap implements Go1.13 error unwrapper.
func (e *FatalError) Unwrap() error { return e.Err }

func (e *FatalError) Error() string { return fmt.Sprintf("dtls fatal: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (*InternalError) Timeout() bool { return false }

// Temporary implements net.Error.Temporary()
func (*InternalError) Temporary() bool { return false }

// Unwrap implements Go1.13 error unwrapper.
func (e *InternalError) Unwrap() error { return e.Err }

func (e *InternalError) Error() string { return fmt.Sprintf("dtls internal: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (*TemporaryError) Timeout() bool { return false }

// Temporary implements net.Error.Temporary()
func (*TemporaryError) Temporary() bool { return true }

// Unwrap implements Go1.13 error unwrapper.
func (e *TemporaryError) Unwrap() error { return e.Err }

func (e *TemporaryError) Error() string { return fmt.Sprintf("dtls temporary: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (*TimeoutError) Timeout() bool { return true }

// Temporary implements net.Error.Temporary()
func (*TimeoutError) Temporary() bool { return true }

// Unwrap implements Go1.13 error unwrapper.
func (e *TimeoutError) Unwrap() error { return e.Err }

func (e *TimeoutError) Error() string { return fmt.Sprintf("dtls timeout: %v", e.Err) }

// Timeout implements net.Error.Timeout()
func (e *HandshakeError) Timeout() bool {
	var netErr net.Error
	if errors.As(e.Err, &netErr) {
		return netErr.Timeout()
	}
	return false
}

// Temporary implements net.Error.Temporary()
func (e *HandshakeError) Temporary() bool {
	var netErr net.Error
	if errors.As(e.Err, &netErr) {
		return netErr.Temporary() //nolint
	}
	return false
}

// Unwrap implements Go1.13 error unwrapper.
func (e *HandshakeError) Unwrap() error { return e.Err }

func (e *HandshakeError) Error() string { return fmt.Sprintf("handshake error: %v", e.Err) }
