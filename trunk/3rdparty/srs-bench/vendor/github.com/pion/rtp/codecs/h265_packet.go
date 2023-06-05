package codecs

import (
	"encoding/binary"
	"errors"
	"fmt"
)

//
// Errors
//

var (
	errH265CorruptedPacket   = errors.New("corrupted h265 packet")
	errInvalidH265PacketType = errors.New("invalid h265 packet type")
)

//
// Network Abstraction Unit Header implementation
//

const (
	// sizeof(uint16)
	h265NaluHeaderSize = 2
	// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
	h265NaluAggregationPacketType = 48
	// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
	h265NaluFragmentationUnitType = 49
	// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.4
	h265NaluPACIPacketType = 50
)

// H265NALUHeader is a H265 NAL Unit Header
// https://datatracker.ietf.org/doc/html/rfc7798#section-1.1.4
// +---------------+---------------+
//  |0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |F|   Type    |  LayerID  | TID |
//  +-------------+-----------------+
type H265NALUHeader uint16

func newH265NALUHeader(highByte, lowByte uint8) H265NALUHeader {
	return H265NALUHeader((uint16(highByte) << 8) | uint16(lowByte))
}

// F is the forbidden bit, should always be 0.
func (h H265NALUHeader) F() bool {
	return (uint16(h) >> 15) != 0
}

// Type of NAL Unit.
func (h H265NALUHeader) Type() uint8 {
	// 01111110 00000000
	const mask = 0b01111110 << 8
	return uint8((uint16(h) & mask) >> (8 + 1))
}

// IsTypeVCLUnit returns whether or not the NAL Unit type is a VCL NAL unit.
func (h H265NALUHeader) IsTypeVCLUnit() bool {
	// Type is coded on 6 bits
	const msbMask = 0b00100000
	return (h.Type() & msbMask) == 0
}

// LayerID should always be 0 in non-3D HEVC context.
func (h H265NALUHeader) LayerID() uint8 {
	// 00000001 11111000
	const mask = (0b00000001 << 8) | 0b11111000
	return uint8((uint16(h) & mask) >> 3)
}

// TID is the temporal identifier of the NAL unit +1.
func (h H265NALUHeader) TID() uint8 {
	const mask = 0b00000111
	return uint8(uint16(h) & mask)
}

// IsAggregationPacket returns whether or not the packet is an Aggregation packet.
func (h H265NALUHeader) IsAggregationPacket() bool {
	return h.Type() == h265NaluAggregationPacketType
}

// IsFragmentationUnit returns whether or not the packet is a Fragmentation Unit packet.
func (h H265NALUHeader) IsFragmentationUnit() bool {
	return h.Type() == h265NaluFragmentationUnitType
}

// IsPACIPacket returns whether or not the packet is a PACI packet.
func (h H265NALUHeader) IsPACIPacket() bool {
	return h.Type() == h265NaluPACIPacketType
}

//
// Single NAL Unit Packet implementation
//

// H265SingleNALUnitPacket represents a NALU packet, containing exactly one NAL unit.
//     0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |           PayloadHdr          |      DONL (conditional)       |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                                                               |
//   |                  NAL unit payload data                        |
//   |                                                               |
//   |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                               :...OPTIONAL RTP padding        |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Reference: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.1
type H265SingleNALUnitPacket struct {
	// payloadHeader is the header of the H265 packet.
	payloadHeader H265NALUHeader
	// donl is a 16-bit field, that may or may not be present.
	donl *uint16
	// payload of the fragmentation unit.
	payload []byte

	mightNeedDONL bool
}

// WithDONL can be called to specify whether or not DONL might be parsed.
// DONL may need to be parsed if `sprop-max-don-diff` is greater than 0 on the RTP stream.
func (p *H265SingleNALUnitPacket) WithDONL(value bool) {
	p.mightNeedDONL = value
}

