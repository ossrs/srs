// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package flexfec implements FlexFEC to recover missing RTP packets due to packet loss.
// https://datatracker.ietf.org/doc/html/rfc8627
package flexfec

import (
	"encoding/binary"

	"github.com/pion/interceptor/pkg/flexfec/util"
	"github.com/pion/rtp"
)

const (
	// BaseRTPHeaderSize represents the minium RTP packet header size in bytes.
	BaseRTPHeaderSize = 12
	// BaseFecHeaderSize represents the minium FEC payload's header size including the
	// required first mask.
	BaseFecHeaderSize = 12
)

// EncoderFactory is an interface for generic FEC encoders.
type EncoderFactory interface {
	NewEncoder(payloadType uint8, ssrc uint32) FlexEncoder
}

// FlexEncoder is the interface that FecInterceptor uses to encode Fec packets.
type FlexEncoder interface {
	EncodeFec(mediaPackets []rtp.Packet, numFecPackets uint32) []rtp.Packet
}

// FlexEncoder20 implementation is WIP, contains bugs and no tests. Check out FlexEncoder03.
type FlexEncoder20 struct {
	fecBaseSn   uint16
	payloadType uint8
	ssrc        uint32
	coverage    *ProtectionCoverage
}

// NewFlexEncoder returns a new FlexEncoder20.
// FlexEncoder20 implementation is WIP, contains bugs and no tests. Check out FlexEncoder03.
func NewFlexEncoder(payloadType uint8, ssrc uint32) *FlexEncoder20 {
	return &FlexEncoder20{
		payloadType: payloadType,
		ssrc:        ssrc,
		fecBaseSn:   uint16(1000),
	}
}

// EncodeFec returns a list of generated RTP packets with FEC payloads that protect the specified mediaPackets.
// This method does not account for missing RTP packets in the mediaPackets array nor does it account for
// them being passed out of order.
func (flex *FlexEncoder20) EncodeFec(mediaPackets []rtp.Packet, numFecPackets uint32) []rtp.Packet {
	// Start by defining which FEC packets cover which media packets
	if flex.coverage == nil {
		flex.coverage = NewCoverage(mediaPackets, numFecPackets)
	} else {
		flex.coverage.UpdateCoverage(mediaPackets, numFecPackets)
	}

	if flex.coverage == nil {
		return nil
	}

	// Generate FEC payloads
	fecPackets := make([]rtp.Packet, numFecPackets)
	for fecPacketIndex := uint32(0); fecPacketIndex < numFecPackets; fecPacketIndex++ {
		fecPackets[fecPacketIndex] = flex.encodeFlexFecPacket(fecPacketIndex, mediaPackets[0].SequenceNumber)
	}

	return fecPackets
}

func (flex *FlexEncoder20) encodeFlexFecPacket(fecPacketIndex uint32, mediaBaseSn uint16) rtp.Packet {
	mediaPacketsIt := flex.coverage.GetCoveredBy(fecPacketIndex)
	flexFecHeader := flex.encodeFlexFecHeader(
		mediaPacketsIt,
		flex.coverage.ExtractMask1(fecPacketIndex),
		flex.coverage.ExtractMask2(fecPacketIndex),
		flex.coverage.ExtractMask3(fecPacketIndex),
		mediaBaseSn,
	)
	flexFecRepairPayload := flex.encodeFlexFecRepairPayload(mediaPacketsIt.Reset())

	packet := rtp.Packet{
		Header: rtp.Header{
			Version:        2,
			Padding:        false,
			Extension:      false,
			Marker:         false,
			PayloadType:    flex.payloadType,
			SequenceNumber: flex.fecBaseSn,
			Timestamp:      54243243,
			SSRC:           flex.ssrc,
			CSRC:           []uint32{},
		},
		Payload: append(flexFecHeader, flexFecRepairPayload...),
	}
	flex.fecBaseSn++

	return packet
}

