// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package codecs

import (
	"github.com/pion/rtp/codecs/av1/obu"
)

const (
	av1ZMask     = byte(0b10000000)
	av1ZBitshift = 7

	av1YMask     = byte(0b01000000)
	av1YBitshift = 6

	av1WMask     = byte(0b00110000)
	av1WBitshift = 4

	av1NMask     = byte(0b00001000)
	av1NBitshift = 3
)

// AV1Payloader payloads AV1 packets.
type AV1Payloader struct{}

// Payload implements AV1 RTP payloader.
// Reads from a open_bitstream_unit (OBU) framing stream as defined in
// 5.3. https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=39
// Returns AV1 RTP packets https://aomediacodec.github.io/av1-rtp-spec/
// The payload is fragmented into multiple packets, each packet is a valid AV1 RTP payload.
// nolint:cyclop
func (p *AV1Payloader) Payload(mtu uint16, payload []byte) (payloads [][]byte) {
	// 2 is the minimum MTU for AV1 (aggregate header + 1 byte)
	if mtu <= 1 || len(payload) == 0 {
		return payloads
	}

	// We maximize the use of the W field in the AV1 aggregation header
	// to minimize the need for explicit length fields for each OBU.
	// To achieve this, we temporarily hold the OBU payload before adding it to a packet.
	// Since we can't determine in advance whether the next OBU should be included in the same packet
	// or start a new one, we also can't know ahead of time if an OBU is the last in the current packet.
	var currentOBUPayload []byte
	var currentPacketOBUHeader *obu.ExtensionHeader
	obusInPacket := 0
	newSequence := false
	startWithNewPacket := false

	for offset := 0; offset < len(payload); {
		obuHeader, err := obu.ParseOBUHeader(payload[offset:])
		if err != nil {
			break
		}

		offset += obuHeader.Size()
		//  if ( obu_has_size_field ) {
		//    obu_size leb128()
		//  } else {
		//    obu_size = sz - 1 - obu_extension_flag
		//  }
		var obuSize int
		if obuHeader.HasSizeField {
			obuSizeValue, n, err := obu.ReadLeb128(payload[offset:])
			if err != nil {
				break
			}

			offset += int(n)            //nolint:gosec // G115, leb128 size is a signle digit
			obuSize = int(obuSizeValue) //nolint:gosec // G115, Leb128 is capped at 4 bytes
		} else {
			obuSize = len(payload) - offset
		}

		// Each RTP packet MUST NOT contain OBUs that belong to different temporal units.
		// If a sequence header OBU is present in an RTP packet, then it SHOULD be the first OBU in the packet.
		// https://aomediacodec.github.io/av1-rtp-spec/#5-packetization-rules
		needNewPacket := obuHeader.Type == obu.OBUTemporalDelimiter || obuHeader.Type == obu.OBUSequenceHeader
		// If more than one OBU contained in an RTP packet has an OBU extension header,
		// then the values of the temporal_id and spatial_id MUST be the same in all such OBUs in the RTP packet.
		if !needNewPacket && obuHeader.ExtensionHeader != nil && currentPacketOBUHeader != nil {
			needNewPacket = obuHeader.ExtensionHeader.SpatialID != currentPacketOBUHeader.SpatialID ||
				obuHeader.ExtensionHeader.TemporalID != currentPacketOBUHeader.TemporalID
		}

		if obuHeader.ExtensionHeader != nil {
			currentPacketOBUHeader = obuHeader.ExtensionHeader
		}

		if obuSize > len(payload)-offset {
			break
		}

		if len(currentOBUPayload) > 0 {
			payloads, obusInPacket = p.appendOBUPayload(
				payloads,
				currentOBUPayload,
				newSequence,
				needNewPacket,
				startWithNewPacket,
				int(mtu),
				obusInPacket,
			)
			currentOBUPayload = nil
			startWithNewPacket = needNewPacket

			if needNewPacket {
				newSequence = false
				currentPacketOBUHeader = nil
			}
		}

		// The temporal delimiter OBU, if present, SHOULD be removed when transmitting,
		// and MUST be ignored by receivers. Tile list OBUs are not supported.
		// They SHOULD be removed when transmitted, and MUST be ignored by receivers.
		// https://aomediacodec.github.io/av1-rtp-spec/#5-packetization-rules
		if obuHeader.Type == obu.OBUTileList || obuHeader.Type == obu.OBUTemporalDelimiter {
			offset += obuSize

			continue
		}

		currentOBUPayload = make([]byte, obuSize+obuHeader.Size())
		// The AV1 specification allows OBUs to have an optional size field called obu_size
		// (also leb128 encoded), signaled by the obu_has_size_field flag in the OBU header.
		// To minimize overhead, the obu_has_size_field flag SHOULD be set to zero in all OBUs.
		// https://aomediacodec.github.io/av1-rtp-spec/#45-payload-structure
		obuHeader.HasSizeField = false
		copy(currentOBUPayload, obuHeader.Marshal())
		//nolint:gosec // G115 we validate the size of the payload
		copy(currentOBUPayload[obuHeader.Size():], payload[offset:offset+obuSize])
		offset += obuSize
		newSequence = obuHeader.Type == obu.OBUSequenceHeader
	}

	if len(currentOBUPayload) > 0 {
		payloads, _ = p.appendOBUPayload(
			payloads,
			currentOBUPayload,
			newSequence,
			true,
			startWithNewPacket,
			int(mtu),
			obusInPacket,
		)
	}

	return payloads
}

