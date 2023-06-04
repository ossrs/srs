package codecs

import (
	"encoding/binary"
	"fmt"
)

// H264Payloader payloads H264 packets
type H264Payloader struct {
	spsNalu, ppsNalu []byte
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

func annexbNALUStartCode() []byte { return []byte{0x00, 0x00, 0x00, 0x01} }

func emitNalus(nals []byte, emit func([]byte)) {
	nextInd := func(nalu []byte, start int) (indStart int, indLen int) {
		zeroCount := 0

		for i, b := range nalu[start:] {
			if b == 0 {
				zeroCount++
				continue
			} else if b == 1 {
				if zeroCount >= 2 {
					return start + i - zeroCount, zeroCount + 1
				}
			}
			zeroCount = 0
		}
		return -1, -1
	}

	nextIndStart, nextIndLen := nextInd(nals, 0)
	if nextIndStart == -1 {
		emit(nals)
	} else {
		for nextIndStart != -1 {
			prevStart := nextIndStart + nextIndLen
			nextIndStart, nextIndLen = nextInd(nals, prevStart)
			if nextIndStart != -1 {
				emit(nals[prevStart:nextIndStart])
			} else {
				// Emit until end of stream, no end indicator found
				emit(nals[prevStart:])
			}
		}
	}
}

// Payload fragments a H264 packet across one or more byte arrays
func (p *H264Payloader) Payload(mtu uint16, payload []byte) [][]byte {
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
			p.spsNalu = nalu
			return
		case naluType == ppsNALUType:
			p.ppsNalu = nalu
			return
		case p.spsNalu != nil && p.ppsNalu != nil:
			// Pack current NALU with SPS and PPS as STAP-A
			spsLen := make([]byte, 2)
			binary.BigEndian.PutUint16(spsLen, uint16(len(p.spsNalu)))

			ppsLen := make([]byte, 2)
			binary.BigEndian.PutUint16(ppsLen, uint16(len(p.ppsNalu)))

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

		naluData := nalu
		// According to the RFC, the first octet is skipped due to redundant information
		naluDataIndex := 1
		naluDataLength := len(nalu) - naluDataIndex
		naluDataRemaining := naluDataLength

		if min(maxFragmentSize, naluDataRemaining) <= 0 {
			return
		}

		for naluDataRemaining > 0 {
			currentFragmentSize := min(maxFragmentSize, naluDataRemaining)
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
			if naluDataRemaining == naluDataLength {
				// Set start bit
				out[1] |= 1 << 7
			} else if naluDataRemaining-currentFragmentSize == 0 {
				// Set end bit
				out[1] |= 1 << 6
			}

			copy(out[fuaHeaderSize:], naluData[naluDataIndex:naluDataIndex+currentFragmentSize])
			payloads = append(payloads, out)

			naluDataRemaining -= currentFragmentSize
			naluDataIndex += currentFragmentSize
		}
	})

	return payloads
}

// H264Packet represents the H264 header that is stored in the payload of an RTP Packet
type H264Packet struct {
	IsAVC     bool
	fuaBuffer []byte

	videoDepacketizer
}

func (p *H264Packet) doPackaging(nalu []byte) []byte {
	if p.IsAVC {
		naluLength := make([]byte, 4)
		binary.BigEndian.PutUint32(naluLength, uint32(len(nalu)))
		return append(naluLength, nalu...)
	}

	return append(annexbNALUStartCode(), nalu...)
}

// IsDetectedFinalPacketInSequence returns true of the packet passed in has the
// marker bit set indicated the end of a packet sequence
func (p *H264Packet) IsDetectedFinalPacketInSequence(rtpPacketMarketBit bool) bool {
	return rtpPacketMarketBit
}

// Unmarshal parses the passed byte slice and stores the result in the H264Packet this method is called upon
func (p *H264Packet) Unmarshal(payload []byte) ([]byte, error) {
	if payload == nil {
		return nil, errNilPacket
	} else if len(payload) <= 2 {
		return nil, fmt.Errorf("%w: %d <= 2", errShortPacket, len(payload))
	}

	// NALU Types
	// https://tools.ietf.org/html/rfc6184#section-5.4
	naluType := payload[0] & naluTypeBitmask
	switch {
	case naluType > 0 && naluType < 24:
		return p.doPackaging(payload), nil

	case naluType == stapaNALUType:
		currOffset := int(stapaHeaderSize)
		result := []byte{}
		for currOffset < len(payload) {
			naluSize := int(binary.BigEndian.Uint16(payload[currOffset:]))
			currOffset += stapaNALULengthSize

			if len(payload) < currOffset+naluSize {
				return nil, fmt.Errorf("%w STAP-A declared size(%d) is larger than buffer(%d)", errShortPacket, naluSize, len(payload)-currOffset)
			}

			result = append(result, p.doPackaging(payload[currOffset:currOffset+naluSize])...)
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
			return p.doPackaging(nalu), nil
		}

		return []byte{}, nil
	}

	return nil, fmt.Errorf("%w: %d", errUnhandledNALUType, naluType)
}

// H264PartitionHeadChecker is obsolete
type H264PartitionHeadChecker struct{}

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
