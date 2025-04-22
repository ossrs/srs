package rtpsimpleaudio

import (
	"github.com/pion/rtp"
)

// Decoder is a RTP/simple audio decoder.
type Decoder struct{}

// Init initializes the decoder.
func (d *Decoder) Init() error {
	return nil
}

// Decode decodes an audio frame from a RTP packet.
func (d *Decoder) Decode(pkt *rtp.Packet) ([]byte, error) {
	return pkt.Payload, nil
}
