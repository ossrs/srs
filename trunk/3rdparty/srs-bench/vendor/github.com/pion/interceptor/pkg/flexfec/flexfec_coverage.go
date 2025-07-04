// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package flexfec

import (
	"github.com/pion/interceptor/pkg/flexfec/util"
	"github.com/pion/rtp"
)

// Maximum number of media packets that can be protected by a single FEC packet.
// We are not supporting the possibility of having an FEC packet protect multiple
// SSRC source packets for now.
// https://datatracker.ietf.org/doc/html/rfc8627#section-4.2.2.1
const (
	MaxMediaPackets uint32 = 110
	MaxFecPackets   uint32 = MaxMediaPackets
)

// ProtectionCoverage defines the map of RTP packets that individual Fec packets protect.
type ProtectionCoverage struct {
	// Array of masks, each mask capable of covering up to maxMediaPkts = 110.
	// A mask is represented as a grouping of bytes where each individual bit
	// represents the coverage for the media packet at the corresponding index.
	packetMasks     [MaxFecPackets]util.BitArray
	numFecPackets   uint32
	numMediaPackets uint32
	mediaPackets    []rtp.Packet
}

// NewCoverage returns a new ProtectionCoverage object. numFecPackets represents the number of
// Fec packets that we will be generating to cover the list of mediaPackets. This allows us to know
// how big the underlying map should be.
func NewCoverage(mediaPackets []rtp.Packet, numFecPackets uint32) *ProtectionCoverage {
	numMediaPackets := uint32(len(mediaPackets)) //nolint:gosec // G115

	// Basic sanity checks
	if numMediaPackets <= 0 || numMediaPackets > MaxMediaPackets {
		return nil
	}

	// We allocate the biggest array of bitmasks that respects the max constraints.
	var packetMasks [MaxFecPackets]util.BitArray
	for i := 0; i < int(MaxFecPackets); i++ {
		packetMasks[i] = util.BitArray{}
	}

	coverage := &ProtectionCoverage{
		packetMasks:     packetMasks,
		numFecPackets:   0,
		numMediaPackets: 0,
		mediaPackets:    nil,
	}

	coverage.UpdateCoverage(mediaPackets, numFecPackets)

	return coverage
}

// UpdateCoverage updates the ProtectionCoverage object with new bitmasks accounting for the numFecPackets
// we want to use to protect the batch media packets.
func (p *ProtectionCoverage) UpdateCoverage(mediaPackets []rtp.Packet, numFecPackets uint32) {
	numMediaPackets := uint32(len(mediaPackets)) //nolint:gosec // G115

	// Basic sanity checks
	if numMediaPackets <= 0 || numMediaPackets > MaxMediaPackets {
		return
	}

	p.mediaPackets = mediaPackets

	if numFecPackets == p.numFecPackets && numMediaPackets == p.numMediaPackets {
		// We have the same number of FEC packets covering the same number of media packets, we can simply
		// reuse the previous coverage map with the updated media packets.
		return
	}

	p.numFecPackets = numFecPackets
	p.numMediaPackets = numMediaPackets

	// The number of FEC packets and/or the number of packets has changed, we need to update the coverage map
	// to reflect these new values.
	p.resetCoverage()

	// Generate FEC bit mask where numFecPackets FEC packets are covering numMediaPackets Media packets.
	// In the packetMasks array, each FEC packet is represented by a single BitArray, each bit in a given BitArray
	// corresponds to a specific Media packet.
	// Ex: Row I, Col J is set to 1 -> FEC packet I will protect media packet J.
	for fecPacketIndex := uint32(0); fecPacketIndex < numFecPackets; fecPacketIndex++ {
		// We use an interleaved method to determine coverage. Given N FEC packets, Media packet X will be
		// covered by FEC packet X % N.
		coveredMediaPacketIndex := fecPacketIndex
		for coveredMediaPacketIndex < numMediaPackets {
			p.packetMasks[fecPacketIndex].SetBit(coveredMediaPacketIndex)
			coveredMediaPacketIndex += numFecPackets
		}
	}
}

// ResetCoverage clears the underlying map so that we can reuse it for new batches of RTP packets.
func (p *ProtectionCoverage) resetCoverage() {
	for i := uint32(0); i < MaxFecPackets; i++ {
		p.packetMasks[i].Reset()
	}
}

// GetCoveredBy returns an iterator over RTP packets that are protected by the specified Fec packet index.
func (p *ProtectionCoverage) GetCoveredBy(fecPacketIndex uint32) *util.MediaPacketIterator {
	coverage := make([]uint32, 0, p.numMediaPackets)
	for mediaPacketIndex := uint32(0); mediaPacketIndex < p.numMediaPackets; mediaPacketIndex++ {
		if p.packetMasks[fecPacketIndex].GetBit(mediaPacketIndex) == 1 {
			coverage = append(coverage, mediaPacketIndex)
		}
	}

	return util.NewMediaPacketIterator(p.mediaPackets, coverage)
}

// ExtractMask1 returns the first section of the bitmask as defined by the FEC header.
// https://datatracker.ietf.org/doc/html/rfc8627#section-4.2.2.1
func (p *ProtectionCoverage) ExtractMask1(fecPacketIndex uint32) uint16 {
	return extractMask1(p.packetMasks[fecPacketIndex])
}

// ExtractMask2 returns the second section of the bitmask as defined by the FEC header.
// https://datatracker.ietf.org/doc/html/rfc8627#section-4.2.2.1
func (p *ProtectionCoverage) ExtractMask2(fecPacketIndex uint32) uint32 {
	return extractMask2(p.packetMasks[fecPacketIndex])
}

// ExtractMask3 returns the third section of the bitmask as defined by the FEC header.
// https://datatracker.ietf.org/doc/html/rfc8627#section-4.2.2.1
func (p *ProtectionCoverage) ExtractMask3(fecPacketIndex uint32) uint64 {
	return extractMask3(p.packetMasks[fecPacketIndex])
}

// ExtractMask3_03 returns the third section of the bitmask as defined by the FEC header.
// https://datatracker.ietf.org/doc/html/draft-ietf-payload-flexible-fec-scheme-03#section-4.2
func (p *ProtectionCoverage) ExtractMask3_03(fecPacketIndex uint32) uint64 {
	return extractMask3_03(p.packetMasks[fecPacketIndex])
}

func extractMask1(mask util.BitArray) uint16 {
	// We get the first 16 bits (64 - 16 -> shift by 48) and we shift once more for K field
	mask1 := mask.Lo >> 49

	return uint16(mask1) //nolint:gosec // G115
}

func extractMask2(mask util.BitArray) uint32 {
	// We remove the first 15 bits
	mask2 := mask.Lo << 15
	// We get the first 31 bits (64 - 32 -> shift by 32) and we shift once more for K field
	mask2 >>= 33

	return uint32(mask2) //nolint:gosec
}

func extractMask3(mask util.BitArray) uint64 {
	// We remove the first 46 bits
	maskLo := mask.Lo << 46
	maskHi := mask.Hi >> 18
	mask3 := maskLo | maskHi

	return mask3
}

func extractMask3_03(mask util.BitArray) uint64 {
	// We remove the first 46 bits
	maskLo := mask.Lo << 46
	maskHi := mask.Hi >> 18
	mask3 := maskLo | maskHi
	// We shift once for the K bit.
	mask3 >>= 1

	return mask3
}
