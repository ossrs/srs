// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

import (
	"encoding/binary"
	"errors"
)

//  This parameter is used to inform the receiver that a sender is willing to
//  accept zero as checksum if some other error detection method is used
//  instead.
//
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   Type = 0x8001 (suggested)   |          Length = 8           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           Error Detection Method Identifier (EDMID)           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

type paramZeroChecksumAcceptable struct {
	paramHeader
	// The Error Detection Method Identifier (EDMID) specifies an alternate
	// error detection method the sender of this parameter is willing to use for
	// received packets.
	edmid uint32
}

// Zero Checksum parameter error.
var (
	ErrZeroChecksumParamTooShort = errors.New("zero checksum parameter too short")
)

const (
	dtlsErrorDetectionMethod uint32 = 1
)

func (r *paramZeroChecksumAcceptable) marshal() ([]byte, error) {
	r.typ = zeroChecksumAcceptable
	r.raw = make([]byte, 4)
	binary.BigEndian.PutUint32(r.raw, r.edmid)

	return r.paramHeader.marshal()
}

func (r *paramZeroChecksumAcceptable) unmarshal(raw []byte) (param, error) {
	err := r.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	if len(r.raw) < 4 {
		return nil, ErrZeroChecksumParamTooShort
	}
	r.edmid = binary.BigEndian.Uint32(r.raw)

	return r, nil
}
