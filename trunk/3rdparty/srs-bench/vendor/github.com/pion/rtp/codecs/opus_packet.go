package codecs

// OpusPayloader payloads Opus packets
type OpusPayloader struct{}

// Payload fragments an Opus packet across one or more byte arrays
func (p *OpusPayloader) Payload(mtu int, payload []byte) [][]byte {
	if payload == nil {
		return [][]byte{}
	}

	out := make([]byte, len(payload))
	copy(out, payload)
	return [][]byte{out}
}

// OpusPacket represents the Opus header that is stored in the payload of an RTP Packet
type OpusPacket struct {
	Payload []byte
}

// Unmarshal parses the passed byte slice and stores the result in the OpusPacket this method is called upon
func (p *OpusPacket) Unmarshal(packet []byte) ([]byte, error) {
	if packet == nil {
		return nil, errNilPacket
	} else if len(packet) == 0 {
		return nil, errShortPacket
	}

	p.Payload = packet
	return packet, nil
}

// OpusPartitionHeadChecker checks Opus partition head
type OpusPartitionHeadChecker struct{}

// IsPartitionHead checks whether if this is a head of the Opus partition
func (*OpusPartitionHeadChecker) IsPartitionHead(packet []byte) bool {
	p := &OpusPacket{}
	if _, err := p.Unmarshal(packet); err != nil {
		return false
	}
	return true
}
