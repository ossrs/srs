// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package stun

import (
	"errors"
	"fmt"
	"io"
)

// ErrorCodeAttribute represents ERROR-CODE attribute.
//
// RFC 5389 Section 15.6
type ErrorCodeAttribute struct {
	Code   ErrorCode
	Reason []byte
}

func (c ErrorCodeAttribute) String() string {
	return fmt.Sprintf("%d: %s", c.Code, c.Reason)
}

// constants for ERROR-CODE encoding.
const (
	errorCodeReasonStart = 4
	errorCodeClassByte   = 2
	errorCodeNumberByte  = 3
	errorCodeReasonMaxB  = 763
	errorCodeModulo      = 100
)

// AddTo adds ERROR-CODE to m.
func (c ErrorCodeAttribute) AddTo(m *Message) error {
	value := make([]byte, 0, errorCodeReasonStart+errorCodeReasonMaxB)
	if err := CheckOverflow(AttrErrorCode,
		len(c.Reason)+errorCodeReasonStart,
		errorCodeReasonMaxB+errorCodeReasonStart,
	); err != nil {
		return err
	}
	value = value[:errorCodeReasonStart+len(c.Reason)]
	number := byte(c.Code % errorCodeModulo) // error code modulo 100
	class := byte(c.Code / errorCodeModulo)  // hundred digit
	value[errorCodeClassByte] = class
	value[errorCodeNumberByte] = number
	copy(value[errorCodeReasonStart:], c.Reason)
	m.Add(AttrErrorCode, value)
	return nil
}

// GetFrom decodes ERROR-CODE from m. Reason is valid until m.Raw is valid.
func (c *ErrorCodeAttribute) GetFrom(m *Message) error {
	v, err := m.Get(AttrErrorCode)
	if err != nil {
		return err
	}
	if len(v) < errorCodeReasonStart {
		return io.ErrUnexpectedEOF
	}
	var (
		class  = uint16(v[errorCodeClassByte])
		number = uint16(v[errorCodeNumberByte])
		code   = int(class*errorCodeModulo + number)
	)
	c.Code = ErrorCode(code)
	c.Reason = v[errorCodeReasonStart:]
	return nil
}

// ErrorCode is code for ERROR-CODE attribute.
type ErrorCode int

// ErrNoDefaultReason means that default reason for provided error code
// is not defined in RFC.
var ErrNoDefaultReason = errors.New("no default reason for ErrorCode")

// AddTo adds ERROR-CODE with default reason to m. If there
// is no default reason, returns ErrNoDefaultReason.
func (c ErrorCode) AddTo(m *Message) error {
	reason := errorReasons[c]
	if reason == nil {
		return ErrNoDefaultReason
	}
	a := &ErrorCodeAttribute{
		Code:   c,
		Reason: reason,
	}
	return a.AddTo(m)
}

// Possible error codes.
const (
	CodeTryAlternate     ErrorCode = 300
	CodeBadRequest       ErrorCode = 400
	CodeUnauthorized     ErrorCode = 401
	CodeUnknownAttribute ErrorCode = 420
	CodeStaleNonce       ErrorCode = 438
	CodeRoleConflict     ErrorCode = 487
	CodeServerError      ErrorCode = 500
)

// DEPRECATED constants.
const (
	// DEPRECATED, use CodeUnauthorized.
	CodeUnauthorised = CodeUnauthorized
)

// Error codes from RFC 5766.
//
// RFC 5766 Section 15
const (
	CodeForbidden             ErrorCode = 403 // Forbidden
	CodeAllocMismatch         ErrorCode = 437 // Allocation Mismatch
	CodeWrongCredentials      ErrorCode = 441 // Wrong Credentials
	CodeUnsupportedTransProto ErrorCode = 442 // Unsupported Transport Protocol
	CodeAllocQuotaReached     ErrorCode = 486 // Allocation Quota Reached
	CodeInsufficientCapacity  ErrorCode = 508 // Insufficient Capacity
)

// Error codes from RFC 6062.
//
// RFC 6062 Section 6.3
const (
	CodeConnAlreadyExists    ErrorCode = 446
	CodeConnTimeoutOrFailure ErrorCode = 447
)

// Error codes from RFC 6156.
//
// RFC 6156 Section 10.2
const (
	CodeAddrFamilyNotSupported ErrorCode = 440 // Address Family not Supported
	CodePeerAddrFamilyMismatch ErrorCode = 443 // Peer Address Family Mismatch
)

//nolint:gochecknoglobals
var errorReasons = map[ErrorCode][]byte{
	CodeTryAlternate:     []byte("Try Alternate"),
	CodeBadRequest:       []byte("Bad Request"),
	CodeUnauthorized:     []byte("Unauthorized"),
	CodeUnknownAttribute: []byte("Unknown Attribute"),
	CodeStaleNonce:       []byte("Stale Nonce"),
	CodeServerError:      []byte("Server Error"),
	CodeRoleConflict:     []byte("Role Conflict"),

	// RFC 5766.
	CodeForbidden:             []byte("Forbidden"),
	CodeAllocMismatch:         []byte("Allocation Mismatch"),
	CodeWrongCredentials:      []byte("Wrong Credentials"),
	CodeUnsupportedTransProto: []byte("Unsupported Transport Protocol"),
	CodeAllocQuotaReached:     []byte("Allocation Quota Reached"),
	CodeInsufficientCapacity:  []byte("Insufficient Capacity"),

	// RFC 6062.
	CodeConnAlreadyExists:    []byte("Connection Already Exists"),
	CodeConnTimeoutOrFailure: []byte("Connection Timeout or Failure"),

	// RFC 6156.
	CodeAddrFamilyNotSupported: []byte("Address Family not Supported"),
	CodePeerAddrFamilyMismatch: []byte("Peer Address Family Mismatch"),
}
