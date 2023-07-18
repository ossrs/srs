package codecs

import (
	"github.com/pion/randutil"
)

// Use global random generator to properly seed by crypto grade random.
var globalMathRandomGenerator = randutil.NewMathRandomGenerator() // nolint:gochecknoglobals

// VP9Payloader payloads VP9 packets
type VP9Payloader struct {
	pictureID   uint16
	initialized bool

	// InitialPictureIDFn is a function that returns random initial picture ID.
	InitialPictureIDFn func() uint16
}

const (
	vp9HeaderSize    = 3 // Flexible mode 15 bit picture ID
	maxSpatialLayers = 5
	maxVP9RefPics    = 3
)

// Payload fragments an VP9 packet across one or more byte arrays
func (p *VP9Payloader) Payload(mtu uint16, payload []byte) [][]byte {
	/*
	 * https://www.ietf.org/id/draft-ietf-payload-vp9-13.txt
	 *
	 * Flexible mode (F=1)
	 *        0 1 2 3 4 5 6 7
	 *       +-+-+-+-+-+-+-+-+
	 *       |I|P|L|F|B|E|V|Z| (REQUIRED)
	 *       +-+-+-+-+-+-+-+-+
	 *  I:   |M| PICTURE ID  | (REQUIRED)
	 *       +-+-+-+-+-+-+-+-+
	 *  M:   | EXTENDED PID  | (RECOMMENDED)
	 *       +-+-+-+-+-+-+-+-+
	 *  L:   | TID |U| SID |D| (CONDITIONALLY RECOMMENDED)
	 *       +-+-+-+-+-+-+-+-+                             -\
	 *  P,F: | P_DIFF      |N| (CONDITIONALLY REQUIRED)    - up to 3 times
	 *       +-+-+-+-+-+-+-+-+                             -/
	 *  V:   | SS            |
	 *       | ..            |
	 *       +-+-+-+-+-+-+-+-+
	 *
	 * Non-flexible mode (F=0)
	 *        0 1 2 3 4 5 6 7
	 *       +-+-+-+-+-+-+-+-+
	 *       |I|P|L|F|B|E|V|Z| (REQUIRED)
	 *       +-+-+-+-+-+-+-+-+
	 *  I:   |M| PICTURE ID  | (RECOMMENDED)
	 *       +-+-+-+-+-+-+-+-+
	 *  M:   | EXTENDED PID  | (RECOMMENDED)
	 *       +-+-+-+-+-+-+-+-+
	 *  L:   | TID |U| SID |D| (CONDITIONALLY RECOMMENDED)
	 *       +-+-+-+-+-+-+-+-+
	 *       |   TL0PICIDX   | (CONDITIONALLY REQUIRED)
	 *       +-+-+-+-+-+-+-+-+
	 *  V:   | SS            |
	 *       | ..            |
	 *       +-+-+-+-+-+-+-+-+
	 */

	if !p.initialized {
		if p.InitialPictureIDFn == nil {
			p.InitialPictureIDFn = func() uint16 {
				return uint16(globalMathRandomGenerator.Intn(0x7FFF))
			}
		}
		p.pictureID = p.InitialPictureIDFn() & 0x7FFF
		p.initialized = true
	}
	if payload == nil {
		return [][]byte{}
	}

	maxFragmentSize := int(mtu) - vp9HeaderSize
	payloadDataRemaining := len(payload)
	payloadDataIndex := 0

	if min(maxFragmentSize, payloadDataRemaining) <= 0 {
		return [][]byte{}
	}

	var payloads [][]byte
	for payloadDataRemaining > 0 {
		currentFragmentSize := min(maxFragmentSize, payloadDataRemaining)
		out := make([]byte, vp9HeaderSize+currentFragmentSize)

		out[0] = 0x90 // F=1 I=1
		if payloadDataIndex == 0 {
			out[0] |= 0x08 // B=1
		}
		if payloadDataRemaining == currentFragmentSize {
			out[0] |= 0x04 // E=1
		}
		out[1] = byte(p.pictureID>>8) | 0x80
		out[2] = byte(p.pictureID)
		copy(out[vp9HeaderSize:], payload[payloadDataIndex:payloadDataIndex+currentFragmentSize])
		payloads = append(payloads, out)

		payloadDataRemaining -= currentFragmentSize
		payloadDataIndex += currentFragmentSize
	}
	p.pictureID++
	if p.pictureID >= 0x8000 {
		p.pictureID = 0
	}

	return payloads
}

