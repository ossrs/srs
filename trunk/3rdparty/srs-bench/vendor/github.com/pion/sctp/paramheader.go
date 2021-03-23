package sctp

import (
	"encoding/binary"
	"encoding/hex"
	"fmt"

	"github.com/pkg/errors"
)

type paramHeader struct {
	typ paramType
	len int
	raw []byte
}

const (
	paramHeaderLength = 4
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
		return errors.New("param header too short")
	}

	paramLengthPlusHeader := binary.BigEndian.Uint16(raw[2:])
	if int(paramLengthPlusHeader) < paramHeaderLength {
		return errors.Errorf("param self reported length (%d) smaller than header length (%d)", int(paramLengthPlusHeader), paramHeaderLength)
	}
	if len(raw) < int(paramLengthPlusHeader) {
		return errors.Errorf("param length (%d) shorter than its self reported length (%d)", len(raw), int(paramLengthPlusHeader))
	}

	typ, err := parseParamType(raw[0:])
	if err != nil {
		return errors.Wrap(err, "failed to parse param type")
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
