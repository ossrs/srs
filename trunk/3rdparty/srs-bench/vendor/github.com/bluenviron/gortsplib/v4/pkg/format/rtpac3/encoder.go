package rtpac3

import (
	"crypto/rand"

	"github.com/pion/rtp"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/ac3"
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

func packetCount(avail, le int) int {
	n := le / avail
	if (le % avail) != 0 {
		n++
	}
	return n
}

// Encoder is a AC-3 encoder.
// Specification: https://datatracker.ietf.org/doc/html/rfc4184
type Encoder struct {
	// payload type of packets.
	PayloadType uint8

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

	// split frames into batches
	for _, frame := range frames {
		if e.lenAggregated(batch, frame) <= e.PayloadMaxSize {
			// add to existing batch
			batch = append(batch, frame)
		} else {
			// write current batch
			if batch != nil {
				pkts, err := e.writeBatch(batch, timestamp)
				if err != nil {
					return nil, err
				}
				rets = append(rets, pkts...)
				timestamp += uint32(len(batch)) * ac3.SamplesPerFrame
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
	if len(frames) != 1 || e.lenAggregated(frames, nil) < e.PayloadMaxSize {
		return e.writeAggregated(frames, timestamp)
	}

	return e.writeFragmented(frames[0], timestamp)
}

func (e *Encoder) writeFragmented(frame []byte, timestamp uint32) ([]*rtp.Packet, error) {
	avail := e.PayloadMaxSize - 4
	le := len(frame)
	packetCount := packetCount(avail, le)

	ret := make([]*rtp.Packet, packetCount)
	le = avail

	ft := uint8(2)
	if avail >= (len(frame) * 5 / 8) {
		ft = 1
	}

	for i := range ret {
		if i == (packetCount - 1) {
			le = len(frame)
		}

		payload := make([]byte, 2+le)
		payload[0] = ft
		payload[1] = uint8(packetCount)

		n := copy(payload[2:], frame)
		frame = frame[n:]

		ret[i] = &rtp.Packet{
			Header: rtp.Header{
				Version:        rtpVersion,
				PayloadType:    e.PayloadType,
				SequenceNumber: e.sequenceNumber,
				Timestamp:      timestamp,
				SSRC:           *e.SSRC,
				Marker:         i == (packetCount - 1),
			},
			Payload: payload,
		}

		e.sequenceNumber++
		ft = 3
	}

	return ret, nil
}

func (e *Encoder) lenAggregated(frames [][]byte, addFrame []byte) int {
	n := 2 + len(addFrame)
	for _, frame := range frames {
		n += len(frame)
	}
	return n
}

func (e *Encoder) writeAggregated(frames [][]byte, timestamp uint32) ([]*rtp.Packet, error) {
	payload := make([]byte, e.lenAggregated(frames, nil))

	payload[1] = uint8(len(frames))

	n := 2
	for _, frame := range frames {
		n += copy(payload[n:], frame)
	}

	pkt := &rtp.Packet{
		Header: rtp.Header{
			Version:        rtpVersion,
			PayloadType:    e.PayloadType,
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