func (flex *FlexEncoder20) encodeFlexFecHeader(
	mediaPackets *util.MediaPacketIterator,
	mask1 uint16,
	optionalMask2 uint32,
	optionalMask3 uint64,
	mediaBaseSn uint16,
) []byte {
	/*
	   0                   1                   2                   3
	        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	       |0|0|P|X|  CC   |M| PT recovery |        length recovery        |
	       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	       |                          TS recovery                          |
	       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	       |           SN base_i           |k|          Mask [0-14]        |
	       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	       |k|                   Mask [15-45] (optional)                   |
	       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	       |                     Mask [46-109] (optional)                  |
	       |                                                               |
	       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	       |   ... next SN base and Mask for CSRC_i in CSRC list ...       |
	       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	       :               Repair "Payload" follows FEC Header             :
	       :                                                               :
	*/

	// Get header size - This depends on the size of the bitmask.
	headerSize := BaseFecHeaderSize
	if optionalMask2 > 0 {
		headerSize += 4
	}
	if optionalMask3 > 0 {
		headerSize += 8
	}

	// Allocate the FlexFec header
	flexFecHeader := make([]byte, headerSize)

	// XOR the relevant fields for the header
	// TO DO - CHECK TO SEE IF THE MARSHALTO() call works with this.
	tmpMediaPacketBuf := make([]byte, headerSize)
	for mediaPackets.HasNext() {
		mediaPacket := mediaPackets.Next()
		n, err := mediaPacket.MarshalTo(tmpMediaPacketBuf)

		if n == 0 || err != nil {
			return nil
		}

		// XOR the first 2 bytes of the header: V, P, X, CC, M, PT fields
		flexFecHeader[0] ^= tmpMediaPacketBuf[0]
		flexFecHeader[1] ^= tmpMediaPacketBuf[1]

		// XOR the length recovery field
		lengthRecoveryVal := uint16(mediaPacket.MarshalSize() - BaseRTPHeaderSize) //nolint:gosec // G115
		flexFecHeader[2] ^= uint8(lengthRecoveryVal >> 8)                          //nolint:gosec // G115
		flexFecHeader[3] ^= uint8(lengthRecoveryVal)                               //nolint:gosec // G115

		// XOR the 5th to 8th bytes of the header: the timestamp field
		flexFecHeader[4] ^= flexFecHeader[4]
		flexFecHeader[5] ^= flexFecHeader[5]
		flexFecHeader[6] ^= flexFecHeader[6]
		flexFecHeader[7] ^= flexFecHeader[7]
	}

	// Write the base SN for the batch of media packets
	binary.BigEndian.PutUint16(flexFecHeader[8:10], mediaBaseSn)

	// Write the bitmasks to the header
	binary.BigEndian.PutUint16(flexFecHeader[10:12], mask1)

	if optionalMask2 > 0 {
		binary.BigEndian.PutUint32(flexFecHeader[12:16], optionalMask2)
		flexFecHeader[10] |= 0b10000000
	}
	if optionalMask3 > 0 {
		binary.BigEndian.PutUint64(flexFecHeader[16:24], optionalMask3)
		flexFecHeader[12] |= 0b10000000
	}

	return flexFecHeader
}

func (flex *FlexEncoder20) encodeFlexFecRepairPayload(mediaPackets *util.MediaPacketIterator) []byte {
	flexFecPayload := make([]byte, len(mediaPackets.First().Payload))

	for mediaPackets.HasNext() {
		mediaPacketPayload := mediaPackets.Next().Payload

		if len(flexFecPayload) < len(mediaPacketPayload) {
			// Expected FEC packet payload is bigger that what we can currently store,
			// we need to resize.
			flexFecPayloadTmp := make([]byte, len(mediaPacketPayload))
			copy(flexFecPayloadTmp, flexFecPayload)
			flexFecPayload = flexFecPayloadTmp
		}
		for byteIndex := 0; byteIndex < len(mediaPacketPayload); byteIndex++ {
			flexFecPayload[byteIndex] ^= mediaPacketPayload[byteIndex]
		}
	}

	return flexFecPayload
}