// Unmarshal parses the passed byte slice and stores the result in the H265SingleNALUnitPacket this method is called upon.
func (p *H265SingleNALUnitPacket) Unmarshal(payload []byte) ([]byte, error) {
	// sizeof(headers)
	const totalHeaderSize = h265NaluHeaderSize
	if payload == nil {
		return nil, errNilPacket
	} else if len(payload) <= totalHeaderSize {
		return nil, fmt.Errorf("%w: %d <= %v", errShortPacket, len(payload), totalHeaderSize)
	}

	payloadHeader := newH265NALUHeader(payload[0], payload[1])
	if payloadHeader.F() {
		return nil, errH265CorruptedPacket
	}
	if payloadHeader.IsFragmentationUnit() || payloadHeader.IsPACIPacket() || payloadHeader.IsAggregationPacket() {
		return nil, errInvalidH265PacketType
	}

	payload = payload[2:]

	if p.mightNeedDONL {
		// sizeof(uint16)
		if len(payload) <= 2 {
			return nil, errShortPacket
		}

		donl := (uint16(payload[0]) << 8) | uint16(payload[1])
		p.donl = &donl
		payload = payload[2:]
	}

	p.payloadHeader = payloadHeader
	p.payload = payload

	return nil, nil
}

// PayloadHeader returns the NALU header of the packet.
func (p *H265SingleNALUnitPacket) PayloadHeader() H265NALUHeader {
	return p.payloadHeader
}

// DONL returns the DONL of the packet.
func (p *H265SingleNALUnitPacket) DONL() *uint16 {
	return p.donl
}

// Payload returns the Fragmentation Unit packet payload.
func (p *H265SingleNALUnitPacket) Payload() []byte {
	return p.payload
}

func (p *H265SingleNALUnitPacket) isH265Packet() {}

//
// Aggregation Packets implementation
//

// H265AggregationUnitFirst represent the First Aggregation Unit in an AP.
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//                   :       DONL (conditional)      |   NALU size   |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |   NALU size   |                                               |
//   +-+-+-+-+-+-+-+-+         NAL unit                              |
//   |                                                               |
//   |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                               :
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Reference: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
type H265AggregationUnitFirst struct {
	donl        *uint16
	nalUnitSize uint16
	nalUnit     []byte
}

// DONL field, when present, specifies the value of the 16 least
// significant bits of the decoding order number of the aggregated NAL
// unit.
func (u H265AggregationUnitFirst) DONL() *uint16 {
	return u.donl
}

// NALUSize represents the size, in bytes, of the NalUnit.
func (u H265AggregationUnitFirst) NALUSize() uint16 {
	return u.nalUnitSize
}

// NalUnit payload.
func (u H265AggregationUnitFirst) NalUnit() []byte {
	return u.nalUnit
}

// H265AggregationUnit represent the an Aggregation Unit in an AP, which is not the first one.
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//                   : DOND (cond)   |          NALU size            |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                                                               |
//   |                       NAL unit                                |
//   |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                               :
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Reference: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
type H265AggregationUnit struct {
	dond        *uint8
	nalUnitSize uint16
	nalUnit     []byte
}

// DOND field plus 1 specifies the difference between
// the decoding order number values of the current aggregated NAL unit
// and the preceding aggregated NAL unit in the same AP.
func (u H265AggregationUnit) DOND() *uint8 {
	return u.dond
}

// NALUSize represents the size, in bytes, of the NalUnit.
func (u H265AggregationUnit) NALUSize() uint16 {
	return u.nalUnitSize
}

// NalUnit payload.
func (u H265AggregationUnit) NalUnit() []byte {
	return u.nalUnit
}

// H265AggregationPacket represents an Aggregation packet.
//   0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |    PayloadHdr (Type=48)       |                               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
//   |                                                               |
//   |             two or more aggregation units                     |
//   |                                                               |
//   |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                               :...OPTIONAL RTP padding        |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Reference: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
type H265AggregationPacket struct {
	firstUnit  *H265AggregationUnitFirst
	otherUnits []H265AggregationUnit

	mightNeedDONL bool
}

// WithDONL can be called to specify whether or not DONL might be parsed.
// DONL may need to be parsed if `sprop-max-don-diff` is greater than 0 on the RTP stream.
func (p *H265AggregationPacket) WithDONL(value bool) {
	p.mightNeedDONL = value
}