// VP9Packet represents the VP9 header that is stored in the payload of an RTP Packet
type VP9Packet struct {
	// Required header
	I bool // PictureID is present
	P bool // Inter-picture predicted frame
	L bool // Layer indices is present
	F bool // Flexible mode
	B bool // Start of a frame
	E bool // End of a frame
	V bool // Scalability structure (SS) data present
	Z bool // Not a reference frame for upper spatial layers

	// Recommended headers
	PictureID uint16 // 7 or 16 bits, picture ID

	// Conditionally recommended headers
	TID uint8 // Temporal layer ID
	U   bool  // Switching up point
	SID uint8 // Spatial layer ID
	D   bool  // Inter-layer dependency used

	// Conditionally required headers
	PDiff     []uint8 // Reference index (F=1)
	TL0PICIDX uint8   // Temporal layer zero index (F=0)

	// Scalability structure headers
	NS      uint8 // N_S + 1 indicates the number of spatial layers present in the VP9 stream
	Y       bool  // Each spatial layer's frame resolution present
	G       bool  // PG description present flag.
	NG      uint8 // N_G indicates the number of pictures in a Picture Group (PG)
	Width   []uint16
	Height  []uint16
	PGTID   []uint8   // Temporal layer ID of pictures in a Picture Group
	PGU     []bool    // Switching up point of pictures in a Picture Group
	PGPDiff [][]uint8 // Reference indecies of pictures in a Picture Group

	Payload []byte

	videoDepacketizer
}

// Unmarshal parses the passed byte slice and stores the result in the VP9Packet this method is called upon
func (p *VP9Packet) Unmarshal(packet []byte) ([]byte, error) {
	if packet == nil {
		return nil, errNilPacket
	}
	if len(packet) < 1 {
		return nil, errShortPacket
	}

	p.I = packet[0]&0x80 != 0
	p.P = packet[0]&0x40 != 0
	p.L = packet[0]&0x20 != 0
	p.F = packet[0]&0x10 != 0
	p.B = packet[0]&0x08 != 0
	p.E = packet[0]&0x04 != 0
	p.V = packet[0]&0x02 != 0
	p.Z = packet[0]&0x01 != 0

	pos := 1
	var err error

	if p.I {
		pos, err = p.parsePictureID(packet, pos)
		if err != nil {
			return nil, err
		}
	}

	if p.L {
		pos, err = p.parseLayerInfo(packet, pos)
		if err != nil {
			return nil, err
		}
	}

	if p.F && p.P {
		pos, err = p.parseRefIndices(packet, pos)
		if err != nil {
			return nil, err
		}
	}

	if p.V {
		pos, err = p.parseSSData(packet, pos)
		if err != nil {
			return nil, err
		}
	}

	p.Payload = packet[pos:]
	return p.Payload, nil
}

// Picture ID:
//
//      +-+-+-+-+-+-+-+-+
// I:   |M| PICTURE ID  |   M:0 => picture id is 7 bits.
//      +-+-+-+-+-+-+-+-+   M:1 => picture id is 15 bits.
// M:   | EXTENDED PID  |
//      +-+-+-+-+-+-+-+-+
//
func (p *VP9Packet) parsePictureID(packet []byte, pos int) (int, error) {
	if len(packet) <= pos {
		return pos, errShortPacket
	}

	p.PictureID = uint16(packet[pos] & 0x7F)
	if packet[pos]&0x80 != 0 {
		pos++
		if len(packet) <= pos {
			return pos, errShortPacket
		}
		p.PictureID = p.PictureID<<8 | uint16(packet[pos])
	}
	pos++
	return pos, nil
}

func (p *VP9Packet) parseLayerInfo(packet []byte, pos int) (int, error) {
	pos, err := p.parseLayerInfoCommon(packet, pos)
	if err != nil {
		return pos, err
	}

	if p.F {
		return pos, nil
	}

	return p.parseLayerInfoNonFlexibleMode(packet, pos)
}

