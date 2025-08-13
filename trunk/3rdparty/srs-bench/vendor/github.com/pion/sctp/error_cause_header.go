// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

import (
	"encoding/binary"
	"errors"
)

// errorCauseHeader represents the shared header that is shared by all error causes.
type errorCauseHeader struct {
	code errorCauseCode
	len  uint16
	raw  []byte
}

const (
	errorCauseHeaderLength = 4
)

// ErrInvalidSCTPChunk is returned when an SCTP chunk is invalid.
var ErrInvalidSCTPChunk = errors.New("invalid SCTP chunk")

func (e *errorCauseHeader) marshal() ([]byte, error) {
	e.len = uint16(len(e.raw)) + uint16(errorCauseHeaderLength) //nolint:gosec // G115
	raw := make([]byte, e.len)
	binary.BigEndian.PutUint16(raw[0:], uint16(e.code))
	binary.BigEndian.PutUint16(raw[2:], e.len)
	copy(raw[errorCauseHeaderLength:], e.raw)

	return raw, nil
}

func (e *errorCauseHeader) unmarshal(raw []byte) error {
	e.code = errorCauseCode(binary.BigEndian.Uint16(raw[0:]))
	e.len = binary.BigEndian.Uint16(raw[2:])
	if e.len < errorCauseHeaderLength || int(e.len) > len(raw) {
		return ErrInvalidSCTPChunk
	}
	valueLength := e.len - errorCauseHeaderLength
	e.raw = raw[errorCauseHeaderLength : errorCauseHeaderLength+valueLength]

	return nil
}

func (e *errorCauseHeader) length() uint16 {
	return e.len
}

func (e *errorCauseHeader) errorCauseCode() errorCauseCode {
	return e.code
}

// String makes errorCauseHeader printable.
func (e errorCauseHeader) String() string {
	return e.code.String()
}
