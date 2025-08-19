// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtcp

import "fmt"

// RawPacket represents an unparsed RTCP packet. It's returned by Unmarshal when
// a packet with an unknown type is encountered.
type RawPacket []byte

// Marshal encodes the packet in binary.
func (r RawPacket) Marshal() ([]byte, error) {
	return r, nil
}

// Unmarshal decodes the packet from binary.
func (r *RawPacket) Unmarshal(b []byte) error {
	if len(b) < (headerLength) {
		return errPacketTooShort
	}
	*r = b

	var h Header
	return h.Unmarshal(b)
}

// Header returns the Header associated with this packet.
func (r RawPacket) Header() Header {
	var h Header
	if err := h.Unmarshal(r); err != nil {
		return Header{}
	}
	return h
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (r *RawPacket) DestinationSSRC() []uint32 {
	return []uint32{}
}

func (r RawPacket) String() string {
	out := fmt.Sprintf("RawPacket: %v", ([]byte)(r))
	return out
}

// MarshalSize returns the size of the packet once marshaled
func (r RawPacket) MarshalSize() int {
	return len(r)
}
