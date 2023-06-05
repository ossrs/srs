// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import (
	"io"
	"sync"

	"github.com/pion/rtp"
)

const maxPayloadLen = 1460

type packetManager struct {
	headerPool  *sync.Pool
	payloadPool *sync.Pool
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
	}
}

func (m *packetManager) NewPacket(header *rtp.Header, payload []byte) (*retainablePacket, error) {
	if len(payload) > maxPayloadLen {
		return nil, io.ErrShortBuffer
	}

	p := &retainablePacket{
		onRelease: m.releasePacket,
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

	return p, nil
}

func (m *packetManager) releasePacket(header *rtp.Header, payload *[]byte) {
	m.headerPool.Put(header)
	if payload != nil {
		m.payloadPool.Put(payload)
	}
}

type noOpPacketFactory struct{}

func (f *noOpPacketFactory) NewPacket(header *rtp.Header, payload []byte) (*retainablePacket, error) {
	return &retainablePacket{
		onRelease: f.releasePacket,
		count:     1,
		header:    header,
		payload:   payload,
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