// Unmarshal parses the passed byte slice and stores the result in the H265AggregationPacket this method is called upon.
func (p *H265AggregationPacket) Unmarshal(payload []byte) ([]byte, error) {
	// sizeof(headers)
	const totalHeaderSize = h265NaluHeaderSize
	if payload == nil {
		return nil, errNilPacket
	} else if len(payload) <= totalHeaderSize {
		return nil, fmt.Errorf("%w: %d <= %v", errShortPacket, len(payload), totalHeaderSize)
	}

	payloadHeader := newH265NALUHeader(payload[0], payload[1])
	if payloadHeader.F() {
		return nil, errH265CorruptedPacket
	}
	if !payloadHeader.IsAggregationPacket() {
		return nil, errInvalidH265PacketType
	}

	// First parse the first aggregation unit
	payload = payload[2:]
	firstUnit := &H265AggregationUnitFirst{}

	if p.mightNeedDONL {
		if len(payload) < 2 {
			return nil, errShortPacket
		}

		donl := (uint16(payload[0]) << 8) | uint16(payload[1])
		firstUnit.donl = &donl

		payload = payload[2:]
	}
	if len(payload) < 2 {
		return nil, errShortPacket
	}
	firstUnit.nalUnitSize = (uint16(payload[0]) << 8) | uint16(payload[1])
	payload = payload[2:]

	if len(payload) < int(firstUnit.nalUnitSize) {
		return nil, errShortPacket
	}

	firstUnit.nalUnit = payload[:firstUnit.nalUnitSize]
	payload = payload[firstUnit.nalUnitSize:]

	// Parse remaining Aggregation Units
	var units []H265AggregationUnit
	for {
		unit := H265AggregationUnit{}

		if p.mightNeedDONL {
			if len(payload) < 1 {
				break
			}

			dond := payload[0]
			unit.dond = &dond

			payload = payload[1:]
		}

		if len(payload) < 2 {
			break
		}
		unit.nalUnitSize = (uint16(payload[0]) << 8) | uint16(payload[1])
		payload = payload[2:]

		if len(payload) < int(unit.nalUnitSize) {
			break
		}

		unit.nalUnit = payload[:unit.nalUnitSize]
		payload = payload[unit.nalUnitSize:]

		units = append(units, unit)
	}

	// There need to be **at least** two Aggregation Units (first + another one)
	if len(units) == 0 {
		return nil, errShortPacket
	}

	p.firstUnit = firstUnit
	p.otherUnits = units

	return nil, nil
}

// FirstUnit returns the first Aggregated Unit of the packet.
func (p *H265AggregationPacket) FirstUnit() *H265AggregationUnitFirst {
	return p.firstUnit
}

// OtherUnits returns the all the other Aggregated Unit of the packet (excluding the first one).
func (p *H265AggregationPacket) OtherUnits() []H265AggregationUnit {
	return p.otherUnits
}

func (p *H265AggregationPacket) isH265Packet() {}

//
// Fragmentation Unit implementation
//

const (
	// sizeof(uint8)
	h265FragmentationUnitHeaderSize = 1
)

// H265FragmentationUnitHeader is a H265 FU Header
// +---------------+
// |0|1|2|3|4|5|6|7|
// +-+-+-+-+-+-+-+-+
// |S|E|  FuType   |
// +---------------+
type H265FragmentationUnitHeader uint8

// S represents the start of a fragmented NAL unit.
func (h H265FragmentationUnitHeader) S() bool {
	const mask = 0b10000000
	return ((h & mask) >> 7) != 0
}

// E represents the end of a fragmented NAL unit.
func (h H265FragmentationUnitHeader) E() bool {
	const mask = 0b01000000
	return ((h & mask) >> 6) != 0
}

// FuType MUST be equal to the field Type of the fragmented NAL unit.
func (h H265FragmentationUnitHeader) FuType() uint8 {
	const mask = 0b00111111
	return uint8(h) & mask
}

