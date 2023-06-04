// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package recordlayer

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/internal/util"
	"github.com/pion/dtls/v2/pkg/protocol"
)

// Header implements a TLS RecordLayer header
type Header struct {
	ContentType    protocol.ContentType
	ContentLen     uint16
	Version        protocol.Version
	Epoch          uint16
	SequenceNumber uint64 // uint48 in spec
}

// RecordLayer enums
const (
	HeaderSize        = 13
	MaxSequenceNumber = 0x0000FFFFFFFFFFFF
)

// Marshal encodes a TLS RecordLayer Header to binary
func (h *Header) Marshal() ([]byte, error) {
	if h.SequenceNumber > MaxSequenceNumber {
		return nil, errSequenceNumberOverflow
	}

	out := make([]byte, HeaderSize)
	out[0] = byte(h.ContentType)
	out[1] = h.Version.Major
	out[2] = h.Version.Minor
	binary.BigEndian.PutUint16(out[3:], h.Epoch)
	util.PutBigEndianUint48(out[5:], h.SequenceNumber)
	binary.BigEndian.PutUint16(out[HeaderSize-2:], h.ContentLen)
	return out, nil
}

// Unmarshal populates a TLS RecordLayer Header from binary
func (h *Header) Unmarshal(data []byte) error {
	if len(data) < HeaderSize {
		return errBufferTooSmall
	}
	h.ContentType = protocol.ContentType(data[0])
	h.Version.Major = data[1]
	h.Version.Minor = data[2]
	h.Epoch = binary.BigEndian.Uint16(data[3:])

	// SequenceNumber is stored as uint48, make into uint64
	seqCopy := make([]byte, 8)
	copy(seqCopy[2:], data[5:11])
	h.SequenceNumber = binary.BigEndian.Uint64(seqCopy)

	if !h.Version.Equal(protocol.Version1_0) && !h.Version.Equal(protocol.Version1_2) {
		return errUnsupportedProtocolVersion
	}

	return nil
}
