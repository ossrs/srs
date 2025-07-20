// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package codecs

import (
	"bytes"
	"encoding/binary"
	"fmt"
)

// H264Payloader payloads H264 packets.
type H264Payloader struct {
	spsNalu, ppsNalu []byte
	DisableStapA     bool
}

const (
	stapaNALUType  = 24
	fuaNALUType    = 28
	fubNALUType    = 29
	spsNALUType    = 7
	ppsNALUType    = 8
	audNALUType    = 9
	fillerNALUType = 12

	fuaHeaderSize       = 2
	stapaHeaderSize     = 1
	stapaNALULengthSize = 2

	naluTypeBitmask   = 0x1F
	naluRefIdcBitmask = 0x60
	fuStartBitmask    = 0x80
	fuEndBitmask      = 0x40

	outputStapAHeader = 0x78
)

// nolint:gochecknoglobals
var (
	naluStartCode       = []byte{0x00, 0x00, 0x01}
	annexbNALUStartCode = []byte{0x00, 0x00, 0x00, 0x01}
)

func emitNalus(nals []byte, emit func([]byte)) {
	// look for 3-byte NALU start code
	start := bytes.Index(nals, naluStartCode)
	offset := 3

	if start == -1 {
		// no start code, emit the whole buffer
		emit(nals)

		return
	}

	length := len(nals)

	for start < length {
		// look for the next NALU start (end of this NALU)
		end := bytes.Index(nals[start+offset:], naluStartCode)
		if end == -1 {
			// no more NALUs, emit the rest of the buffer
			emit(nals[start+offset:])

			break
		}

		// next NALU start
		nextStart := start + offset + end

		// check if the next NALU is actually a 4-byte start code
		endIs4Byte := nals[nextStart-1] == 0
		if endIs4Byte {
			nextStart--
		}

		emit(nals[start+offset : nextStart])

		start = nextStart

		if endIs4Byte {
			offset = 4
		} else {
			offset = 3
		}
	}
}

// Payload fragments a H264 packet across one or more byte arrays.
func (p *H264Payloader) Payload(mtu uint16, payload []byte) [][]byte { //nolint:cyclop
	var payloads [][]byte
	if len(payload) == 0 {
		return payloads
	}

	emitNalus(payload, func(nalu []byte) {
		if len(nalu) == 0 {
			return
		}

		naluType := nalu[0] & naluTypeBitmask
		naluRefIdc := nalu[0] & naluRefIdcBitmask

		switch {
		case naluType == audNALUType || naluType == fillerNALUType:
			return
		case naluType == spsNALUType:
			if !p.DisableStapA {
				p.spsNalu = nalu

				return
			}
		case naluType == ppsNALUType:
			if !p.DisableStapA {
				p.ppsNalu = nalu

				return
			}
		case !p.DisableStapA && p.spsNalu != nil && p.ppsNalu != nil:
			// Pack current NALU with SPS and PPS as STAP-A
			spsLen := make([]byte, 2)
			binary.BigEndian.PutUint16(spsLen, uint16(len(p.spsNalu))) // nolint: gosec // G115

			ppsLen := make([]byte, 2)
			binary.BigEndian.PutUint16(ppsLen, uint16(len(p.ppsNalu))) // nolint: gosec // G115

			stapANalu := []byte{outputStapAHeader}
			stapANalu = append(stapANalu, spsLen...)
			stapANalu = append(stapANalu, p.spsNalu...)
			stapANalu = append(stapANalu, ppsLen...)
			stapANalu = append(stapANalu, p.ppsNalu...)
			if len(stapANalu) <= int(mtu) {
				out := make([]byte, len(stapANalu))
				copy(out, stapANalu)
				payloads = append(payloads, out)
			}

			p.spsNalu = nil
			p.ppsNalu = nil
		}

		// Single NALU
		if len(nalu) <= int(mtu) {
			out := make([]byte, len(nalu))
			copy(out, nalu)
			payloads = append(payloads, out)

			return
		}

		// FU-A
		maxFragmentSize := int(mtu) - fuaHeaderSize

		// The FU payload consists of fragments of the payload of the fragmented
		// NAL unit so that if the fragmentation unit payloads of consecutive
		// FUs are sequentially concatenated, the payload of the fragmented NAL
		// unit can be reconstructed.  The NAL unit type octet of the fragmented
		// NAL unit is not included as such in the fragmentation unit payload,
		// 	but rather the information of the NAL unit type octet of the
		// fragmented NAL unit is conveyed in the F and NRI fields of the FU
		// indicator octet of the fragmentation unit and in the type field of
		// the FU header.  An FU payload MAY have any number of octets and MAY
		// be empty.

		// According to the RFC, the first octet is skipped due to redundant information
		naluIndex := 1
		naluLength := len(nalu) - naluIndex
		naluRemaining := naluLength

		if minInt(maxFragmentSize, naluRemaining) <= 0 {
			return
		}

		for naluRemaining > 0 {
			currentFragmentSize := minInt(maxFragmentSize, naluRemaining)
			out := make([]byte, fuaHeaderSize+currentFragmentSize)

			// +---------------+
			// |0|1|2|3|4|5|6|7|
			// +-+-+-+-+-+-+-+-+
			// |F|NRI|  Type   |
			// +---------------+
			out[0] = fuaNALUType
			out[0] |= naluRefIdc

			// +---------------+
			// |0|1|2|3|4|5|6|7|
			// +-+-+-+-+-+-+-+-+
			// |S|E|R|  Type   |
			// +---------------+

			out[1] = naluType
			if naluRemaining == naluLength {
				// Set start bit
				out[1] |= 1 << 7
			} else if naluRemaining-currentFragmentSize == 0 {
				// Set end bit
				out[1] |= 1 << 6
			}

			copy(out[fuaHeaderSize:], nalu[naluIndex:naluIndex+currentFragmentSize])
			payloads = append(payloads, out)

			naluRemaining -= currentFragmentSize
			naluIndex += currentFragmentSize
		}
	})

	return payloads
}