// H265FragmentationUnitPacket represents a single Fragmentation Unit packet.
//
//  0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |    PayloadHdr (Type=49)       |   FU header   | DONL (cond)   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
// | DONL (cond)   |                                               |
// |-+-+-+-+-+-+-+-+                                               |
// |                         FU payload                            |
// |                                                               |
// |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                               :...OPTIONAL RTP padding        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Reference: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
type H265FragmentationUnitPacket struct {
	// payloadHeader is the header of the H265 packet.
	payloadHeader H265NALUHeader
	// fuHeader is the header of the fragmentation unit
	fuHeader H265FragmentationUnitHeader
	// donl is a 16-bit field, that may or may not be present.
	donl *uint16
	// payload of the fragmentation unit.
	payload []byte

	mightNeedDONL bool
}

// WithDONL can be called to specify whether or not DONL might be parsed.
// DONL may need to be parsed if `sprop-max-don-diff` is greater than 0 on the RTP stream.
func (p *H265FragmentationUnitPacket) WithDONL(value bool) {
	p.mightNeedDONL = value
}

// Unmarshal parses the passed byte slice and stores the result in the H265FragmentationUnitPacket this method is called upon.
func (p *H265FragmentationUnitPacket) Unmarshal(payload []byte) ([]byte, error) {
	// sizeof(headers)
	const totalHeaderSize = h265NaluHeaderSize + h265FragmentationUnitHeaderSize
	if payload == nil {
		return nil, errNilPacket
	} else if len(payload) <= totalHeaderSize {
		return nil, fmt.Errorf("%w: %d <= %v", errShortPacket, len(payload), totalHeaderSize)
	}

	payloadHeader := newH265NALUHeader(payload[0], payload[1])
	if payloadHeader.F() {
		return nil, errH265CorruptedPacket
	}
	if !payloadHeader.IsFragmentationUnit() {
		return nil, errInvalidH265PacketType
	}

	fuHeader := H265FragmentationUnitHeader(payload[2])
	payload = payload[3:]

	if fuHeader.S() && p.mightNeedDONL {
		// sizeof(uint16)
		if len(payload) <= 2 {
			return nil, errShortPacket
		}

		donl := (uint16(payload[0]) << 8) | uint16(payload[1])
		p.donl = &donl
		payload = payload[2:]
	}

	p.payloadHeader = payloadHeader
	p.fuHeader = fuHeader
	p.payload = payload

	return nil, nil
}

// PayloadHeader returns the NALU header of the packet.
func (p *H265FragmentationUnitPacket) PayloadHeader() H265NALUHeader {
	return p.payloadHeader
}

// FuHeader returns the Fragmentation Unit Header of the packet.
func (p *H265FragmentationUnitPacket) FuHeader() H265FragmentationUnitHeader {
	return p.fuHeader
}

// DONL returns the DONL of the packet.
func (p *H265FragmentationUnitPacket) DONL() *uint16 {
	return p.donl
}

// Payload returns the Fragmentation Unit packet payload.
func (p *H265FragmentationUnitPacket) Payload() []byte {
	return p.payload
}

func (p *H265FragmentationUnitPacket) isH265Packet() {}

//
// PACI implementation
//

// H265PACIPacket represents a single H265 PACI packet.
//
//  0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |    PayloadHdr (Type=50)       |A|   cType   | PHSsize |F0..2|Y|
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |        Payload Header Extension Structure (PHES)              |
// |=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=|
// |                                                               |
// |                  PACI payload: NAL unit                       |
// |                   . . .                                       |
// |                                                               |
// |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                               :...OPTIONAL RTP padding        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Reference: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.4
type H265PACIPacket struct {
	// payloadHeader is the header of the H265 packet.
	payloadHeader H265NALUHeader

	// Field which holds value for `A`, `cType`, `PHSsize`, `F0`, `F1`, `F2` and `Y` fields.
	paciHeaderFields uint16

	// phes is a header extension, of byte length `PHSsize`
	phes []byte

	// Payload contains NAL units & optional padding
	payload []byte
}

// PayloadHeader returns the NAL Unit Header.
func (p *H265PACIPacket) PayloadHeader() H265NALUHeader {
	return p.payloadHeader
}

