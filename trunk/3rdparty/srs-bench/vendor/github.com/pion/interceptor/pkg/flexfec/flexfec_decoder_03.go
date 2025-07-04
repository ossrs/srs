// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package flexfec implements FlexFEC-03 to recover missing RTP packets due to packet loss.
// https://datatracker.ietf.org/doc/html/draft-ietf-payload-flexible-fec-scheme-03
package flexfec

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sort"

	"github.com/pion/logging"
	"github.com/pion/rtp"
)

// Static errors for the flexfec package.
var (
	errPacketTruncated                = errors.New("packet truncated")
	errRetransmissionBitSet           = errors.New("packet with retransmission bit set not supported")
	errInflexibleGeneratorMatrix      = errors.New("packet with inflexible generator matrix not supported")
	errMultipleSSRCProtection         = errors.New("multiple ssrc protection not supported")
	errLastOptionalMaskKBitSetToFalse = errors.New("k-bit of last optional mask is set to false")
)

// fecDecoder is a WIP implementation decoder used for testing purposes.
type fecDecoder struct {
	logger              logging.LeveledLogger
	ssrc                uint32
	protectedStreamSSRC uint32
	maxMediaPackets     int
	maxFECPackets       int
	recoveredPackets    []rtp.Packet
	receivedFECPackets  []fecPacketState
}

func newFECDecoder(ssrc uint32, protectedStreamSSRC uint32) *fecDecoder {
	return &fecDecoder{
		logger:              logging.NewDefaultLoggerFactory().NewLogger("fec_decoder"),
		ssrc:                ssrc,
		protectedStreamSSRC: protectedStreamSSRC,
		maxMediaPackets:     100,
		maxFECPackets:       100,
		recoveredPackets:    make([]rtp.Packet, 0),
		receivedFECPackets:  make([]fecPacketState, 0),
	}
}

func (d *fecDecoder) DecodeFec(receivedPacket rtp.Packet) []rtp.Packet {
	if len(d.recoveredPackets) == d.maxMediaPackets {
		backRecoveredPacket := d.recoveredPackets[len(d.recoveredPackets)-1]
		if backRecoveredPacket.SSRC == receivedPacket.SSRC {
			seqDiffVal := seqDiff(receivedPacket.SequenceNumber, backRecoveredPacket.SequenceNumber)
			if seqDiffVal > uint16(d.maxMediaPackets) { //nolint:gosec
				d.logger.Info("big gap in media sequence numbers - resetting buffers")
				d.recoveredPackets = nil
				d.receivedFECPackets = nil
			}
		}
	}

	d.insertPacket(receivedPacket)

	return d.attemptRecovery()
}

func (d *fecDecoder) insertPacket(receivedPkt rtp.Packet) {
	// Discard old FEC packets such that the sequence numbers in
	// `received_fec_packets_` span at most 1/2 of the sequence number space.
	// This is important for keeping `received_fec_packets_` sorted, and may
	// also reduce the possibility of incorrect decoding due to sequence number
	// wrap-around.
	if len(d.receivedFECPackets) > 0 && receivedPkt.SSRC == d.ssrc {
		toRemove := 0
		for _, fecPkt := range d.receivedFECPackets {
			if abs(int(receivedPkt.SequenceNumber)-int(fecPkt.packet.SequenceNumber)) > 0x3fff {
				toRemove++
			} else {
				// No need to keep iterating, since |received_fec_packets_| is sorted.
				break
			}
		}
	}

	switch receivedPkt.SSRC {
	case d.ssrc:
		d.insertFECPacket(receivedPkt)
	case d.protectedStreamSSRC:
		d.insertMediaPacket(receivedPkt)
	}

	d.discardOldRecoveredPackets()
}

