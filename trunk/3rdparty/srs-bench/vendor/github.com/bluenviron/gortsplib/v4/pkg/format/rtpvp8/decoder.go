package rtpvp8

import (
	"errors"
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/vp8"
	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
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

// Decoder is a RTP/VP8 decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc7741
type Decoder struct {
	firstPacketReceived bool
	fragmentsSize       int
	fragments           [][]byte
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

// Decode decodes a VP8 frame from a RTP packet.
func (d *Decoder) Decode(pkt *rtp.Packet) ([]byte, error) {
	var vpkt codecs.VP8Packet
	_, err := vpkt.Unmarshal(pkt.Payload)
	if err != nil {
		d.resetFragments()
		return nil, err
	}

	if vpkt.PID != 0 {
		d.resetFragments()
		return nil, fmt.Errorf("packets containing single partitions are not supported")
	}

	var frame []byte

	if vpkt.S == 1 {
		d.resetFragments()
		d.firstPacketReceived = true

		if !pkt.Marker {
			d.fragmentsSize = len(vpkt.Payload)
			d.fragments = append(d.fragments, vpkt.Payload)
			d.fragmentNextSeqNum = pkt.SequenceNumber + 1
			return nil, ErrMorePacketsNeeded
		}

		frame = vpkt.Payload
	} else {
		if d.fragmentsSize == 0 {
			if !d.firstPacketReceived {
				return nil, ErrNonStartingPacketAndNoPrevious
			}

			return nil, fmt.Errorf("received a non-starting fragment")
		}

		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		d.fragmentsSize += len(vpkt.Payload)

		if d.fragmentsSize > vp8.MaxFrameSize {
			errSize := d.fragmentsSize
			d.resetFragments()
			return nil, fmt.Errorf("frame size (%d) is too big, maximum is %d",
				errSize, vp8.MaxFrameSize)
		}

		d.fragments = append(d.fragments, vpkt.Payload)
		d.fragmentNextSeqNum++

		if !pkt.Marker {
			return nil, ErrMorePacketsNeeded
		}

		frame = joinFragments(d.fragments, d.fragmentsSize)
		d.resetFragments()
	}

	return frame, nil
}
