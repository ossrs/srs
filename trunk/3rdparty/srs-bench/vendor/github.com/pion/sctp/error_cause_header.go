package sctp

import (
	"encoding/binary"
)

// errorCauseHeader represents the shared header that is shared by all error causes
type errorCauseHeader struct {
	code errorCauseCode
	len  uint16
	raw  []byte
}

const (
	errorCauseHeaderLength = 4
)

func (e *errorCauseHeader) marshal() ([]byte, error) {
	e.len = uint16(len(e.raw)) + uint16(errorCauseHeaderLength)
	raw := make([]byte, e.len)
	binary.BigEndian.PutUint16(raw[0:], uint16(e.code))
	binary.BigEndian.PutUint16(raw[2:], e.len)
	copy(raw[errorCauseHeaderLength:], e.raw)

	return raw, nil
}

func (e *errorCauseHeader) unmarshal(raw []byte) error {
	e.code = errorCauseCode(binary.BigEndian.Uint16(raw[0:]))
	e.len = binary.BigEndian.Uint16(raw[2:])
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

// String makes errorCauseHeader printable
func (e errorCauseHeader) String() string {
	return e.code.String()
}