func (d *fecDecoder) insertMediaPacket(receivedPkt rtp.Packet) {
	for _, recoveredPacket := range d.recoveredPackets {
		if recoveredPacket.SequenceNumber == receivedPkt.SequenceNumber {
			return
		}
	}

	d.recoveredPackets = append(d.recoveredPackets, receivedPkt)
	sort.Slice(d.recoveredPackets, func(i, j int) bool {
		return isNewerSeq(d.recoveredPackets[i].SequenceNumber, d.recoveredPackets[j].SequenceNumber)
	})
	d.updateCoveringFecPackets(receivedPkt)
}

func (d *fecDecoder) updateCoveringFecPackets(receivedPkt rtp.Packet) {
	for _, fecPkt := range d.receivedFECPackets {
		for _, protectedPacket := range fecPkt.protectedPackets {
			if protectedPacket.seq == receivedPkt.SequenceNumber {
				protectedPacket.packet = &receivedPkt
			}
		}
	}
}

func (d *fecDecoder) insertFECPacket(fecPkt rtp.Packet) { //nolint:cyclop
	for _, existingFECPacket := range d.receivedFECPackets {
		if existingFECPacket.packet.SequenceNumber == fecPkt.SequenceNumber {
			return
		}
	}

	fec, err := parseFlexFEC03Header(fecPkt.Payload)
	if err != nil {
		d.logger.Errorf("failed to parse flexfec03 header: %v", err)

		return
	}

	if fec.protectedSSRC != d.protectedStreamSSRC {
		d.logger.Errorf("fec is protecting unknown ssrc, expected %d, got %d", fec.protectedSSRC, d.protectedStreamSSRC)

		return
	}

	protectedSeqs := decodeMask(uint64(fec.mask0), 15, fec.seqNumBase)
	if fec.mask1 != 0 {
		protectedSeqs = append(protectedSeqs, decodeMask(uint64(fec.mask1), 31, fec.seqNumBase+15)...)
	}
	if fec.mask2 != 0 {
		protectedSeqs = append(protectedSeqs, decodeMask(fec.mask2, 63, fec.seqNumBase+46)...)
	}

	if len(protectedSeqs) == 0 {
		d.logger.Warn("empty fec packet mask")

		return
	}

	protectedPackets := make([]*protectedPacket, 0, len(protectedSeqs))
	protectedSeqIt := 0
	recoveredPacketIt := 0

	for protectedSeqIt < len(protectedSeqs) && recoveredPacketIt < len(d.recoveredPackets) {
		switch {
		case isNewerSeq(protectedSeqs[protectedSeqIt], d.recoveredPackets[recoveredPacketIt].SequenceNumber):
			protectedPackets = append(protectedPackets, &protectedPacket{
				seq:    protectedSeqs[protectedSeqIt],
				packet: nil,
			})
			protectedSeqIt++
		case isNewerSeq(d.recoveredPackets[recoveredPacketIt].SequenceNumber, protectedSeqs[protectedSeqIt]):
			recoveredPacketIt++
		default:
			protectedPackets = append(protectedPackets, &protectedPacket{
				seq:    protectedSeqs[protectedSeqIt],
				packet: &d.recoveredPackets[recoveredPacketIt],
			})
			protectedSeqIt++
			recoveredPacketIt++
		}
	}

	for protectedSeqIt < len(protectedSeqs) {
		protectedPackets = append(protectedPackets, &protectedPacket{
			seq:    protectedSeqs[protectedSeqIt],
			packet: nil,
		})
		protectedSeqIt++
	}
	d.receivedFECPackets = append(d.receivedFECPackets, fecPacketState{
		packet:           fecPkt,
		flexFec:          fec,
		protectedPackets: protectedPackets,
	})

	sort.Slice(d.receivedFECPackets, func(i, j int) bool {
		return isNewerSeq(d.receivedFECPackets[i].packet.SequenceNumber, d.receivedFECPackets[j].packet.SequenceNumber)
	})

	if len(d.receivedFECPackets) > d.maxFECPackets {
		d.receivedFECPackets = d.receivedFECPackets[1:]
	}
}

