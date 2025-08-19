// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package codecs

// OpusPayloader payloads Opus packets.
type OpusPayloader struct{}

// Payload fragments an Opus packet across one or more byte arrays.
func (p *OpusPayloader) Payload(_ uint16, payload []byte) [][]byte {
	if payload == nil {
		return [][]byte{}
	}

	out := make([]byte, len(payload))
	copy(out, payload)

	return [][]byte{out}
}

// OpusPacket represents the Opus header that is stored in the payload of an RTP Packet.
type OpusPacket struct {
	Payload []byte

	audioDepacketizer
}

// Unmarshal parses the passed byte slice and stores the result in the OpusPacket this method is called upon.
func (p *OpusPacket) Unmarshal(packet []byte) ([]byte, error) {
	if packet == nil {
		return nil, errNilPacket
	} else if len(packet) == 0 {
		return nil, errShortPacket
	}

	p.Payload = packet

	return packet, nil
}

// OpusPartitionHeadChecker checks Opus partition head.
//
// Deprecated: replaced by OpusPacket.IsPartitionHead().
type OpusPartitionHeadChecker struct{}

// IsPartitionHead checks whether if this is a head of the Opus partition.
//
// Deprecated: replaced by OpusPacket.IsPartitionHead().
func (*OpusPartitionHeadChecker) IsPartitionHead(packet []byte) bool {
	return (&OpusPacket{}).IsPartitionHead(packet)
}