// A copies the F bit of the PACI payload NALU.
func (p *H265PACIPacket) A() bool {
	const mask = 0b10000000 << 8
	return (p.paciHeaderFields & mask) != 0
}

// CType copies the Type field of the PACI payload NALU.
func (p *H265PACIPacket) CType() uint8 {
	const mask = 0b01111110 << 8
	return uint8((p.paciHeaderFields & mask) >> (8 + 1))
}

// PHSsize indicates the size of the PHES field.
func (p *H265PACIPacket) PHSsize() uint8 {
	const mask = (0b00000001 << 8) | 0b11110000
	return uint8((p.paciHeaderFields & mask) >> 4)
}

// F0 indicates the presence of a Temporal Scalability support extension in the PHES.
func (p *H265PACIPacket) F0() bool {
	const mask = 0b00001000
	return (p.paciHeaderFields & mask) != 0
}

// F1 must be zero, reserved for future extensions.
func (p *H265PACIPacket) F1() bool {
	const mask = 0b00000100
	return (p.paciHeaderFields & mask) != 0
}

// F2 must be zero, reserved for future extensions.
func (p *H265PACIPacket) F2() bool {
	const mask = 0b00000010
	return (p.paciHeaderFields & mask) != 0
}

// Y must be zero, reserved for future extensions.
func (p *H265PACIPacket) Y() bool {
	const mask = 0b00000001
	return (p.paciHeaderFields & mask) != 0
}

// PHES contains header extensions. Its size is indicated by PHSsize.
func (p *H265PACIPacket) PHES() []byte {
	return p.phes
}

// Payload is a single NALU or NALU-like struct, not including the first two octets (header).
func (p *H265PACIPacket) Payload() []byte {
	return p.payload
}

// TSCI returns the Temporal Scalability Control Information extension, if present.
func (p *H265PACIPacket) TSCI() *H265TSCI {
	if !p.F0() || p.PHSsize() < 3 {
		return nil
	}

	tsci := H265TSCI((uint32(p.phes[0]) << 16) | (uint32(p.phes[1]) << 8) | uint32(p.phes[0]))
	return &tsci
}

// Unmarshal parses the passed byte slice and stores the result in the H265PACIPacket this method is called upon.
func (p *H265PACIPacket) Unmarshal(payload []byte) ([]byte, error) {
	// sizeof(headers)
	const totalHeaderSize = h265NaluHeaderSize + 2
	if payload == nil {
		return nil, errNilPacket
	} else if len(payload) <= totalHeaderSize {
		return nil, fmt.Errorf("%w: %d <= %v", errShortPacket, len(payload), totalHeaderSize)
	}

	payloadHeader := newH265NALUHeader(payload[0], payload[1])
	if payloadHeader.F() {
		return nil, errH265CorruptedPacket
	}
	if !payloadHeader.IsPACIPacket() {
		return nil, errInvalidH265PacketType
	}

	paciHeaderFields := (uint16(payload[2]) << 8) | uint16(payload[3])
	payload = payload[4:]

	p.paciHeaderFields = paciHeaderFields
	headerExtensionSize := p.PHSsize()

	if len(payload) < int(headerExtensionSize)+1 {
		p.paciHeaderFields = 0
		return nil, errShortPacket
	}

	p.payloadHeader = payloadHeader

	if headerExtensionSize > 0 {
		p.phes = payload[:headerExtensionSize]
	}

	payload = payload[headerExtensionSize:]
	p.payload = payload

	return nil, nil
}

func (p *H265PACIPacket) isH265Packet() {}

//
// Temporal Scalability Control Information
//

// H265TSCI is a Temporal Scalability Control Information header extension.
// Reference: https://datatracker.ietf.org/doc/html/rfc7798#section-4.5
type H265TSCI uint32

// TL0PICIDX see RFC7798 for more details.
func (h H265TSCI) TL0PICIDX() uint8 {
	const m1 = 0xFFFF0000
	const m2 = 0xFF00
	return uint8((((h & m1) >> 16) & m2) >> 8)
}