func (d *fecDecoder) attemptRecovery() []rtp.Packet {
	recoveredPackets := make([]rtp.Packet, 0)
	for {
		packetsRecovered := 0
		for _, fecPkt := range d.receivedFECPackets {
			packetsMissing := 0
			for _, pkt := range fecPkt.protectedPackets {
				if pkt.packet == nil {
					packetsMissing++
					if packetsMissing > 1 {
						break
					}
				}
			}

			if packetsMissing != 1 {
				continue
			}

			recovered, err := d.recoverPacket(&fecPkt) //nolint:gosec
			if err != nil {
				d.logger.Errorf("failed to recover packet: %v", err)
			}

			recoveredPackets = append(recoveredPackets, recovered)
			d.recoveredPackets = append(d.recoveredPackets, recovered)
			sort.Slice(d.recoveredPackets, func(i, j int) bool {
				return isNewerSeq(d.recoveredPackets[i].SequenceNumber, d.recoveredPackets[j].SequenceNumber)
			})

			d.updateCoveringFecPackets(recovered)
			d.discardOldRecoveredPackets()
			packetsRecovered++
		}

		if packetsRecovered == 0 {
			break
		}
	}

	return recoveredPackets
}

func (d *fecDecoder) recoverPacket(fec *fecPacketState) (rtp.Packet, error) {
	// https://datatracker.ietf.org/doc/html/draft-ietf-payload-flexible-fec-scheme-03#section-6.3.2

	// 2. For the repair packet in T, extract the FEC bit string as the
	//    first 80 bits of the FEC header.
	headerRecovery := make([]byte, 12)
	copy(headerRecovery, fec.packet.Payload[:10])

	var seqnum uint16
	for _, protectedPacket := range fec.protectedPackets {
		if protectedPacket.packet != nil {
			// 1.   For each of the source packets that are successfully received in
			//      T, compute the 80-bit string by concatenating the first 64 bits
			//      of their RTP header and the unsigned network-ordered 16-bit
			//      representation of their length in bytes minus 12.
			receivedHeader, err := protectedPacket.packet.Header.Marshal()
			if err != nil {
				return rtp.Packet{}, fmt.Errorf("marshal received header: %w", err)
			}
			binary.BigEndian.PutUint16(receivedHeader[2:4], uint16(protectedPacket.packet.MarshalSize()-12)) //nolint:gosec
			for i := 0; i < 8; i++ {
				headerRecovery[i] ^= receivedHeader[i]
			}
		} else {
			seqnum = protectedPacket.seq
		}
	}

	// set version to 2
	headerRecovery[0] |= 0x80
	headerRecovery[0] &= 0xbf
	payloadLength := binary.BigEndian.Uint16(headerRecovery[2:4])
	binary.BigEndian.PutUint16(headerRecovery[2:4], seqnum)
	binary.BigEndian.PutUint32(headerRecovery[8:12], d.protectedStreamSSRC)

	payloadRecovery := make([]byte, payloadLength)
	copy(payloadRecovery, fec.flexFec.payload)
	for _, protectedPacket := range fec.protectedPackets {
		if protectedPacket.packet != nil {
			packet, err := protectedPacket.packet.Marshal()
			if err != nil {
				return rtp.Packet{}, fmt.Errorf("marshal protected packet: %w", err)
			}
			for i := 0; i < minInt(int(payloadLength), len(packet)-12); i++ {
				payloadRecovery[i] ^= packet[12+i]
			}
		}
	}

	headerRecovery = append(headerRecovery, payloadRecovery...) //nolint:makezero

	var packet rtp.Packet
	err := packet.Unmarshal(headerRecovery)
	if err != nil {
		return rtp.Packet{}, fmt.Errorf("unmarshal recovered: %w", err)
	}

	return packet, nil
}

