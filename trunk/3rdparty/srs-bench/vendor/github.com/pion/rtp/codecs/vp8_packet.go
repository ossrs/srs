package codecs

// VP8Payloader payloads VP8 packets
type VP8Payloader struct{}

const (
	vp8HeaderSize = 1
)

// Payload fragments a VP8 packet across one or more byte arrays
func (p *VP8Payloader) Payload(mtu int, payload []byte) [][]byte {
	/*
	 * https://tools.ietf.org/html/rfc7741#section-4.2
	 *
	 *       0 1 2 3 4 5 6 7
	 *      +-+-+-+-+-+-+-+-+
	 *      |X|R|N|S|R| PID | (REQUIRED)
	 *      +-+-+-+-+-+-+-+-+
	 * X:   |I|L|T|K| RSV   | (OPTIONAL)
	 *      +-+-+-+-+-+-+-+-+
	 * I:   |M| PictureID   | (OPTIONAL)
	 *      +-+-+-+-+-+-+-+-+
	 * L:   |   TL0PICIDX   | (OPTIONAL)
	 *      +-+-+-+-+-+-+-+-+
	 * T/K: |TID|Y| KEYIDX  | (OPTIONAL)
	 *      +-+-+-+-+-+-+-+-+
	 *  S: Start of VP8 partition.  SHOULD be set to 1 when the first payload
	 *     octet of the RTP packet is the beginning of a new VP8 partition,
	 *     and MUST NOT be 1 otherwise.  The S bit MUST be set to 1 for the
	 *     first packet of each encoded frame.
	 */

	maxFragmentSize := mtu - vp8HeaderSize

	payloadData := payload
	payloadDataRemaining := len(payload)

	payloadDataIndex := 0
	var payloads [][]byte

	// Make sure the fragment/payload size is correct
	if min(maxFragmentSize, payloadDataRemaining) <= 0 {
		return payloads
	}
	for payloadDataRemaining > 0 {
		currentFragmentSize := min(maxFragmentSize, payloadDataRemaining)
		out := make([]byte, vp8HeaderSize+currentFragmentSize)
		if payloadDataRemaining == len(payload) {
			out[0] = 0x10
		}

		copy(out[vp8HeaderSize:], payloadData[payloadDataIndex:payloadDataIndex+currentFragmentSize])
		payloads = append(payloads, out)

		payloadDataRemaining -= currentFragmentSize
		payloadDataIndex += currentFragmentSize
	}

	return payloads
}

// VP8Packet represents the VP8 header that is stored in the payload of an RTP Packet
type VP8Packet struct {
	// Required Header
	X   uint8 /* extended controlbits present */
	N   uint8 /* (non-reference frame)  when set to 1 this frame can be discarded */
	S   uint8 /* start of VP8 partition */
	PID uint8 /* partition index */

	// Optional Header
	I         uint8  /* 1 if PictureID is present */
	L         uint8  /* 1 if TL0PICIDX is present */
	T         uint8  /* 1 if TID is present */
	K         uint8  /* 1 if KEYIDX is present */
	PictureID uint16 /* 8 or 16 bits, picture ID */
	TL0PICIDX uint8  /* 8 bits temporal level zero index */

	Payload []byte
}

// Unmarshal parses the passed byte slice and stores the result in the VP8Packet this method is called upon
func (p *VP8Packet) Unmarshal(payload []byte) ([]byte, error) {
	if payload == nil {
		return nil, errNilPacket
	}

	payloadLen := len(payload)

	if payloadLen < 4 {
		return nil, errShortPacket
	}

	payloadIndex := 0

	p.X = (payload[payloadIndex] & 0x80) >> 7
	p.N = (payload[payloadIndex] & 0x20) >> 5
	p.S = (payload[payloadIndex] & 0x10) >> 4
	p.PID = payload[payloadIndex] & 0x07

	payloadIndex++

	if p.X == 1 {
		p.I = (payload[payloadIndex] & 0x80) >> 7
		p.L = (payload[payloadIndex] & 0x40) >> 6
		p.T = (payload[payloadIndex] & 0x20) >> 5
		p.K = (payload[payloadIndex] & 0x10) >> 4
		payloadIndex++
	}

	if p.I == 1 { // PID present?
		if payload[payloadIndex]&0x80 > 0 { // M == 1, PID is 16bit
			payloadIndex += 2
		} else {
			payloadIndex++
		}
	}

	if p.L == 1 {
		payloadIndex++
	}

	if p.T == 1 || p.K == 1 {
		payloadIndex++
	}

	if payloadIndex >= payloadLen {
		return nil, errShortPacket
	}
	p.Payload = payload[payloadIndex:]
	return p.Payload, nil
}

// VP8PartitionHeadChecker checks VP8 partition head
type VP8PartitionHeadChecker struct{}

// IsPartitionHead checks whether if this is a head of the VP8 partition
func (*VP8PartitionHeadChecker) IsPartitionHead(packet []byte) bool {
	p := &VP8Packet{}
	if _, err := p.Unmarshal(packet); err != nil {
		return false
	}
	return p.S == 1
}