//nolint:cyclop
func (p *AV1Payloader) appendOBUPayload(
	payloads [][]byte,
	obuPayload []byte,
	isNewVideoSequence, isLast, startWithNewPacket bool,
	mtu, currentOBUCount int,
) ([][]byte, int) {
	currentPayload := len(payloads) - 1
	freeSpace := 0
	if currentPayload >= 0 {
		freeSpace = mtu - len(payloads[currentPayload])
	}

	if currentPayload < 0 || freeSpace <= 0 || startWithNewPacket {
		payload := make([]byte, 1, mtu)
		if isNewVideoSequence {
			payload[0] |= 1 << av1NBitshift
		}

		payloads = append(payloads, payload)
		currentPayload = len(payloads) - 1
		// MTU - aggregation header
		freeSpace = mtu - 1
		currentOBUCount = 0
	}

	remaining := len(obuPayload)
	// How much to write to the current packet.
	toWrite := remaining
	if toWrite >= freeSpace {
		toWrite = freeSpace
	}

	// W: two bit field that describes the number of OBU elements in the packet.
	// This field MUST be set equal to 0 or equal to the number of OBU elements contained in the packet.
	// If set to 0, each OBU element MUST be preceded by a length field. If not set to 0 (i.e., W = 1, 2 or 3)
	// the last OBU element MUST NOT be preceded by a length field.
	// https://aomediacodec.github.io/av1-rtp-spec/#44-av1-aggregation-header
	shouldUseWField := (isLast || toWrite >= freeSpace) && currentOBUCount < 3
	switch {
	case shouldUseWField:
		payloads[currentPayload][0] |= byte((currentOBUCount+1)<<av1WBitshift) & av1WMask
		payloads[currentPayload] = append(payloads[currentPayload], obuPayload[:toWrite]...)
		currentOBUCount = 0
	case freeSpace >= 2:
		// 2 bytes is the minimum size for OBUs with length field.
		// [1 byte for the length field] [1 byte for the OBU]
		//nolint:gosec // G115 false positive
		toWrite = p.computeWriteSize(toWrite, freeSpace)
		lengthField := obu.WriteToLeb128(uint(toWrite)) //nolint:gosec // G115 false positive
		payloads[currentPayload] = append(payloads[currentPayload], lengthField...)
		payloads[currentPayload] = append(payloads[currentPayload], obuPayload[:toWrite]...)
		currentOBUCount++
	default:
		// If we can't fit any more OBUs in the current packet (only 1 byte left and W=0)
		toWrite = 0
	}

	obuPayload = obuPayload[toWrite:]
	remaining -= toWrite

	// Handle fragments.
	for remaining > 0 {
		// New packet with empty aggregation header.
		payload := make([]byte, 1, mtu)
		payloads = append(payloads, payload)
		currentPayload++

		// Append the Y bit to the previous packet. And Z bit to the current packet.
		// If we wrote some bytes to the previous packet.
		// Handles an edge case where the previous packet has only one byte remaining,
		// while the W field is not used. This results in insufficient space
		// for a one-byte length field and a one-byte OBU.
		// So we don't write anything to the initial packet.
		if toWrite != 0 {
			payloads[currentPayload-1][0] |= av1YMask
			payloads[currentPayload][0] |= av1ZMask
		}

		toWrite = remaining
		if toWrite >= mtu-1 { // MTU - aggregation header
			toWrite = mtu - 1
		}

		// Last OBU in the current packet, Or this whole packet is a fragment.
		if isLast || remaining >= mtu-1 {
			payloads[currentPayload][0] |= 1 << av1WBitshift
		} else {
			toWrite = p.computeWriteSize(toWrite, mtu-1)
			lengthField := obu.WriteToLeb128(uint(toWrite)) //nolint:gosec // G115 false positive
			payloads[currentPayload] = append(payloads[currentPayload], lengthField...)
		}

		payloads[currentPayload] = append(payloads[currentPayload], obuPayload[:toWrite]...)
		obuPayload = obuPayload[toWrite:]
		remaining -= toWrite
		currentOBUCount = 1
	}

	return payloads, currentOBUCount
}