func (d *fecDecoder) discardOldRecoveredPackets() {
	const limit = 192
	if len(d.recoveredPackets) > limit {
		d.recoveredPackets = d.recoveredPackets[len(d.recoveredPackets)-192:]
	}
}

func decodeMask(mask uint64, bitCount uint16, seqNumBase uint16) []uint16 {
	res := make([]uint16, 0)
	for i := uint16(0); i < bitCount; i++ {
		if (mask>>(bitCount-1-i))&1 == 1 {
			res = append(res, seqNumBase+i)
		}
	}

	return res
}

type fecPacketState struct {
	packet           rtp.Packet
	flexFec          flexFec
	protectedPackets []*protectedPacket
}

type flexFec struct {
	protectedSSRC uint32
	seqNumBase    uint16
	mask0         uint16
	mask1         uint32
	mask2         uint64
	payload       []byte
}

type protectedPacket struct {
	seq    uint16
	packet *rtp.Packet
}

func parseFlexFEC03Header(data []byte) (flexFec, error) {
	if len(data) < 20 {
		return flexFec{}, fmt.Errorf("%w: length %d", errPacketTruncated, len(data))
	}

	rBit := (data[0] & 0x80) != 0
	if rBit {
		return flexFec{}, errRetransmissionBitSet
	}

	fBit := (data[0] & 0x40) != 0
	if fBit {
		return flexFec{}, errInflexibleGeneratorMatrix
	}

	ssrcCount := data[8]
	if ssrcCount != 1 {
		return flexFec{}, fmt.Errorf("%w: count %d", errMultipleSSRCProtection, ssrcCount)
	}

	protectedSSRC := binary.BigEndian.Uint32(data[12:])
	seqNumBase := binary.BigEndian.Uint16(data[16:])
	rawPacketMask := data[18:]
	var payload []byte

	kBit0 := (rawPacketMask[0] & 0x80) != 0
	maskPart0 := binary.BigEndian.Uint16(rawPacketMask[0:2]) & 0x7FFF
	var maskPart1 uint32
	var maskPart2 uint64

	if kBit0 { //nolint:nestif
		payload = rawPacketMask[2:]
	} else {
		if len(data) < 24 {
			return flexFec{}, fmt.Errorf("%w: length %d", errPacketTruncated, len(data))
		}

		kBit1 := (rawPacketMask[2] & 0x80) != 0
		maskPart1 = binary.BigEndian.Uint32(rawPacketMask[2:]) & 0x7FFFFFFF

		if kBit1 {
			payload = rawPacketMask[6:]
		} else {
			if len(data) < 32 {
				return flexFec{}, fmt.Errorf("%w: length %d", errPacketTruncated, len(data))
			}

			kBit2 := (rawPacketMask[6] & 0x80) != 0
			maskPart2 = binary.BigEndian.Uint64(rawPacketMask[6:]) & 0x7FFFFFFFFFFFFFFF

			if kBit2 {
				payload = rawPacketMask[14:]
			} else {
				return flexFec{}, errLastOptionalMaskKBitSetToFalse
			}
		}
	}

	return flexFec{
		protectedSSRC: protectedSSRC,
		seqNumBase:    seqNumBase,
		mask0:         maskPart0,
		mask1:         maskPart1,
		mask2:         maskPart2,
		payload:       payload,
	}, nil
}

func seqDiff(a, b uint16) uint16 {
	return minUInt16(a-b, b-a)
}

func minInt(a, b int) int {
	if a < b {
		return a
	}

	return b
}

func minUInt16(a, b uint16) uint16 {
	if a < b {
		return a
	}

	return b
}

func abs(x int) int {
	if x >= 0 {
		return x
	}

	return -x
}

func isNewerSeq(prevValue, value uint16) bool {
	// half-way mark
	breakpoint := uint16(0x8000)
	if value-prevValue == breakpoint {
		return value > prevValue
	}

	return value != prevValue && (value-prevValue) < breakpoint
}
