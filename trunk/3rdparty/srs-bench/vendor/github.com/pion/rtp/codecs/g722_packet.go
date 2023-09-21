package codecs

// G722Payloader payloads G722 packets
type G722Payloader struct{}

// Payload fragments an G722 packet across one or more byte arrays
func (p *G722Payloader) Payload(mtu uint16, payload []byte) [][]byte {
	var out [][]byte
	if payload == nil || mtu == 0 {
		return out
	}

	for len(payload) > int(mtu) {
		o := make([]byte, mtu)
		copy(o, payload[:mtu])
		payload = payload[mtu:]
		out = append(out, o)
	}
	o := make([]byte, len(payload))
	copy(o, payload)
	return append(out, o)
}
