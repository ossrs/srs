package rtpmpeg1audio

import (
	"crypto/rand"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg1audio"
	"github.com/pion/rtp"
)

const (
	rtpVersion            = 2
	defaultPayloadMaxSize = 1460 // 1500 (UDP MTU) - 20 (IP header) - 8 (UDP header) - 12 (RTP header)
)

func randUint32() (uint32, error) {
	var b [4]byte
	_, err := rand.Read(b[:])
	if err != nil {
		return 0, err
	}
	return uint32(b[0])<<24 | uint32(b[1])<<16 | uint32(b[2])<<8 | uint32(b[3]), nil
}

func lenAggregated(frames [][]byte, frame []byte) int {
	n := 4 + len(frame)
	for _, fr := range frames {
		n += len(fr)
	}
	return n
}

func packetCount(avail, le int) int {
	n := le / avail
	if (le % avail) != 0 {
		n++
	}
	return n
}

// Encoder is a RTP/MPEG-1/2 Audio encoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc2250
type Encoder struct {
	// SSRC of packets (optional).
	// It defaults to a random value.
	SSRC *uint32

	// initial sequence number of packets (optional).
	// It defaults to a random value.
	InitialSequenceNumber *uint16

	// maximum size of packet payloads (optional).
	// It defaults to 1460.
	PayloadMaxSize int

	sequenceNumber uint16
}

// Init initializes the encoder.
func (e *Encoder) Init() error {
	if e.SSRC == nil {
		v, err := randUint32()
		if err != nil {
			return err
		}
		e.SSRC = &v
	}
	if e.InitialSequenceNumber == nil {
		v, err := randUint32()
		if err != nil {
			return err
		}
		v2 := uint16(v)
		e.InitialSequenceNumber = &v2
	}
	if e.PayloadMaxSize == 0 {
		e.PayloadMaxSize = defaultPayloadMaxSize
	}

	e.sequenceNumber = *e.InitialSequenceNumber
	return nil
}

// Encode encodes frames into RTP packets.
func (e *Encoder) Encode(frames [][]byte) ([]*rtp.Packet, error) {
	var rets []*rtp.Packet
	var batch [][]byte
	timestamp := uint32(0)

	for _, frame := range frames {
		if lenAggregated(batch, frame) <= e.PayloadMaxSize {
			batch = append(batch, frame)
		} else {
			// write current batch
			if batch != nil {
				pkts, err := e.writeBatch(batch, timestamp)
				if err != nil {
					return nil, err
				}
				rets = append(rets, pkts...)

				for _, frame := range batch {
					var h mpeg1audio.FrameHeader
					err := h.Unmarshal(frame)
					if err != nil {
						return nil, err
					}

					timestamp += uint32(h.SampleCount())
				}
			}

			// initialize new batch
			batch = [][]byte{frame}
		}
	}

	// write last batch
	pkts, err := e.writeBatch(batch, timestamp)
	if err != nil {
		return nil, err
	}
	rets = append(rets, pkts...)

	return rets, nil
}

func (e *Encoder) writeBatch(frames [][]byte, timestamp uint32) ([]*rtp.Packet, error) {
	if len(frames) != 1 || lenAggregated(frames, nil) < e.PayloadMaxSize {
		return e.writeAggregated(frames, timestamp)
	}

	return e.writeFragmented(frames[0], timestamp)
}

func (e *Encoder) writeFragmented(frame []byte, timestamp uint32) ([]*rtp.Packet, error) {
	avail := e.PayloadMaxSize - 4
	le := len(frame)
	packetCount := packetCount(avail, le)

	ret := make([]*rtp.Packet, packetCount)
	pos := 0
	le = avail

	for i := range ret {
		if i == (packetCount - 1) {
			le = len(frame) - pos
		}

		payload := make([]byte, 4+le)
		payload[2] = byte(pos >> 8)
		payload[3] = byte(pos)

		pos += copy(payload[4:], frame[pos:])

		ret[i] = &rtp.Packet{
			Header: rtp.Header{
				Version:        rtpVersion,
				PayloadType:    14,
				SequenceNumber: e.sequenceNumber,
				Timestamp:      timestamp,
				SSRC:           *e.SSRC,
				Marker:         true,
			},
			Payload: payload,
		}

		e.sequenceNumber++
	}

	return ret, nil
}

func (e *Encoder) writeAggregated(frames [][]byte, timestamp uint32) ([]*rtp.Packet, error) {
	payload := make([]byte, lenAggregated(frames, nil))

	n := 4
	for _, frame := range frames {
		n += copy(payload[n:], frame)
	}

	pkt := &rtp.Packet{
		Header: rtp.Header{
			Version:        rtpVersion,
			PayloadType:    14,
			SequenceNumber: e.sequenceNumber,
			Timestamp:      timestamp,
			SSRC:           *e.SSRC,
			Marker:         true,
		},
		Payload: payload,
	}

	e.sequenceNumber++

	return []*rtp.Packet{pkt}, nil
}
