// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

import (
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
)

type paramHeaderUnrecognizedAction byte

type paramHeader struct {
	typ                paramType
	unrecognizedAction paramHeaderUnrecognizedAction
	len                int
	raw                []byte
}

/*
	 The Parameter Types are encoded such that the highest-order 2 bits specify
	 the action that is taken if the processing endpoint does not recognize the
	 Parameter Type.

	 00 - Stop processing this parameter and do not process any further parameters within this chunk.

	 01 - Stop processing this parameter, do not process any further parameters within this chunk, and
		  report the unrecognized parameter, as described in Section 3.2.2.

	 10 - Skip this parameter and continue processing.

	 11 - Skip this parameter and continue processing, but report the unrecognized
		  parameter, as described in Section 3.2.2.

	 https://www.rfc-editor.org/rfc/rfc9260.html#section-3.2.1
*/

const (
	paramHeaderUnrecognizedActionMask                                        = 0b11000000
	paramHeaderUnrecognizedActionStop          paramHeaderUnrecognizedAction = 0b00000000
	paramHeaderUnrecognizedActionStopAndReport paramHeaderUnrecognizedAction = 0b01000000
	paramHeaderUnrecognizedActionSkip          paramHeaderUnrecognizedAction = 0b10000000
	paramHeaderUnrecognizedActionSkipAndReport paramHeaderUnrecognizedAction = 0b11000000

	paramHeaderLength = 4
)

// Parameter header parse errors.
var (
	ErrParamHeaderTooShort                  = errors.New("param header too short")
	ErrParamHeaderSelfReportedLengthShorter = errors.New("param self reported length is shorter than header length")
	ErrParamHeaderSelfReportedLengthLonger  = errors.New("param self reported length is longer than header length")
	ErrParamHeaderParseFailed               = errors.New("failed to parse param type")
)

func (p *paramHeader) marshal() ([]byte, error) {
	paramLengthPlusHeader := paramHeaderLength + len(p.raw)

	rawParam := make([]byte, paramLengthPlusHeader)
	binary.BigEndian.PutUint16(rawParam[0:], uint16(p.typ))
	binary.BigEndian.PutUint16(rawParam[2:], uint16(paramLengthPlusHeader)) //nolint:gosec // G115
	copy(rawParam[paramHeaderLength:], p.raw)

	return rawParam, nil
}

func (p *paramHeader) unmarshal(raw []byte) error {
	if len(raw) < paramHeaderLength {
		return ErrParamHeaderTooShort
	}

	paramLengthPlusHeader := binary.BigEndian.Uint16(raw[2:])
	if int(paramLengthPlusHeader) < paramHeaderLength {
		return fmt.Errorf(
			"%w: param self reported length (%d) shorter than header length (%d)",
			ErrParamHeaderSelfReportedLengthShorter, int(paramLengthPlusHeader), paramHeaderLength,
		)
	}
	if len(raw) < int(paramLengthPlusHeader) {
		return fmt.Errorf(
			"%w: param length (%d) shorter than its self reported length (%d)",
			ErrParamHeaderSelfReportedLengthLonger, len(raw), int(paramLengthPlusHeader),
		)
	}

	typ, err := parseParamType(raw[0:])
	if err != nil {
		return fmt.Errorf("%w: %v", ErrParamHeaderParseFailed, err) //nolint:errorlint
	}
	p.typ = typ
	p.unrecognizedAction = paramHeaderUnrecognizedAction(raw[0] & paramHeaderUnrecognizedActionMask)
	p.raw = raw[paramHeaderLength:paramLengthPlusHeader]
	p.len = int(paramLengthPlusHeader)

	return nil
}

func (p *paramHeader) length() int {
	return p.len
}

// String makes paramHeader printable.
func (p paramHeader) String() string {
	return fmt.Sprintf("%s (%d): %s", p.typ, p.len, hex.Dump(p.raw))
}
