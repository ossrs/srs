package rtplpcm

import (
	"fmt"

	"github.com/pion/rtp"
)

// Decoder is a RTP/LPCM decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc3190
type Decoder struct {
	BitDepth     int
	ChannelCount int

	sampleSize int
}

// Init initializes the decoder.
func (d *Decoder) Init() error {
	d.sampleSize = d.BitDepth * d.ChannelCount / 8
	return nil
}

// Decode decodes audio samples from a RTP packet.
// It returns audio samples and PTS of the first sample.
func (d *Decoder) Decode(pkt *rtp.Packet) ([]byte, error) {
	plen := len(pkt.Payload)
	if (plen % d.sampleSize) != 0 {
		return nil, fmt.Errorf("received payload of wrong size")
	}

	return pkt.Payload, nil
}
