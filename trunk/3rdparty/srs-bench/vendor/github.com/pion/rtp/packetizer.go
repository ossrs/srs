package rtp

import (
	"time"
)

// Payloader payloads a byte array for use as rtp.Packet payloads
type Payloader interface {
	Payload(mtu int, payload []byte) [][]byte
}

// Packetizer packetizes a payload
type Packetizer interface {
	Packetize(payload []byte, samples uint32) []*Packet
	EnableAbsSendTime(value int)
}

type packetizer struct {
	MTU              int
	PayloadType      uint8
	SSRC             uint32
	Payloader        Payloader
	Sequencer        Sequencer
	Timestamp        uint32
	ClockRate        uint32
	extensionNumbers struct { // put extension numbers in here. If they're 0, the extension is disabled (0 is not a legal extension number)
		AbsSendTime int // http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
	}
	timegen func() time.Time
}

// NewPacketizer returns a new instance of a Packetizer for a specific payloader
func NewPacketizer(mtu int, pt uint8, ssrc uint32, payloader Payloader, sequencer Sequencer, clockRate uint32) Packetizer {
	return &packetizer{
		MTU:         mtu,
		PayloadType: pt,
		SSRC:        ssrc,
		Payloader:   payloader,
		Sequencer:   sequencer,
		Timestamp:   globalMathRandomGenerator.Uint32(),
		ClockRate:   clockRate,
		timegen:     time.Now,
	}
}

func (p *packetizer) EnableAbsSendTime(value int) {
	p.extensionNumbers.AbsSendTime = value
}

// Packetize packetizes the payload of an RTP packet and returns one or more RTP packets
func (p *packetizer) Packetize(payload []byte, samples uint32) []*Packet {
	// Guard against an empty payload
	if len(payload) == 0 {
		return nil
	}

	payloads := p.Payloader.Payload(p.MTU-12, payload)
	packets := make([]*Packet, len(payloads))

	for i, pp := range payloads {
		packets[i] = &Packet{
			Header: Header{
				Version:        2,
				Padding:        false,
				Extension:      false,
				Marker:         i == len(payloads)-1,
				PayloadType:    p.PayloadType,
				SequenceNumber: p.Sequencer.NextSequenceNumber(),
				Timestamp:      p.Timestamp, // Figure out how to do timestamps
				SSRC:           p.SSRC,
			},
			Payload: pp,
		}
	}
	p.Timestamp += samples

	if len(packets) != 0 && p.extensionNumbers.AbsSendTime != 0 {
		sendTime := NewAbsSendTimeExtension(p.timegen())
		// apply http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
		b, err := sendTime.Marshal()
		if err != nil {
			return nil // never happens
		}
		err = packets[len(packets)-1].SetExtension(uint8(p.extensionNumbers.AbsSendTime), b)
		if err != nil {
			return nil // never happens
		}
	}

	return packets
}
