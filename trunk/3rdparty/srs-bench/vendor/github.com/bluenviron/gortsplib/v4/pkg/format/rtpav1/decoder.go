package rtpav1

import (
	"errors"
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/av1"
	"github.com/pion/rtp"
)

// ErrMorePacketsNeeded is returned when more packets are needed.
var ErrMorePacketsNeeded = errors.New("need more packets")

// ErrNonStartingPacketAndNoPrevious is returned when we received a non-starting
// packet of a fragmented NALU and we didn't received anything before.
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

func tuSize(tu [][]byte) int {
	s := 0
	for _, obu := range tu {
		s += len(obu)
	}
	return s
}

// Decoder is a RTP/AV1 decoder.
// Specification: https://aomediacodec.github.io/av1-rtp-spec/
type Decoder struct {
	firstPacketReceived bool
	fragments           [][]byte
	fragmentsSize       int
	fragmentNextSeqNum  uint16

	// for Decode()
	frameBuffer     [][]byte
	frameBufferLen  int
	frameBufferSize int
}

// Init initializes the decoder.
func (d *Decoder) Init() error {
	return nil
}

func (d *Decoder) resetFragments() {
	d.fragments = d.fragments[:0]
	d.fragmentsSize = 0
}

func (d *Decoder) decodeOBUs(pkt *rtp.Packet) ([][]byte, error) {
	if len(pkt.Payload) < 2 {
		return nil, fmt.Errorf("invalid payload size")
	}

	z := (pkt.Payload[0] & 0b10000000) != 0
	y := (pkt.Payload[0] & 0b01000000) != 0
	w := (pkt.Payload[0] >> 4) & 0b11
	payload := pkt.Payload[1:]
	var obus [][]byte

	for len(payload) > 0 {
		var obu []byte

		if w == 0 || byte(len(obus)) < (w-1) {
			var size av1.LEB128
			n, err := size.Unmarshal(payload)
			if err != nil {
				d.resetFragments()
				return nil, err
			}
			payload = payload[n:]

			if size == 0 || len(payload) < int(size) {
				d.resetFragments()
				return nil, fmt.Errorf("invalid OBU size")
			}

			obu, payload = payload[:size], payload[size:]
		} else {
			obu, payload = payload, nil
		}

		obus = append(obus, obu)
	}

	if w != 0 && len(obus) != int(w) {
		return nil, fmt.Errorf("invalid W field")
	}

	// first OBU is continuation of previous one
	if z {
		if d.fragmentsSize == 0 {
			if !d.firstPacketReceived {
				return nil, ErrNonStartingPacketAndNoPrevious
			}

			return nil, fmt.Errorf("received a subsequent fragment without previous fragments")
		}

		d.firstPacketReceived = true

		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		d.fragmentsSize += len(obus[0])

		if d.fragmentsSize > av1.MaxTemporalUnitSize {
			errSize := d.fragmentsSize
			d.resetFragments()
			return nil, fmt.Errorf("temporal unit size (%d) is too big, maximum is %d",
				errSize, av1.MaxTemporalUnitSize)
		}

		d.fragments = append(d.fragments, obus[0])
		d.fragmentNextSeqNum++

		if len(obus) == 1 && y {
			return nil, ErrMorePacketsNeeded
		}

		obus[0] = joinFragments(d.fragments, d.fragmentsSize)
		d.resetFragments()
	} else {
		d.firstPacketReceived = true
	}

	// last OBU will continue in next packet
	if y {
		var obu []byte
		obu, obus = obus[len(obus)-1], obus[:len(obus)-1]

		d.fragmentsSize = len(obu)
		d.fragments = append(d.fragments, obu)
		d.fragmentNextSeqNum = pkt.SequenceNumber + 1

		if len(obus) == 0 {
			return nil, ErrMorePacketsNeeded
		}
	}

	return obus, nil
}

// Decode decodes a temporal unit from a RTP packet.
func (d *Decoder) Decode(pkt *rtp.Packet) ([][]byte, error) {
	obus, err := d.decodeOBUs(pkt)
	if err != nil {
		return nil, err
	}
	l := len(obus)

	if (d.frameBufferLen + l) > av1.MaxOBUsPerTemporalUnit {
		errCount := d.frameBufferLen + l
		d.frameBuffer = nil
		d.frameBufferLen = 0
		d.frameBufferSize = 0
		return nil, fmt.Errorf("OBU count (%d) exceeds maximum allowed (%d)",
			errCount, av1.MaxOBUsPerTemporalUnit)
	}

	addSize := tuSize(obus)

	if (d.frameBufferSize + addSize) > av1.MaxTemporalUnitSize {
		errSize := d.frameBufferSize + addSize
		d.frameBuffer = nil
		d.frameBufferLen = 0
		d.frameBufferSize = 0
		return nil, fmt.Errorf("temporal unit size (%d) is too big, maximum is %d",
			errSize, av1.MaxOBUsPerTemporalUnit)
	}

	d.frameBuffer = append(d.frameBuffer, obus...)
	d.frameBufferLen += l
	d.frameBufferSize += addSize

	if !pkt.Marker {
		return nil, ErrMorePacketsNeeded
	}

	ret := d.frameBuffer

	// do not reuse frameBuffer to avoid race conditions
	d.frameBuffer = nil
	d.frameBufferLen = 0
	d.frameBufferSize = 0

	return ret, nil
}
