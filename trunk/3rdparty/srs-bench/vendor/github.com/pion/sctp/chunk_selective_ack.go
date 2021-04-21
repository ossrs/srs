package sctp

import (
	"encoding/binary"
	"fmt"

	"github.com/pkg/errors"
)

/*
chunkSelectiveAck represents an SCTP Chunk of type SACK

This chunk is sent to the peer endpoint to acknowledge received DATA
chunks and to inform the peer endpoint of gaps in the received
subsequences of DATA chunks as represented by their TSNs.
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 3    |Chunk  Flags   |      Chunk Length             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Cumulative TSN Ack                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Advertised Receiver Window Credit (a_rwnd)           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Number of Gap Ack Blocks = N  |  Number of Duplicate TSNs = X |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Gap Ack Block #1 Start       |   Gap Ack Block #1 End        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/                                                               /
\                              ...                              \
/                                                               /
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Gap Ack Block #N Start      |  Gap Ack Block #N End         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Duplicate TSN 1                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/                                                               /
\                              ...                              \
/                                                               /
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Duplicate TSN X                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

type gapAckBlock struct {
	start uint16
	end   uint16
}

// String makes gapAckBlock printable
func (g gapAckBlock) String() string {
	return fmt.Sprintf("%d - %d", g.start, g.end)
}

type chunkSelectiveAck struct {
	chunkHeader
	cumulativeTSNAck               uint32
	advertisedReceiverWindowCredit uint32
	gapAckBlocks                   []gapAckBlock
	duplicateTSN                   []uint32
}

const (
	selectiveAckHeaderSize = 12
)

func (s *chunkSelectiveAck) unmarshal(raw []byte) error {
	if err := s.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if s.typ != ctSack {
		return errors.Errorf("ChunkType is not of type SACK, actually is %s", s.typ.String())
	}

	if len(s.raw) < selectiveAckHeaderSize {
		return errors.Errorf("SACK Chunk size is not large enough to contain header (%v remaining, needs %v bytes)",
			len(s.raw), selectiveAckHeaderSize)
	}

	s.cumulativeTSNAck = binary.BigEndian.Uint32(s.raw[0:])
	s.advertisedReceiverWindowCredit = binary.BigEndian.Uint32(s.raw[4:])
	s.gapAckBlocks = make([]gapAckBlock, binary.BigEndian.Uint16(s.raw[8:]))
	s.duplicateTSN = make([]uint32, binary.BigEndian.Uint16(s.raw[10:]))

	if len(s.raw) != selectiveAckHeaderSize+(4*len(s.gapAckBlocks)+(4*len(s.duplicateTSN))) {
		return errors.Errorf("SACK Chunk size does not match predicted amount from header values")
	}

	offset := selectiveAckHeaderSize
	for i := range s.gapAckBlocks {
		s.gapAckBlocks[i].start = binary.BigEndian.Uint16(s.raw[offset:])
		s.gapAckBlocks[i].end = binary.BigEndian.Uint16(s.raw[offset+2:])
		offset += 4
	}
	for i := range s.duplicateTSN {
		s.duplicateTSN[i] = binary.BigEndian.Uint32(s.raw[offset:])
		offset += 4
	}

	return nil
}

func (s *chunkSelectiveAck) marshal() ([]byte, error) {
	sackRaw := make([]byte, selectiveAckHeaderSize+(4*len(s.gapAckBlocks)+(4*len(s.duplicateTSN))))
	binary.BigEndian.PutUint32(sackRaw[0:], s.cumulativeTSNAck)
	binary.BigEndian.PutUint32(sackRaw[4:], s.advertisedReceiverWindowCredit)
	binary.BigEndian.PutUint16(sackRaw[8:], uint16(len(s.gapAckBlocks)))
	binary.BigEndian.PutUint16(sackRaw[10:], uint16(len(s.duplicateTSN)))
	offset := selectiveAckHeaderSize
	for _, g := range s.gapAckBlocks {
		binary.BigEndian.PutUint16(sackRaw[offset:], g.start)
		binary.BigEndian.PutUint16(sackRaw[offset+2:], g.end)
		offset += 4
	}
	for _, t := range s.duplicateTSN {
		binary.BigEndian.PutUint32(sackRaw[offset:], t)
		offset += 4
	}

	s.chunkHeader.typ = ctSack
	s.chunkHeader.raw = sackRaw
	return s.chunkHeader.marshal()
}

func (s *chunkSelectiveAck) check() (abort bool, err error) {
	return false, nil
}

// String makes chunkSelectiveAck printable
func (s *chunkSelectiveAck) String() string {
	res := fmt.Sprintf("SACK cumTsnAck=%d arwnd=%d dupTsn=%d",
		s.cumulativeTSNAck,
		s.advertisedReceiverWindowCredit,
		s.duplicateTSN)

	for _, gap := range s.gapAckBlocks {
		res = fmt.Sprintf("%s\n gap ack: %s", res, gap)
	}

	return res
}
