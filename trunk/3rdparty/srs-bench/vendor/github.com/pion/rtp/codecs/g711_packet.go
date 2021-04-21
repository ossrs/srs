package codecs

// G711Payloader payloads G711 packets
type G711Payloader struct{}

// Payload fragments an G711 packet across one or more byte arrays
func (p *G711Payloader) Payload(mtu int, payload []byte) [][]byte {
	var out [][]byte
	if payload == nil || mtu <= 0 {
		return out
	}

	for len(payload) > mtu {
		o := make([]byte, mtu)
		copy(o, payload[:mtu])
		payload = payload[mtu:]
		out = append(out, o)
	}
	o := make([]byte, len(payload))
	copy(o, payload)
	return append(out, o)
}
