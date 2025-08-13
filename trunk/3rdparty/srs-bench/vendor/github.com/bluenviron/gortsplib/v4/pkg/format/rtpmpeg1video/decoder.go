package rtpmpeg1video

import (
	"errors"
	"fmt"

	"github.com/pion/rtp"
)

const (
	maxFrameSize = 1 * 1024 * 1024
)

// ErrMorePacketsNeeded is returned when more packets are needed.
var ErrMorePacketsNeeded = errors.New("need more packets")

// ErrNonStartingPacketAndNoPrevious is returned when we received a non-starting
// packet of a fragmented frame and we didn't received anything before.
// It's normal to receive this when decoding a stream that has been already
// running for some time.
var ErrNonStartingPacketAndNoPrevious = errors.New(
	"received a non-starting fragment without any previous starting fragment")

func joinFragments(fragments [][]byte, size int) []byte {
	ret := make([]byte, size)
	n := 0
	for _, p := range fragments {
		n += copy(ret[n:], p)
	}
	return ret
}

// Decoder is a RTP/MPEG-1/2 Video decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc2250
type Decoder struct {
	fragments          [][]byte
	fragmentsSize      int
	fragmentNextSeqNum uint16

	sliceBuffer     [][]byte
	sliceBufferSize int
}

// Init initializes the decoder.
func (d *Decoder) Init() error {
	return nil
}

func (d *Decoder) resetFragments() {
	d.fragments = d.fragments[:0]
	d.fragmentsSize = 0
}

func (d *Decoder) decodeSlice(pkt *rtp.Packet) ([]byte, error) {
	if len(pkt.Payload) < 4 {
		d.resetFragments()
		return nil, fmt.Errorf("payload is too short")
	}

	mbz := pkt.Payload[0] >> 3
	if mbz != 0 {
		d.resetFragments()
		return nil, fmt.Errorf("invalid MBZ: %v", mbz)
	}

	t := (pkt.Payload[0] >> 2) & 0x01
	if t != 0 {
		d.resetFragments()
		return nil, fmt.Errorf("MPEG-2 video-specific header extension is not supported yet")
	}

	an := pkt.Payload[2] >> 7
	if an != 0 {
		d.resetFragments()
		return nil, fmt.Errorf("AN not supported yet")
	}

	n := (pkt.Payload[2] >> 6) & 0x01
	if n != 0 {
		d.resetFragments()
		return nil, fmt.Errorf("N not supported yet")
	}

	b := (pkt.Payload[2] >> 4) & 0x01
	e := (pkt.Payload[2] >> 3) & 0x01

	switch {
	case b == 1 && e == 1:
		return pkt.Payload[4:], nil

	case b == 1:
		d.fragments = d.fragments[:0]
		d.fragments = append(d.fragments, pkt.Payload[4:])
		d.fragmentsSize = len(pkt.Payload[4:])
		d.fragmentNextSeqNum = pkt.SequenceNumber + 1
		return nil, ErrMorePacketsNeeded

	case e == 1:
		if d.fragmentsSize == 0 {
			return nil, ErrNonStartingPacketAndNoPrevious
		}

		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		d.fragments = append(d.fragments, pkt.Payload[4:])
		d.fragmentsSize += len(pkt.Payload[4:])

		slice := joinFragments(d.fragments, d.fragmentsSize)
		d.resetFragments()
		return slice, nil

	default:
		if d.fragmentsSize == 0 {
			return nil, ErrNonStartingPacketAndNoPrevious
		}

		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		d.fragments = append(d.fragments, pkt.Payload[4:])
		d.fragmentsSize += len(pkt.Payload[4:])
		d.fragmentNextSeqNum++
		return nil, ErrMorePacketsNeeded
	}
}

// Decode decodes frames from a RTP packet.
func (d *Decoder) Decode(pkt *rtp.Packet) ([]byte, error) {
	slice, err := d.decodeSlice(pkt)
	if err != nil {
		return nil, err
	}

	addSize := len(slice)

	if (d.sliceBufferSize + addSize) > maxFrameSize {
		errSize := d.sliceBufferSize + addSize
		d.sliceBuffer = nil
		d.sliceBufferSize = 0
		return nil, fmt.Errorf("frame size (%d) is too big, maximum is %d",
			errSize, maxFrameSize)
	}

	d.sliceBuffer = append(d.sliceBuffer, slice)
	d.sliceBufferSize += addSize

	if !pkt.Marker {
		return nil, ErrMorePacketsNeeded
	}

	ret := joinFragments(d.sliceBuffer, d.sliceBufferSize)

	// do not reuse sliceBuffer to avoid race conditions
	d.sliceBuffer = nil
	d.sliceBufferSize = 0

	return ret, nil
}
