// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import (
	"encoding/binary"
	"io"
	"sync"

	"github.com/pion/rtp"
)

const maxPayloadLen = 1460

type packetManager struct {
	headerPool   *sync.Pool
	payloadPool  *sync.Pool
	rtxSequencer rtp.Sequencer
}

func newPacketManager() *packetManager {
	return &packetManager{
		headerPool: &sync.Pool{
			New: func() interface{} {
				return &rtp.Header{}
			},
		},
		payloadPool: &sync.Pool{
			New: func() interface{} {
				buf := make([]byte, maxPayloadLen)
				return &buf
			},
		},
		rtxSequencer: rtp.NewRandomSequencer(),
	}
}

func (m *packetManager) NewPacket(header *rtp.Header, payload []byte, rtxSsrc uint32, rtxPayloadType uint8) (*retainablePacket, error) {
	if len(payload) > maxPayloadLen {
		return nil, io.ErrShortBuffer
	}

	p := &retainablePacket{
		onRelease:      m.releasePacket,
		sequenceNumber: header.SequenceNumber,
		// new packets have retain count of 1
		count: 1,
	}

	var ok bool
	p.header, ok = m.headerPool.Get().(*rtp.Header)
	if !ok {
		return nil, errFailedToCastHeaderPool
	}

	*p.header = header.Clone()

	if payload != nil {
		p.buffer, ok = m.payloadPool.Get().(*[]byte)
		if !ok {
			return nil, errFailedToCastPayloadPool
		}

		size := copy(*p.buffer, payload)
		p.payload = (*p.buffer)[:size]
	}

	if rtxSsrc != 0 && rtxPayloadType != 0 {
		// Store the original sequence number and rewrite the sequence number.
		originalSequenceNumber := p.header.SequenceNumber
		p.header.SequenceNumber = m.rtxSequencer.NextSequenceNumber()

		// Rewrite the SSRC.
		p.header.SSRC = rtxSsrc
		// Rewrite the payload type.
		p.header.PayloadType = rtxPayloadType

		// Remove padding if present.
		paddingLength := 0
		if p.header.Padding && p.payload != nil && len(p.payload) > 0 {
			paddingLength = int(p.payload[len(p.payload)-1])
			p.header.Padding = false
		}

		// Write the original sequence number at the beginning of the payload.
		payload := make([]byte, 2)
		binary.BigEndian.PutUint16(payload, originalSequenceNumber)
		p.payload = append(payload, p.payload[:len(p.payload)-paddingLength]...)
	}

	return p, nil
}

func (m *packetManager) releasePacket(header *rtp.Header, payload *[]byte) {
	m.headerPool.Put(header)
	if payload != nil {
		m.payloadPool.Put(payload)
	}
}

type noOpPacketFactory struct{}

func (f *noOpPacketFactory) NewPacket(header *rtp.Header, payload []byte, _ uint32, _ uint8) (*retainablePacket, error) {
	return &retainablePacket{
		onRelease:      f.releasePacket,
		count:          1,
		header:         header,
		payload:        payload,
		sequenceNumber: header.SequenceNumber,
	}, nil
}

func (f *noOpPacketFactory) releasePacket(_ *rtp.Header, _ *[]byte) {
	// no-op
}

type retainablePacket struct {
	onRelease func(*rtp.Header, *[]byte)

	countMu sync.Mutex
	count   int

	header  *rtp.Header
	buffer  *[]byte
	payload []byte

	sequenceNumber uint16
}

func (p *retainablePacket) Header() *rtp.Header {
	return p.header
}

func (p *retainablePacket) Payload() []byte {
	return p.payload
}

func (p *retainablePacket) Retain() error {
	p.countMu.Lock()
	defer p.countMu.Unlock()
	if p.count == 0 {
		// already released
		return errPacketReleased
	}
	p.count++
	return nil
}

func (p *retainablePacket) Release() {
	p.countMu.Lock()
	defer p.countMu.Unlock()
	p.count--

	if p.count == 0 {
		// release back to pool
		p.onRelease(p.header, p.buffer)
		p.header = nil
		p.buffer = nil
		p.payload = nil
	}
}
