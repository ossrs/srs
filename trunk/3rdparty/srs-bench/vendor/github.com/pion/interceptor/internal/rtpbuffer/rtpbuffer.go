// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package rtpbuffer provides a buffer for storing RTP packets
package rtpbuffer

import (
	"fmt"
)

const (
	// Uint16SizeHalf is half of a math.Uint16.
	Uint16SizeHalf = 1 << 15

	maxPayloadLen = 1460
)

// RTPBuffer stores RTP packets and allows custom logic
// around the lifetime of them via the PacketFactory.
type RTPBuffer struct {
	packets      []*RetainablePacket
	size         uint16
	highestAdded uint16
	started      bool
}

// NewRTPBuffer constructs a new RTPBuffer.
func NewRTPBuffer(size uint16) (*RTPBuffer, error) {
	allowedSizes := make([]uint16, 0)
	correctSize := false
	for i := 0; i < 16; i++ {
		if size == 1<<i {
			correctSize = true

			break
		}
		allowedSizes = append(allowedSizes, 1<<i)
	}

	if !correctSize {
		return nil, fmt.Errorf("%w: %d is not a valid size, allowed sizes: %v", ErrInvalidSize, size, allowedSizes)
	}

	return &RTPBuffer{
		packets: make([]*RetainablePacket, size),
		size:    size,
	}, nil
}

// Add places the RetainablePacket in the RTPBuffer.
func (r *RTPBuffer) Add(packet *RetainablePacket) {
	seq := packet.sequenceNumber
	if !r.started {
		r.packets[seq%r.size] = packet
		r.highestAdded = seq
		r.started = true

		return
	}

	diff := seq - r.highestAdded
	if diff == 0 {
		return
	} else if diff < Uint16SizeHalf {
		for i := r.highestAdded + 1; i != seq; i++ {
			idx := i % r.size
			prevPacket := r.packets[idx]
			if prevPacket != nil {
				prevPacket.Release()
			}
			r.packets[idx] = nil
		}
		r.highestAdded = seq
	}

	idx := seq % r.size
	prevPacket := r.packets[idx]
	if prevPacket != nil {
		prevPacket.Release()
	}
	r.packets[idx] = packet
}

// Get returns the RetainablePacket for the requested sequence number.
func (r *RTPBuffer) Get(seq uint16) *RetainablePacket {
	diff := r.highestAdded - seq
	if diff >= Uint16SizeHalf {
		return nil
	}

	if diff >= r.size {
		return nil
	}

	pkt := r.packets[seq%r.size]
	if pkt != nil {
		if pkt.sequenceNumber != seq {
			return nil
		}
		// already released
		if err := pkt.Retain(); err != nil {
			return nil
		}
	}

	return pkt
}
