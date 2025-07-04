// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtp

import (
	"time"
)

// Payloader payloads a byte array for use as rtp.Packet payloads.
type Payloader interface {
	Payload(mtu uint16, payload []byte) [][]byte
}

// Packetizer packetizes a payload.
type Packetizer interface {
	Packetize(payload []byte, samples uint32) []*Packet
	GeneratePadding(samples uint32) []*Packet
	EnableAbsSendTime(value int)
	SkipSamples(skippedSamples uint32)
}

type packetizer struct {
	MTU         uint16
	PayloadType uint8
	SSRC        uint32
	Payloader   Payloader
	Sequencer   Sequencer
	Timestamp   uint32

	// Deprecated: will be removed in a future version.
	ClockRate uint32

	// put extension numbers in here. If they're 0, the extension is disabled (0 is not a legal extension number)
	extensionNumbers struct {
		AbsSendTime int // http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
	}
	timegen func() time.Time
}

// NewPacketizer returns a new instance of a Packetizer for a specific payloader.
func NewPacketizer(
	mtu uint16,
	pt uint8,
	ssrc uint32,
	payloader Payloader,
	sequencer Sequencer,
	clockRate uint32,
) Packetizer {
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

// WithSSRC sets the SSRC for the Packetizer.
func WithSSRC(ssrc uint32) func(*packetizer) {
	return func(p *packetizer) {
		p.SSRC = ssrc
	}
}

// WithPayloadType sets the PayloadType for the Packetizer.
func WithPayloadType(pt uint8) func(*packetizer) {
	return func(p *packetizer) {
		p.PayloadType = pt
	}
}

// WithTimestamp sets the initial Timestamp for the Packetizer.
func WithTimestamp(timestamp uint32) func(*packetizer) {
	return func(p *packetizer) {
		p.Timestamp = timestamp
	}
}

// PacketizerOption is a function that configures a RTP Packetizer.
type PacketizerOption func(*packetizer)

// NewPacketizerWithOptions returns a new instance of a Packetizer with the given options.
func NewPacketizerWithOptions(
	mtu uint16,
	payloader Payloader,
	sequencer Sequencer,
	clockRate uint32,
	options ...PacketizerOption,
) Packetizer {
	packetizerInstance := &packetizer{
		MTU:       mtu,
		Payloader: payloader,
		Sequencer: sequencer,
		Timestamp: globalMathRandomGenerator.Uint32(),
		ClockRate: clockRate,
		timegen:   time.Now,
	}

	for _, option := range options {
		option(packetizerInstance)
	}

	return packetizerInstance
}

func (p *packetizer) EnableAbsSendTime(value int) {
	p.extensionNumbers.AbsSendTime = value
}

// Packetize packetizes the payload of an RTP packet and returns one or more RTP packets.
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
				CSRC:           []uint32{},
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
		err = packets[len(packets)-1].SetExtension(uint8(p.extensionNumbers.AbsSendTime), b) // nolint: gosec // G115
		if err != nil {
			return nil // never happens
		}
	}

	return packets
}

// GeneratePadding returns required padding-only packages.
func (p *packetizer) GeneratePadding(samples uint32) []*Packet {
	// Guard against an empty payload
	if samples == 0 {
		return nil
	}

	packets := make([]*Packet, samples)

	for i := 0; i < int(samples); i++ {
		packets[i] = &Packet{
			Header: Header{
				Version:        2,
				Padding:        true,
				Extension:      false,
				Marker:         false,
				PayloadType:    p.PayloadType,
				SequenceNumber: p.Sequencer.NextSequenceNumber(),
				Timestamp:      p.Timestamp, // Use latest timestamp
				SSRC:           p.SSRC,
				CSRC:           []uint32{},
				PaddingSize:    255,
			},
		}
	}

	return packets
}

// SkipSamples causes a gap in sample count between Packetize requests so the
// RTP payloads produced have a gap in timestamps.
func (p *packetizer) SkipSamples(skippedSamples uint32) {
	p.Timestamp += skippedSamples
}
