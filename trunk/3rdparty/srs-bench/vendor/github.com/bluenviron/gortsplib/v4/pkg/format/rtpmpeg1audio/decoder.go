package rtpmpeg1audio

import (
	"errors"
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg1audio"
	"github.com/pion/rtp"
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

// Decoder is a RTP/MPEG-1/2 Audio decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc2250
type Decoder struct {
	firstPacketReceived bool
	fragments           [][]byte
	fragmentsSize       int
	fragmentsExpected   int
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
func (d *Decoder) Decode(pkt *rtp.Packet) ([][]byte, error) {
	if len(pkt.Payload) < 5 {
		d.resetFragments()
		return nil, fmt.Errorf("payload is too short")
	}

	mbz := uint16(pkt.Payload[0])<<8 | uint16(pkt.Payload[1])
	if mbz != 0 {
		d.resetFragments()
		return nil, fmt.Errorf("invalid MBZ: %v", mbz)
	}

	offset := uint16(pkt.Payload[2])<<8 | uint16(pkt.Payload[3])

	var frames [][]byte

	if offset == 0 {
		d.resetFragments()
		d.firstPacketReceived = true

		buf := pkt.Payload[4:]
		for {
			var h mpeg1audio.FrameHeader
			err := h.Unmarshal(buf)
			if err != nil {
				return nil, err
			}

			fl := h.FrameLen()
			bl := len(buf)
			if bl >= fl {
				frames = append(frames, buf[:fl])
				buf = buf[fl:]
				if len(buf) == 0 {
					break
				}
			} else {
				if len(frames) != 0 {
					return nil, fmt.Errorf("invalid packet")
				}

				d.fragments = append(d.fragments, buf)
				d.fragmentsSize = bl
				d.fragmentsExpected = fl - bl
				return nil, ErrMorePacketsNeeded
			}
		}
	} else {
		if int(offset) != d.fragmentsSize {
			if !d.firstPacketReceived {
				return nil, ErrNonStartingPacketAndNoPrevious
			}

			d.resetFragments()
			return nil, fmt.Errorf("unexpected offset %v, expected %v", offset, d.fragmentsSize)
		}

		bl := len(pkt.Payload[4:])
		d.fragmentsSize += bl
		d.fragmentsExpected -= bl

		if d.fragmentsExpected < 0 {
			d.resetFragments()
			return nil, fmt.Errorf("fragment is too big")
		}

		d.fragments = append(d.fragments, pkt.Payload[4:])

		if d.fragmentsExpected > 0 {
			return nil, ErrMorePacketsNeeded
		}

		frames = [][]byte{joinFragments(d.fragments, d.fragmentsSize)}
		d.resetFragments()
	}

	return frames, nil
}
