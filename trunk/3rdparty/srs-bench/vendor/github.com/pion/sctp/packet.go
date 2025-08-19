// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

import (
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"
)

// Create the crc32 table we'll use for the checksum.
var castagnoliTable = crc32.MakeTable(crc32.Castagnoli) // nolint:gochecknoglobals

// Allocate and zero this data once.
// We need to use it for the checksum and don't want to allocate/clear each time.
var fourZeroes [4]byte // nolint:gochecknoglobals

/*
Packet represents an SCTP packet, defined in https://tools.ietf.org/html/rfc4960#section-3
An SCTP packet is composed of a common header and chunks.  A chunk
contains either control information or user data.

						SCTP Packet Format
	 0                   1                   2                   3
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                        Common Header                          |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                          Chunk #1                             |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                           ...                                 |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                          Chunk #n                             |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

					SCTP Common Header Format
	 0                   1                   2                   3
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|     Source Value Number      |     Destination Value Number   |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                      Verification Tag                         |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                           Checksum                            |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type packet struct {
	sourcePort      uint16
	destinationPort uint16
	verificationTag uint32
	chunks          []chunk
}

const (
	packetHeaderSize = 12
)

// SCTP packet errors.
var (
	ErrPacketRawTooSmall           = errors.New("raw is smaller than the minimum length for a SCTP packet")
	ErrParseSCTPChunkNotEnoughData = errors.New("unable to parse SCTP chunk, not enough data for complete header")
	ErrUnmarshalUnknownChunkType   = errors.New("failed to unmarshal, contains unknown chunk type")
	ErrChecksumMismatch            = errors.New("checksum mismatch theirs")
)

func (p *packet) unmarshal(doChecksum bool, raw []byte) error { //nolint:cyclop
	if len(raw) < packetHeaderSize {
		return fmt.Errorf("%w: raw only %d bytes, %d is the minimum length", ErrPacketRawTooSmall, len(raw), packetHeaderSize)
	}

	offset := packetHeaderSize

	// Check if doing CRC32c is required.
	// Without having SCTP AUTH implemented, this depends only on the type
	// og the first chunk.
	if offset+chunkHeaderSize <= len(raw) {
		switch chunkType(raw[offset]) {
		case ctInit, ctCookieEcho:
			doChecksum = true
		default:
		}
	}
	theirChecksum := binary.LittleEndian.Uint32(raw[8:])
	if theirChecksum != 0 || doChecksum {
		ourChecksum := generatePacketChecksum(raw)
		if theirChecksum != ourChecksum {
			return fmt.Errorf("%w: %d ours: %d", ErrChecksumMismatch, theirChecksum, ourChecksum)
		}
	}

	p.sourcePort = binary.BigEndian.Uint16(raw[0:])
	p.destinationPort = binary.BigEndian.Uint16(raw[2:])
	p.verificationTag = binary.BigEndian.Uint32(raw[4:])

	for {
		// Exact match, no more chunks
		if offset == len(raw) {
			break
		} else if offset+chunkHeaderSize > len(raw) {
			return fmt.Errorf("%w: offset %d remaining %d", ErrParseSCTPChunkNotEnoughData, offset, len(raw))
		}

		var dataChunk chunk
		switch chunkType(raw[offset]) {
		case ctInit:
			dataChunk = &chunkInit{}
		case ctInitAck:
			dataChunk = &chunkInitAck{}
		case ctAbort:
			dataChunk = &chunkAbort{}
		case ctCookieEcho:
			dataChunk = &chunkCookieEcho{}
		case ctCookieAck:
			dataChunk = &chunkCookieAck{}
		case ctHeartbeat:
			dataChunk = &chunkHeartbeat{}
		case ctPayloadData:
			dataChunk = &chunkPayloadData{}
		case ctSack:
			dataChunk = &chunkSelectiveAck{}
		case ctReconfig:
			dataChunk = &chunkReconfig{}
		case ctForwardTSN:
			dataChunk = &chunkForwardTSN{}
		case ctError:
			dataChunk = &chunkError{}
		case ctShutdown:
			dataChunk = &chunkShutdown{}
		case ctShutdownAck:
			dataChunk = &chunkShutdownAck{}
		case ctShutdownComplete:
			dataChunk = &chunkShutdownComplete{}
		default:
			return fmt.Errorf("%w: %s", ErrUnmarshalUnknownChunkType, chunkType(raw[offset]).String())
		}

		if err := dataChunk.unmarshal(raw[offset:]); err != nil {
			return err
		}

		p.chunks = append(p.chunks, dataChunk)
		chunkValuePadding := getPadding(dataChunk.valueLength())
		offset += chunkHeaderSize + dataChunk.valueLength() + chunkValuePadding
	}

	return nil
}

func (p *packet) marshal(doChecksum bool) ([]byte, error) {
	raw := make([]byte, packetHeaderSize)

	// Populate static headers
	// 8-12 is Checksum which will be populated when packet is complete
	binary.BigEndian.PutUint16(raw[0:], p.sourcePort)
	binary.BigEndian.PutUint16(raw[2:], p.destinationPort)
	binary.BigEndian.PutUint32(raw[4:], p.verificationTag)

	// Populate chunks
	for _, c := range p.chunks {
		chunkRaw, err := c.marshal()
		if err != nil {
			return nil, err
		}
		raw = append(raw, chunkRaw...) //nolint:makezero // todo:fix

		paddingNeeded := getPadding(len(raw))
		if paddingNeeded != 0 {
			raw = append(raw, make([]byte, paddingNeeded)...) //nolint:makezero // todo:fix
		}
	}

	if doChecksum {
		// golang CRC32C uses reflected input and reflected output, the
		// net result of this is to have the bytes flipped compared to
		// the non reflected variant that the spec expects.
		//
		// Use LittleEndian.PutUint32 to avoid flipping the bytes in to
		// the spec compliant checksum order
		binary.LittleEndian.PutUint32(raw[8:], generatePacketChecksum(raw))
	}

	return raw, nil
}

func generatePacketChecksum(raw []byte) (sum uint32) {
	// Fastest way to do a crc32 without allocating.
	sum = crc32.Update(sum, castagnoliTable, raw[0:8])
	sum = crc32.Update(sum, castagnoliTable, fourZeroes[:])
	sum = crc32.Update(sum, castagnoliTable, raw[12:])

	return sum
}

// String makes packet printable.
func (p *packet) String() string {
	format := `Packet:
	sourcePort: %d
	destinationPort: %d
	verificationTag: %d
	`
	res := fmt.Sprintf(format,
		p.sourcePort,
		p.destinationPort,
		p.verificationTag,
	)
	for i, chunk := range p.chunks {
		res += fmt.Sprintf("Chunk %d:\n %s", i, chunk)
	}

	return res
}

// TryMarshalUnmarshal attempts to marshal and unmarshal a message. Added for fuzzing.
func TryMarshalUnmarshal(msg []byte) int {
	p := &packet{}
	err := p.unmarshal(false, msg)
	if err != nil {
		return 0
	}

	_, err = p.marshal(false)
	if err != nil {
		return 0
	}

	return 1
}