// IrapPicID see RFC7798 for more details.
func (h H265TSCI) IrapPicID() uint8 {
	const m1 = 0xFFFF0000
	const m2 = 0x00FF
	return uint8(((h & m1) >> 16) & m2)
}

// S see RFC7798 for more details.
func (h H265TSCI) S() bool {
	const m1 = 0xFF00
	const m2 = 0b10000000
	return (uint8((h&m1)>>8) & m2) != 0
}

// E see RFC7798 for more details.
func (h H265TSCI) E() bool {
	const m1 = 0xFF00
	const m2 = 0b01000000
	return (uint8((h&m1)>>8) & m2) != 0
}

// RES see RFC7798 for more details.
func (h H265TSCI) RES() uint8 {
	const m1 = 0xFF00
	const m2 = 0b00111111
	return uint8((h&m1)>>8) & m2
}

//
// H265 Packet interface
//

type isH265Packet interface {
	isH265Packet()
}

var (
	_ isH265Packet = (*H265FragmentationUnitPacket)(nil)
	_ isH265Packet = (*H265PACIPacket)(nil)
	_ isH265Packet = (*H265SingleNALUnitPacket)(nil)
	_ isH265Packet = (*H265AggregationPacket)(nil)
)

//
// Packet implementation
//

// H265Packet represents a H265 packet, stored in the payload of an RTP packet.
type H265Packet struct {
	packet        isH265Packet
	mightNeedDONL bool

	videoDepacketizer
}

// WithDONL can be called to specify whether or not DONL might be parsed.
// DONL may need to be parsed if `sprop-max-don-diff` is greater than 0 on the RTP stream.
func (p *H265Packet) WithDONL(value bool) {
	p.mightNeedDONL = value
}

// Unmarshal parses the passed byte slice and stores the result in the H265Packet this method is called upon
func (p *H265Packet) Unmarshal(payload []byte) ([]byte, error) {
	if payload == nil {
		return nil, errNilPacket
	} else if len(payload) <= h265NaluHeaderSize {
		return nil, fmt.Errorf("%w: %d <= %v", errShortPacket, len(payload), h265NaluHeaderSize)
	}

	payloadHeader := newH265NALUHeader(payload[0], payload[1])
	if payloadHeader.F() {
		return nil, errH265CorruptedPacket
	}

	switch {
	case payloadHeader.IsPACIPacket():
		decoded := &H265PACIPacket{}
		if _, err := decoded.Unmarshal(payload); err != nil {
			return nil, err
		}

		p.packet = decoded

	case payloadHeader.IsFragmentationUnit():
		decoded := &H265FragmentationUnitPacket{}
		decoded.WithDONL(p.mightNeedDONL)

		if _, err := decoded.Unmarshal(payload); err != nil {
			return nil, err
		}

		p.packet = decoded

	case payloadHeader.IsAggregationPacket():
		decoded := &H265AggregationPacket{}
		decoded.WithDONL(p.mightNeedDONL)

		if _, err := decoded.Unmarshal(payload); err != nil {
			return nil, err
		}

		p.packet = decoded

	default:
		decoded := &H265SingleNALUnitPacket{}
		decoded.WithDONL(p.mightNeedDONL)

		if _, err := decoded.Unmarshal(payload); err != nil {
			return nil, err
		}

		p.packet = decoded
	}

	return nil, nil
}

// Packet returns the populated packet.
// Must be casted to one of:
// - *H265SingleNALUnitPacket
// - *H265FragmentationUnitPacket
// - *H265AggregationPacket
// - *H265PACIPacket
// nolint:golint
func (p *H265Packet) Packet() isH265Packet {
	return p.packet
}

// IsPartitionHead checks if this is the head of a packetized nalu stream.
func (*H265Packet) IsPartitionHead(payload []byte) bool {
	if len(payload) < 3 {
		return false
	}

	if H265NALUHeader(binary.BigEndian.Uint16(payload[0:2])).Type() == h265NaluFragmentationUnitType {
		return H265FragmentationUnitHeader(payload[2]).S()
	}

	return true
}
