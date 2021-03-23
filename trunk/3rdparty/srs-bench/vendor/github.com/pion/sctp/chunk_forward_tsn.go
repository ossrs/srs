package sctp

import (
	"encoding/binary"
	"fmt"

	"github.com/pkg/errors"
)

// This chunk shall be used by the data sender to inform the data
// receiver to adjust its cumulative received TSN point forward because
// some missing TSNs are associated with data chunks that SHOULD NOT be
// transmitted or retransmitted by the sender.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   Type = 192  |  Flags = 0x00 |        Length = Variable      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      New Cumulative TSN                       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         Stream-1              |       Stream Sequence-1       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// \                                                               /
// /                                                               \
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         Stream-N              |       Stream Sequence-N       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

type chunkForwardTSN struct {
	chunkHeader

	// This indicates the new cumulative TSN to the data receiver.  Upon
	// the reception of this value, the data receiver MUST consider
	// any missing TSNs earlier than or equal to this value as received,
	// and stop reporting them as gaps in any subsequent SACKs.
	newCumulativeTSN uint32

	streams []chunkForwardTSNStream
}

const (
	newCumulativeTSNLength = 4
	forwardTSNStreamLength = 4
)

var errMarshalStreamFailed = errors.New("failed to marshal stream")

func (c *chunkForwardTSN) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if len(c.raw) < newCumulativeTSNLength {
		return errors.New("chunk to short")
	}

	c.newCumulativeTSN = binary.BigEndian.Uint32(c.raw[0:])

	offset := newCumulativeTSNLength
	remaining := len(c.raw) - offset
	for remaining > 0 {
		s := chunkForwardTSNStream{}

		if err := s.unmarshal(c.raw[offset:]); err != nil {
			return fmt.Errorf("failed to unmarshal stream: %w", err)
		}

		c.streams = append(c.streams, s)

		offset += s.length()
		remaining -= s.length()
	}

	return nil
}

func (c *chunkForwardTSN) marshal() ([]byte, error) {
	out := make([]byte, newCumulativeTSNLength)
	binary.BigEndian.PutUint32(out[0:], c.newCumulativeTSN)

	for _, s := range c.streams {
		b, err := s.marshal()
		if err != nil {
			return nil, fmt.Errorf("%w: %v", errMarshalStreamFailed, err)
		}
		out = append(out, b...)
	}

	c.typ = ctForwardTSN
	c.raw = out
	return c.chunkHeader.marshal()
}

func (c *chunkForwardTSN) check() (abort bool, err error) {
	return true, nil
}

// String makes chunkForwardTSN printable
func (c *chunkForwardTSN) String() string {
	res := fmt.Sprintf("New Cumulative TSN: %d\n", c.newCumulativeTSN)
	for _, s := range c.streams {
		res += fmt.Sprintf(" - si=%d, ssn=%d\n", s.identifier, s.sequence)
	}
	return res
}

type chunkForwardTSNStream struct {
	// This field holds a stream number that was skipped by this
	// FWD-TSN.
	identifier uint16

	// This field holds the sequence number associated with the stream
	// that was skipped.  The stream sequence field holds the largest
	// stream sequence number in this stream being skipped.  The receiver
	// of the FWD-TSN's can use the Stream-N and Stream Sequence-N fields
	// to enable delivery of any stranded TSN's that remain on the stream
	// re-ordering queues.  This field MUST NOT report TSN's corresponding
	// to DATA chunks that are marked as unordered.  For ordered DATA
	// chunks this field MUST be filled in.
	sequence uint16
}

func (s *chunkForwardTSNStream) length() int {
	return forwardTSNStreamLength
}

func (s *chunkForwardTSNStream) unmarshal(raw []byte) error {
	if len(raw) < forwardTSNStreamLength {
		return errors.New("stream to short")
	}
	s.identifier = binary.BigEndian.Uint16(raw[0:])
	s.sequence = binary.BigEndian.Uint16(raw[2:])

	return nil
}

func (s *chunkForwardTSNStream) marshal() ([]byte, error) { // nolint:unparam
	out := make([]byte, forwardTSNStreamLength)

	binary.BigEndian.PutUint16(out[0:], s.identifier)
	binary.BigEndian.PutUint16(out[2:], s.sequence)

	return out, nil
}
