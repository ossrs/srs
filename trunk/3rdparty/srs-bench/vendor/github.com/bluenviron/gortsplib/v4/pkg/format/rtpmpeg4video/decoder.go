package rtpmpeg4video

import (
	"errors"
	"fmt"

	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4video"
)

// ErrMorePacketsNeeded is returned when more packets are needed.
var ErrMorePacketsNeeded = errors.New("need more packets")

func joinFragments(fragments [][]byte, size int) []byte {
	ret := make([]byte, size)
	n := 0
	for _, p := range fragments {
		n += copy(ret[n:], p)
	}
	return ret
}

// Decoder is a RTP/MPEG-4 Video decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc6416
type Decoder struct {
	fragments          [][]byte
	fragmentsSize      int
	fragmentNextSeqNum uint16
}

// Init initializes the decoder.
func (d *Decoder) Init() error {
	return nil
}

func (d *Decoder) resetFragments() {
	d.fragments = d.fragments[:0]
	d.fragmentsSize = 0
}

// Decode decodes a frame from a RTP packet.
func (d *Decoder) Decode(pkt *rtp.Packet) ([]byte, error) {
	var frame []byte

	if d.fragmentsSize == 0 {
		if pkt.Marker {
			frame = pkt.Payload
		} else {
			d.fragmentsSize = len(pkt.Payload)
			d.fragments = append(d.fragments, pkt.Payload)
			d.fragmentNextSeqNum = pkt.SequenceNumber + 1
			return nil, ErrMorePacketsNeeded
		}
	} else {
		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		d.fragmentsSize += len(pkt.Payload)

		if d.fragmentsSize > mpeg4video.MaxFrameSize {
			errSize := d.fragmentsSize
			d.resetFragments()
			return nil, fmt.Errorf("frame size (%d) is too big, maximum is %d",
				errSize, mpeg4video.MaxFrameSize)
		}

		d.fragments = append(d.fragments, pkt.Payload)
		d.fragmentNextSeqNum++

		if !pkt.Marker {
			return nil, ErrMorePacketsNeeded
		}

		frame = joinFragments(d.fragments, d.fragmentsSize)
		d.resetFragments()
	}

	return frame, nil
}
