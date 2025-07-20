package rtpmpeg4audio

import (
	"errors"

	"github.com/pion/rtp"
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

// Decoder is a RTP/MPEG-4 Audio decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc3640
// Specification: https://datatracker.ietf.org/doc/html/rfc6416#section-7.3
type Decoder struct {
	// use RFC6416 (LATM) instead of RFC3640 (generic).
	LATM bool

	// Generic-only
	// The number of bits in which the AU-size field is encoded in the AU-header.
	SizeLength int
	// The number of bits in which the AU-Index is encoded in the first AU-header.
	IndexLength int
	// The number of bits in which the AU-Index-delta field is encoded in any non-first AU-header.
	IndexDeltaLength int

	firstAUParsed      bool
	adtsMode           bool
	fragments          [][]byte
	fragmentsSize      int
	fragmentsExpected  int
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

// Decode decodes AUs from a RTP packet.
// It returns the AUs and the PTS of the first AU.
// The PTS of subsequent AUs can be calculated by adding time.Second*mpeg4audio.SamplesPerAccessUnit/clockRate.
func (d *Decoder) Decode(pkt *rtp.Packet) ([][]byte, error) {
	if !d.LATM {
		return d.decodeGeneric(pkt)
	}
	return d.decodeLATM(pkt)
}
