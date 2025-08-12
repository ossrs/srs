// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package util

import "github.com/pion/rtp"

// MediaPacketIterator supports iterating through a list of media packets protected by
// a specific Fec packet.
type MediaPacketIterator struct {
	mediaPackets   []rtp.Packet
	coveredIndices []uint32
	nextIndex      int
}

// NewMediaPacketIterator returns a new MediaPacketIterator.
func NewMediaPacketIterator(mediaPackets []rtp.Packet, coveredIndices []uint32) *MediaPacketIterator {
	return &MediaPacketIterator{
		mediaPackets:   mediaPackets,
		coveredIndices: coveredIndices,
		nextIndex:      0,
	}
}

// Reset sets the starting iterating index back to 0.
func (m *MediaPacketIterator) Reset() *MediaPacketIterator {
	m.nextIndex = 0

	return m
}

// HasNext indicates whether or not there are more media packets
// that can be iterated through.
func (m *MediaPacketIterator) HasNext() bool {
	return m.nextIndex < len(m.coveredIndices)
}

// Next returns the next media packet to iterate through.
func (m *MediaPacketIterator) Next() *rtp.Packet {
	if m.nextIndex == len(m.coveredIndices) {
		return nil
	}
	packet := m.mediaPackets[m.coveredIndices[m.nextIndex]]
	m.nextIndex++

	return &packet
}

// First returns the first media packet to iterate through.
func (m *MediaPacketIterator) First() *rtp.Packet {
	if len(m.coveredIndices) == 0 {
		return nil
	}

	return &m.mediaPackets[m.coveredIndices[0]]
}