// Layer indices (flexible mode):
//
//      +-+-+-+-+-+-+-+-+
// L:   |  T  |U|  S  |D|
//      +-+-+-+-+-+-+-+-+
//
func (p *VP9Packet) parseLayerInfoCommon(packet []byte, pos int) (int, error) {
	if len(packet) <= pos {
		return pos, errShortPacket
	}

	p.TID = packet[pos] >> 5
	p.U = packet[pos]&0x10 != 0
	p.SID = (packet[pos] >> 1) & 0x7
	p.D = packet[pos]&0x01 != 0

	if p.SID >= maxSpatialLayers {
		return pos, errTooManySpatialLayers
	}

	pos++
	return pos, nil
}

// Layer indices (non-flexible mode):
//
//      +-+-+-+-+-+-+-+-+
// L:   |  T  |U|  S  |D|
//      +-+-+-+-+-+-+-+-+
//      |   TL0PICIDX   |
//      +-+-+-+-+-+-+-+-+
//
func (p *VP9Packet) parseLayerInfoNonFlexibleMode(packet []byte, pos int) (int, error) {
	if len(packet) <= pos {
		return pos, errShortPacket
	}

	p.TL0PICIDX = packet[pos]
	pos++
	return pos, nil
}

// Reference indices:
//
//      +-+-+-+-+-+-+-+-+                P=1,F=1: At least one reference index
// P,F: | P_DIFF      |N|  up to 3 times          has to be specified.
//      +-+-+-+-+-+-+-+-+                    N=1: An additional P_DIFF follows
//                                                current P_DIFF.
//
func (p *VP9Packet) parseRefIndices(packet []byte, pos int) (int, error) {
	for {
		if len(packet) <= pos {
			return pos, errShortPacket
		}
		p.PDiff = append(p.PDiff, packet[pos]>>1)
		if packet[pos]&0x01 == 0 {
			break
		}
		if len(p.PDiff) >= maxVP9RefPics {
			return pos, errTooManyPDiff
		}
		pos++
	}
	pos++

	return pos, nil
}

// Scalability structure (SS):
//
//      +-+-+-+-+-+-+-+-+
// V:   | N_S |Y|G|-|-|-|
//      +-+-+-+-+-+-+-+-+              -|
// Y:   |     WIDTH     | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+               . N_S + 1 times
//      |     HEIGHT    | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+              -|
// G:   |      N_G      | (OPTIONAL)
//      +-+-+-+-+-+-+-+-+                           -|
// N_G: |  T  |U| R |-|-| (OPTIONAL)                 .
//      +-+-+-+-+-+-+-+-+              -|            . N_G times
//      |    P_DIFF     | (OPTIONAL)    . R times    .
//      +-+-+-+-+-+-+-+-+              -|           -|
//
func (p *VP9Packet) parseSSData(packet []byte, pos int) (int, error) {
	if len(packet) <= pos {
		return pos, errShortPacket
	}

	p.NS = packet[pos] >> 5
	p.Y = packet[pos]&0x10 != 0
	p.G = (packet[pos]>>1)&0x7 != 0
	pos++

	NS := p.NS + 1
	p.NG = 0

	if p.Y {
		p.Width = make([]uint16, NS)
		p.Height = make([]uint16, NS)
		for i := 0; i < int(NS); i++ {
			p.Width[i] = uint16(packet[pos])<<8 | uint16(packet[pos+1])
			pos += 2
			p.Height[i] = uint16(packet[pos])<<8 | uint16(packet[pos+1])
			pos += 2
		}
	}

	if p.G {
		p.NG = packet[pos]
		pos++
	}

	for i := 0; i < int(p.NG); i++ {
		p.PGTID = append(p.PGTID, packet[pos]>>5)
		p.PGU = append(p.PGU, packet[pos]&0x10 != 0)
		R := (packet[pos] >> 2) & 0x3
		pos++

		p.PGPDiff = append(p.PGPDiff, []uint8{})
		for j := 0; j < int(R); j++ {
			p.PGPDiff[i] = append(p.PGPDiff[i], packet[pos])
			pos++
		}
	}

	return pos, nil
}

// VP9PartitionHeadChecker is obsolete
type VP9PartitionHeadChecker struct{}

// IsPartitionHead checks whether if this is a head of the VP9 partition
func (*VP9Packet) IsPartitionHead(payload []byte) bool {
	if len(payload) < 1 {
		return false
	}
	return (payload[0] & 0x08) != 0
}