// Measure the maximum write size for a payload with leb128 encoding added.
func (p *AV1Payloader) computeWriteSize(wantToWrite, canWrite int) int {
	leb128Size, isAtEge := p.leb128Size(wantToWrite)
	if canWrite >= wantToWrite+leb128Size {
		return wantToWrite
	}

	// Handle edge case where subtracting one from the leb128 size
	// results in a smaller leb128 size that can fit in the remaining space.
	if isAtEge && canWrite >= wantToWrite+leb128Size-1 {
		return wantToWrite - 1
	}

	return wantToWrite - leb128Size
}

func (p *AV1Payloader) leb128Size(leb128 int) (size int, isAtEge bool) {
	switch {
	case leb128 >= 268435456: // 2^28
		return 5, leb128 == 268435456
	case leb128 >= 2097152: // 2^21
		return 4, leb128 == 2097152
	case leb128 >= 16384: // 2^14
		return 3, leb128 == 16384
	case leb128 >= 128: // 2^7
		return 2, leb128 == 128
	default:
		return 1, false
	}
}

// AV1Packet represents a depacketized AV1 RTP Packet
/*
*  0 1 2 3 4 5 6 7
* +-+-+-+-+-+-+-+-+
* |Z|Y| W |N|-|-|-|
* +-+-+-+-+-+-+-+-+
**/
// https://aomediacodec.github.io/av1-rtp-spec/#44-av1-aggregation-header
// Deprecated: Use AV1Depacketizer instead.
type AV1Packet struct {
	// Z: MUST be set to 1 if the first OBU element is an
	//    OBU fragment that is a continuation of an OBU fragment
	//    from the previous packet, and MUST be set to 0 otherwise.
	Z bool

	// Y: MUST be set to 1 if the last OBU element is an OBU fragment
	//    that will continue in the next packet, and MUST be set to 0 otherwise.
	Y bool

	// W: two bit field that describes the number of OBU elements in the packet.
	//    This field MUST be set equal to 0 or equal to the number of OBU elements
	//    contained in the packet. If set to 0, each OBU element MUST be preceded by
	//    a length field. If not set to 0 (i.e., W = 1, 2 or 3) the last OBU element
	//    MUST NOT be preceded by a length field. Instead, the length of the last OBU
	//    element contained in the packet can be calculated as follows:
	// Length of the last OBU element =
	//    length of the RTP payload
	//  - length of aggregation header
	//  - length of previous OBU elements including length fields
	W byte

	// N: MUST be set to 1 if the packet is the first packet of a coded video sequence, and MUST be set to 0 otherwise.
	N bool

	// Each AV1 RTP Packet is a collection of OBU Elements. Each OBU Element may be a full OBU, or just a fragment of one.
	// AV1Frame provides the tools to construct a collection of OBUs from a collection of OBU Elements
	OBUElements [][]byte

	// zeroAllocation prevents populating the OBUElements field
	zeroAllocation bool
}

// Unmarshal parses the passed byte slice and stores the result in the AV1Packet this method is called upon.
func (p *AV1Packet) Unmarshal(payload []byte) ([]byte, error) {
	if payload == nil {
		return nil, errNilPacket
	} else if len(payload) < 2 {
		return nil, errShortPacket
	}

	p.Z = ((payload[0] & av1ZMask) >> av1ZBitshift) != 0
	p.Y = ((payload[0] & av1YMask) >> av1YBitshift) != 0
	p.N = ((payload[0] & av1NMask) >> av1NBitshift) != 0
	p.W = (payload[0] & av1WMask) >> av1WBitshift

	if p.Z && p.N {
		return nil, errIsKeyframeAndFragment
	}

	if !p.zeroAllocation {
		obuElements, err := p.parseBody(payload[1:])
		if err != nil {
			return nil, err
		}
		p.OBUElements = obuElements
	}

	return payload[1:], nil
}

func (p *AV1Packet) parseBody(payload []byte) ([][]byte, error) {
	if p.OBUElements != nil {
		return p.OBUElements, nil
	}

	obuElements := [][]byte{}

	var obuElementLength, bytesRead uint
	currentIndex := uint(0)
	for i := 1; ; i++ {
		if currentIndex == uint(len(payload)) {
			break
		}

		// If W bit is set the last OBU Element will have no length header
		if byte(i) == p.W {
			bytesRead = 0
			obuElementLength = uint(len(payload)) - currentIndex
		} else {
			var err error
			obuElementLength, bytesRead, err = obu.ReadLeb128(payload[currentIndex:])
			if err != nil {
				return nil, err
			}
		}

		currentIndex += bytesRead
		if uint(len(payload)) < currentIndex+obuElementLength {
			return nil, errShortPacket
		}
		obuElements = append(obuElements, payload[currentIndex:currentIndex+obuElementLength])
		currentIndex += obuElementLength
	}

	return obuElements, nil
}
