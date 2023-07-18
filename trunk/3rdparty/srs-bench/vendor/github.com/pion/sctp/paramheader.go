package sctp

import (
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
)

type paramHeader struct {
	typ paramType
	len int
	raw []byte
}

const (
	paramHeaderLength = 4
)

// Parameter header parse errors
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
	binary.BigEndian.PutUint16(rawParam[2:], uint16(paramLengthPlusHeader))
	copy(rawParam[paramHeaderLength:], p.raw)

	return rawParam, nil
}

func (p *paramHeader) unmarshal(raw []byte) error {
	if len(raw) < paramHeaderLength {
		return ErrParamHeaderTooShort
	}

	paramLengthPlusHeader := binary.BigEndian.Uint16(raw[2:])
	if int(paramLengthPlusHeader) < paramHeaderLength {
		return fmt.Errorf("%w: param self reported length (%d) shorter than header length (%d)", ErrParamHeaderSelfReportedLengthShorter, int(paramLengthPlusHeader), paramHeaderLength)
	}
	if len(raw) < int(paramLengthPlusHeader) {
		return fmt.Errorf("%w: param length (%d) shorter than its self reported length (%d)", ErrParamHeaderSelfReportedLengthLonger, len(raw), int(paramLengthPlusHeader))
	}

	typ, err := parseParamType(raw[0:])
	if err != nil {
		return fmt.Errorf("%w: %v", ErrParamHeaderParseFailed, err) //nolint:errorlint
	}
	p.typ = typ
	p.raw = raw[paramHeaderLength:paramLengthPlusHeader]
	p.len = int(paramLengthPlusHeader)

	return nil
}

func (p *paramHeader) length() int {
	return p.len
}

// String makes paramHeader printable
func (p paramHeader) String() string {
	return fmt.Sprintf("%s (%d): %s", p.typ, p.len, hex.Dump(p.raw))
}
