package rtpac3

import (
	"errors"
	"fmt"

	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/ac3"
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

// Decoder is a AC-3 decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc4184
type Decoder struct {
	firstPacketReceived bool
	fragments           [][]byte
	fragmentsSize       int
	fragmentsExpected   int
	fragmentNextSeqNum  uint16
}

// Init initializes the decoder.
func (d *Decoder) Init() error {
	return nil
}

func (d *Decoder) resetFragments() {
	d.fragments = d.fragments[:0]
	d.fragmentsSize = 0
}

// Decode decodes frames from a RTP packet.
// It returns the frames and the PTS of the first frame.
func (d *Decoder) Decode(pkt *rtp.Packet) ([][]byte, error) {
	if len(pkt.Payload) < 2 {
		d.resetFragments()
		return nil, fmt.Errorf("payload is too short")
	}

	mbz := pkt.Payload[0] >> 2
	ft := pkt.Payload[0] & 0b11

	if mbz != 0 {
		d.resetFragments()
		return nil, fmt.Errorf("invalid MBZ: %v", mbz)
	}

	var frames [][]byte

	switch ft {
	case 0:
		d.resetFragments()
		d.firstPacketReceived = true

		buf := pkt.Payload[2:]

		for {
			var syncInfo ac3.SyncInfo
			err := syncInfo.Unmarshal(buf)
			if err != nil {
				return nil, err
			}
			size := syncInfo.FrameSize()

			if len(buf) < size {
				return nil, fmt.Errorf("payload is too short")
			}

			frames = append(frames, buf[:size])
			buf = buf[size:]

			if len(buf) == 0 {
				break
			}
		}

	case 1, 2:
		d.resetFragments()

		var syncInfo ac3.SyncInfo
		err := syncInfo.Unmarshal(pkt.Payload[2:])
		if err != nil {
			return nil, err
		}
		size := syncInfo.FrameSize()

		le := len(pkt.Payload[2:])
		d.fragmentsSize = le
		d.fragmentsExpected = size - le
		d.fragments = append(d.fragments, pkt.Payload[2:])
		d.fragmentNextSeqNum = pkt.SequenceNumber + 1
		d.firstPacketReceived = true

		return nil, ErrMorePacketsNeeded

	case 3:
		if d.fragmentsSize == 0 {
			if !d.firstPacketReceived {
				return nil, ErrNonStartingPacketAndNoPrevious
			}
			return nil, fmt.Errorf("received a subsequent fragment without previous fragments")
		}

		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		le := len(pkt.Payload[2:])
		d.fragmentsSize += le
		d.fragmentsExpected -= le

		if d.fragmentsExpected < 0 {
			d.resetFragments()
			return nil, fmt.Errorf("fragment is too big")
		}

		d.fragments = append(d.fragments, pkt.Payload[2:])
		d.fragmentNextSeqNum++

		if d.fragmentsExpected > 0 {
			return nil, ErrMorePacketsNeeded
		}

		frames = [][]byte{joinFragments(d.fragments, d.fragmentsSize)}
		d.resetFragments()
	}

	return frames, nil
}
