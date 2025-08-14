package rtph264

import (
	"bytes"
	"errors"
	"fmt"

	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h264"
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

func isAllZero(buf []byte) bool {
	for _, b := range buf {
		if b != 0 {
			return false
		}
	}
	return true
}

func auSize(au [][]byte) int {
	s := 0
	for _, nalu := range au {
		s += len(nalu)
	}
	return s
}

// Decoder is a RTP/H264 decoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc6184
type Decoder struct {
	// indicates the packetization mode.
	PacketizationMode int

	firstPacketReceived bool
	fragments           [][]byte
	fragmentsSize       int
	fragmentNextSeqNum  uint16
	annexBMode          bool

	// for Decode()
	frameBuffer          [][]byte
	frameBufferLen       int
	frameBufferSize      int
	frameBufferTimestamp uint32
}

// Init initializes the decoder.
func (d *Decoder) Init() error {
	if d.PacketizationMode >= 2 {
		return fmt.Errorf("PacketizationMode >= 2 is not supported")
	}
	return nil
}

func (d *Decoder) resetFragments() {
	d.fragments = d.fragments[:0]
	d.fragmentsSize = 0
}

func (d *Decoder) decodeNALUs(pkt *rtp.Packet) ([][]byte, error) {
	if len(pkt.Payload) < 1 {
		d.resetFragments()
		return nil, fmt.Errorf("payload is too short")
	}

	typ := h264.NALUType(pkt.Payload[0] & 0x1F)
	var nalus [][]byte

	switch typ {
	case h264.NALUTypeFUA:
		if len(pkt.Payload) < 2 {
			return nil, fmt.Errorf("invalid FU-A packet (invalid size)")
		}

		start := pkt.Payload[1] >> 7
		end := (pkt.Payload[1] >> 6) & 0x01

		if start == 1 {
			d.resetFragments()

			nri := (pkt.Payload[0] >> 5) & 0x03
			typ := pkt.Payload[1] & 0x1F
			d.fragmentsSize = len(pkt.Payload[1:])
			d.fragments = append(d.fragments, []byte{(nri << 5) | typ}, pkt.Payload[2:])
			d.fragmentNextSeqNum = pkt.SequenceNumber + 1
			d.firstPacketReceived = true

			// RFC 6184 clearly states:
			//
			//   A fragmented NAL unit MUST NOT be transmitted in one FU; that is, the
			//   Start bit and End bit MUST NOT both be set to one in the same FU
			//   header.
			//
			// However, some vendors camera (e.g. CostarHD) have been observed to nevertheless
			// emit one fragmented NAL unit for sufficiently small P-frames.
			if end != 0 {
				nalus = [][]byte{joinFragments(d.fragments, d.fragmentsSize)}
				d.resetFragments()
				break
			}

			return nil, ErrMorePacketsNeeded
		}

		if d.fragmentsSize == 0 {
			if !d.firstPacketReceived {
				return nil, ErrNonStartingPacketAndNoPrevious
			}

			return nil, fmt.Errorf("invalid FU-A packet (non-starting)")
		}

		if pkt.SequenceNumber != d.fragmentNextSeqNum {
			d.resetFragments()
			return nil, fmt.Errorf("discarding frame since a RTP packet is missing")
		}

		d.fragmentsSize += len(pkt.Payload[2:])

		if d.fragmentsSize > h264.MaxAccessUnitSize {
			errSize := d.fragmentsSize
			d.resetFragments()
			return nil, fmt.Errorf("NALU size (%d) is too big, maximum is %d",
				errSize, h264.MaxAccessUnitSize)
		}

		d.fragments = append(d.fragments, pkt.Payload[2:])
		d.fragmentNextSeqNum++

		if end != 1 {
			return nil, ErrMorePacketsNeeded
		}

		nalus = [][]byte{joinFragments(d.fragments, d.fragmentsSize)}
		d.resetFragments()

	case h264.NALUTypeSTAPA:
		d.resetFragments()

		payload := pkt.Payload[1:]

		for {
			if len(payload) < 2 {
				return nil, fmt.Errorf("invalid STAP-A packet (invalid size)")
			}

			size := uint16(payload[0])<<8 | uint16(payload[1])
			payload = payload[2:]

			if size == 0 {
				// discard padding
				if isAllZero(payload) {
					break
				}

				return nil, fmt.Errorf("invalid STAP-A packet (invalid size)")
			}

			if int(size) > len(payload) {
				return nil, fmt.Errorf("invalid STAP-A packet (invalid size)")
			}

			nalus = append(nalus, payload[:size])
			payload = payload[size:]

			if len(payload) == 0 {
				break
			}
		}

		if nalus == nil {
			return nil, fmt.Errorf("STAP-A packet doesn't contain any NALU")
		}

		d.firstPacketReceived = true

	case h264.NALUTypeSTAPB, h264.NALUTypeMTAP16,
		h264.NALUTypeMTAP24, h264.NALUTypeFUB:
		d.resetFragments()
		d.firstPacketReceived = true
		return nil, fmt.Errorf("packet type not supported (%v)", typ)

	default:
		d.resetFragments()
		d.firstPacketReceived = true
		nalus = [][]byte{pkt.Payload}
	}

	nalus, err := d.removeAnnexB(nalus)
	if err != nil {
		return nil, err
	}

	return nalus, nil
}

// Decode decodes an access unit from a RTP packet.
func (d *Decoder) Decode(pkt *rtp.Packet) ([][]byte, error) {
	nalus, err := d.decodeNALUs(pkt)
	if err != nil {
		return nil, err
	}
	l := len(nalus)

	// support splitting access units by timestamp.
	// (some cameras do not use the Marker field, like the FLIR M400)
	if d.frameBuffer != nil && pkt.Timestamp != d.frameBufferTimestamp {
		ret := d.frameBuffer
		d.resetFrameBuffer()

		err = d.addToFrameBuffer(nalus, l, pkt.Timestamp)
		if err != nil {
			return nil, err
		}

		return ret, nil
	}

	err = d.addToFrameBuffer(nalus, l, pkt.Timestamp)
	if err != nil {
		return nil, err
	}

	if !pkt.Marker {
		return nil, ErrMorePacketsNeeded
	}

	ret := d.frameBuffer
	d.resetFrameBuffer()

	return ret, nil
}

func (d *Decoder) resetFrameBuffer() {
	d.frameBuffer = nil // do not reuse frameBuffer to avoid race conditions
	d.frameBufferLen = 0
	d.frameBufferSize = 0
}

func (d *Decoder) addToFrameBuffer(nalus [][]byte, l int, ts uint32) error {
	if (d.frameBufferLen + l) > h264.MaxNALUsPerAccessUnit {
		errCount := d.frameBufferLen + l
		d.resetFrameBuffer()
		return fmt.Errorf("NALU count (%d) exceeds maximum allowed (%d)",
			errCount, h264.MaxNALUsPerAccessUnit)
	}

	addSize := auSize(nalus)

	if (d.frameBufferSize + addSize) > h264.MaxAccessUnitSize {
		errSize := d.frameBufferSize + addSize
		d.resetFrameBuffer()
		return fmt.Errorf("access unit size (%d) is too big, maximum is %d",
			errSize, h264.MaxAccessUnitSize)
	}

	d.frameBuffer = append(d.frameBuffer, nalus...)
	d.frameBufferLen += l
	d.frameBufferSize += addSize
	d.frameBufferTimestamp = ts
	return nil
}

// some cameras / servers wrap NALUs into Annex-B
func (d *Decoder) removeAnnexB(nalus [][]byte) ([][]byte, error) {
	if len(nalus) == 1 {
		nalu := nalus[0]

		if !d.annexBMode && bytes.Contains(nalu, []byte{0x00, 0x00, 0x00, 0x01}) {
			d.annexBMode = true
		}

		if d.annexBMode {
			if !bytes.HasPrefix(nalu, []byte{0x00, 0x00, 0x00, 0x01}) {
				nalu = append([]byte{0x00, 0x00, 0x00, 0x01}, nalu...)
			}

			var annexb h264.AnnexB
			err := annexb.Unmarshal(nalu)
			return annexb, err
		}
	}

	return nalus, nil
}