// H264Packet represents the H264 header that is stored in the payload of an RTP Packet.
type H264Packet struct {
	IsAVC     bool
	fuaBuffer []byte

	videoDepacketizer
}

func (p *H264Packet) doPackaging(buf, nalu []byte) []byte {
	if p.IsAVC {
		buf = binary.BigEndian.AppendUint32(buf, uint32(len(nalu))) // nolint: gosec // G115 false positive
		buf = append(buf, nalu...)

		return buf
	}

	buf = append(buf, annexbNALUStartCode...)
	buf = append(buf, nalu...)

	return buf
}

// IsDetectedFinalPacketInSequence returns true of the packet passed in has the
// marker bit set indicated the end of a packet sequence.
func (p *H264Packet) IsDetectedFinalPacketInSequence(rtpPacketMarketBit bool) bool {
	return rtpPacketMarketBit
}

// Unmarshal parses the passed byte slice and stores the result in the H264Packet this method is called upon.
func (p *H264Packet) Unmarshal(payload []byte) ([]byte, error) {
	if p.zeroAllocation {
		return payload, nil
	}

	return p.parseBody(payload)
}

func (p *H264Packet) parseBody(payload []byte) ([]byte, error) { //nolint:cyclop
	if len(payload) == 0 {
		return nil, fmt.Errorf("%w: %d <=0", errShortPacket, len(payload))
	}

	// NALU Types
	// https://tools.ietf.org/html/rfc6184#section-5.4
	naluType := payload[0] & naluTypeBitmask
	switch {
	case naluType > 0 && naluType < 24:
		return p.doPackaging(nil, payload), nil

	case naluType == stapaNALUType:
		currOffset := int(stapaHeaderSize)
		result := []byte{}
		for currOffset < len(payload) {
			naluSizeBytes := payload[currOffset:]
			if len(naluSizeBytes) < stapaNALULengthSize {
				break
			}
			naluSize := int(binary.BigEndian.Uint16(naluSizeBytes))
			currOffset += stapaNALULengthSize

			if len(payload) < currOffset+naluSize {
				return nil, fmt.Errorf(
					"%w STAP-A declared size(%d) is larger than buffer(%d)",
					errShortPacket,
					naluSize,
					len(payload)-currOffset,
				)
			}

			result = p.doPackaging(result, payload[currOffset:currOffset+naluSize])
			currOffset += naluSize
		}

		return result, nil

	case naluType == fuaNALUType:
		if len(payload) < fuaHeaderSize {
			return nil, errShortPacket
		}

		if p.fuaBuffer == nil {
			p.fuaBuffer = []byte{}
		}

		p.fuaBuffer = append(p.fuaBuffer, payload[fuaHeaderSize:]...)

		if payload[1]&fuEndBitmask != 0 {
			naluRefIdc := payload[0] & naluRefIdcBitmask
			fragmentedNaluType := payload[1] & naluTypeBitmask

			nalu := append([]byte{}, naluRefIdc|fragmentedNaluType)
			nalu = append(nalu, p.fuaBuffer...)
			p.fuaBuffer = nil

			return p.doPackaging(nil, nalu), nil
		}

		return []byte{}, nil
	}

	return nil, fmt.Errorf("%w: %d", errUnhandledNALUType, naluType)
}

// H264PartitionHeadChecker checks H264 partition head.
//
// Deprecated: replaced by H264Packet.IsPartitionHead().
type H264PartitionHeadChecker struct{}

// IsPartitionHead checks if this is the head of a packetized nalu stream.
//
// Deprecated: replaced by H264Packet.IsPartitionHead().
func (*H264PartitionHeadChecker) IsPartitionHead(packet []byte) bool {
	return (&H264Packet{}).IsPartitionHead(packet)
}

// IsPartitionHead checks if this is the head of a packetized nalu stream.
func (*H264Packet) IsPartitionHead(payload []byte) bool {
	if len(payload) < 2 {
		return false
	}

	if payload[0]&naluTypeBitmask == fuaNALUType ||
		payload[0]&naluTypeBitmask == fubNALUType {
		return payload[1]&fuStartBitmask != 0
	}

	return true
}
