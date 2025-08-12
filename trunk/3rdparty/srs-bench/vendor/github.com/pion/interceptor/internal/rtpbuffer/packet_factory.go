// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtpbuffer

import (
	"encoding/binary"
	"io"
	"sync"

	"github.com/pion/rtp"
)

const rtxSsrcByteLength = 2

// PacketFactory allows custom logic around the handle of RTP Packets before they added to the RTPBuffer.
// The NoOpPacketFactory doesn't copy packets, while the RetainablePacket will take a copy before adding.
type PacketFactory interface {
	NewPacket(header *rtp.Header, payload []byte, rtxSsrc uint32, rtxPayloadType uint8) (*RetainablePacket, error)
}

// PacketFactoryCopy is PacketFactory that takes a copy of packets when added to the RTPBuffer.
type PacketFactoryCopy struct {
	headerPool   *sync.Pool
	payloadPool  *sync.Pool
	rtxSequencer rtp.Sequencer
}

// NewPacketFactoryCopy constructs a PacketFactory that takes a copy of packets when added to the RTPBuffer.
func NewPacketFactoryCopy() *PacketFactoryCopy {
	return &PacketFactoryCopy{
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

// NewPacket constructs a new RetainablePacket that can be added to the RTPBuffer.
//
//nolint:cyclop
func (m *PacketFactoryCopy) NewPacket(
	header *rtp.Header, payload []byte, rtxSsrc uint32, rtxPayloadType uint8,
) (*RetainablePacket, error) {
	if len(payload) > maxPayloadLen {
		return nil, io.ErrShortBuffer
	}

	retainablePacket := &RetainablePacket{
		onRelease:      m.releasePacket,
		sequenceNumber: header.SequenceNumber,
		// new packets have retain count of 1
		count: 1,
	}

	var ok bool
	retainablePacket.header, ok = m.headerPool.Get().(*rtp.Header)
	if !ok {
		return nil, errFailedToCastHeaderPool
	}

	*retainablePacket.header = header.Clone()

	if payload != nil {
		retainablePacket.buffer, ok = m.payloadPool.Get().(*[]byte)
		if !ok {
			return nil, errFailedToCastPayloadPool
		}
		if rtxSsrc != 0 && rtxPayloadType != 0 {
			size := copy((*retainablePacket.buffer)[rtxSsrcByteLength:], payload)
			retainablePacket.payload = (*retainablePacket.buffer)[:size+rtxSsrcByteLength]
		} else {
			size := copy(*retainablePacket.buffer, payload)
			retainablePacket.payload = (*retainablePacket.buffer)[:size]
		}
	}

	if rtxSsrc != 0 && rtxPayloadType != 0 { //nolint:nestif
		if payload == nil {
			retainablePacket.buffer, ok = m.payloadPool.Get().(*[]byte)
			if !ok {
				return nil, errFailedToCastPayloadPool
			}
			retainablePacket.payload = (*retainablePacket.buffer)[:rtxSsrcByteLength]
		}
		// Write the original sequence number at the beginning of the payload.
		binary.BigEndian.PutUint16(retainablePacket.payload, retainablePacket.header.SequenceNumber)

		// Rewrite the SSRC.
		retainablePacket.header.SSRC = rtxSsrc
		// Rewrite the payload type.
		retainablePacket.header.PayloadType = rtxPayloadType
		// Rewrite the sequence number.
		retainablePacket.header.SequenceNumber = m.rtxSequencer.NextSequenceNumber()
		// Remove padding if present.
		if retainablePacket.header.Padding {
			// Older versions of pion/rtp didn't have the Header.PaddingSize field and as a workaround
			// users had to add padding to the payload. We need to handle this case here.
			if retainablePacket.header.PaddingSize == 0 && len(retainablePacket.payload) > 0 {
				paddingLength := int(retainablePacket.payload[len(retainablePacket.payload)-1])
				if paddingLength > len(retainablePacket.payload) {
					return nil, errPaddingOverflow
				}
				retainablePacket.payload = (*retainablePacket.buffer)[:len(retainablePacket.payload)-paddingLength]
			}

			retainablePacket.header.Padding = false
			retainablePacket.header.PaddingSize = 0
		}
	}

	return retainablePacket, nil
}

func (m *PacketFactoryCopy) releasePacket(header *rtp.Header, payload *[]byte) {
	m.headerPool.Put(header)
	if payload != nil {
		m.payloadPool.Put(payload)
	}
}

// PacketFactoryNoOp is a PacketFactory implementation that doesn't copy packets.
type PacketFactoryNoOp struct{}

// NewPacket constructs a new RetainablePacket that can be added to the RTPBuffer.
func (f *PacketFactoryNoOp) NewPacket(
	header *rtp.Header, payload []byte, _ uint32, _ uint8,
) (*RetainablePacket, error) {
	return &RetainablePacket{
		onRelease:      f.releasePacket,
		count:          1,
		header:         header,
		payload:        payload,
		sequenceNumber: header.SequenceNumber,
	}, nil
}

func (f *PacketFactoryNoOp) releasePacket(_ *rtp.Header, _ *[]byte) {
	// no-op
}
