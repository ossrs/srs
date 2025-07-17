// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

import (
	"fmt"
	"math/bits"
)

type receivePayloadQueue struct {
	tailTSN      uint32
	chunkSize    int
	tsnBitmask   []uint64
	dupTSN       []uint32
	maxTSNOffset uint32

	cumulativeTSN uint32
}

func newReceivePayloadQueue(maxTSNOffset uint32) *receivePayloadQueue {
	maxTSNOffset = ((maxTSNOffset + 63) / 64) * 64

	return &receivePayloadQueue{
		tsnBitmask:   make([]uint64, maxTSNOffset/64),
		maxTSNOffset: maxTSNOffset,
	}
}

func (q *receivePayloadQueue) init(cumulativeTSN uint32) {
	q.cumulativeTSN = cumulativeTSN
	q.tailTSN = cumulativeTSN
	q.chunkSize = 0
	for i := range q.tsnBitmask {
		q.tsnBitmask[i] = 0
	}
	q.dupTSN = q.dupTSN[:0]
}

func (q *receivePayloadQueue) hasChunk(tsn uint32) bool {
	if q.chunkSize == 0 || sna32LTE(tsn, q.cumulativeTSN) || sna32GT(tsn, q.tailTSN) {
		return false
	}

	index, offset := int(tsn/64)%len(q.tsnBitmask), tsn%64

	return q.tsnBitmask[index]&(1<<offset) != 0
}

func (q *receivePayloadQueue) canPush(tsn uint32) bool {
	ok := q.hasChunk(tsn)
	if ok || sna32LTE(tsn, q.cumulativeTSN) || sna32GT(tsn, q.cumulativeTSN+q.maxTSNOffset) {
		return false
	}

	return true
}

// push pushes a payload data. If the payload data is already in our queue or
// older than our cumulativeTSN marker, it will be recored as duplications,
// which can later be retrieved using popDuplicates.
func (q *receivePayloadQueue) push(tsn uint32) bool {
	if sna32GT(tsn, q.cumulativeTSN+q.maxTSNOffset) {
		return false
	}

	if sna32LTE(tsn, q.cumulativeTSN) || q.hasChunk(tsn) {
		// Found the packet, log in dups
		q.dupTSN = append(q.dupTSN, tsn)

		return false
	}

	index, offset := int(tsn/64)%len(q.tsnBitmask), tsn%64
	q.tsnBitmask[index] |= (1 << offset)
	q.chunkSize++
	if sna32GT(tsn, q.tailTSN) {
		q.tailTSN = tsn
	}

	return true
}

// pop advances cumulativeTSN and pops the oldest chunk's TSN if it matches the given TSN or force is true.
func (q *receivePayloadQueue) pop(force bool) bool {
	tsn := q.cumulativeTSN + 1
	if q.hasChunk(tsn) {
		index, offset := int(tsn/64)%len(q.tsnBitmask), int(tsn%64)
		q.tsnBitmask[index] &= ^uint64(1 << (offset))
		q.chunkSize--
		q.cumulativeTSN++

		return true
	}
	if force {
		q.cumulativeTSN++
		if q.chunkSize == 0 {
			q.tailTSN = q.cumulativeTSN
		}
	}

	return false
}

// popDuplicates returns an array of TSN values that were found duplicate.
func (q *receivePayloadQueue) popDuplicates() []uint32 {
	dups := q.dupTSN
	q.dupTSN = []uint32{}

	return dups
}

func (q *receivePayloadQueue) getGapAckBlocks() (gapAckBlocks []gapAckBlock) {
	var ackBlock gapAckBlock

	if q.chunkSize == 0 {
		return nil
	}

	startTSN, endTSN := q.cumulativeTSN+1, q.tailTSN
	var findEnd bool
	for tsn := startTSN; sna32LTE(tsn, endTSN); {
		index, offset := int(tsn/64)%len(q.tsnBitmask), int(tsn%64)
		if !findEnd { //nolint:nestif
			// find first received tsn as start
			if nonZeroBit, ok := getFirstNonZeroBit(q.tsnBitmask[index], offset, 64); ok {
				//nolint:gosec // G115
				ackBlock.start = uint16(tsn + uint32(nonZeroBit-offset) - q.cumulativeTSN)
				tsn += uint32(nonZeroBit - offset) //nolint:gosec // G115
				findEnd = true
			} else {
				// no result, find start bits in next uint64 bitmask
				tsn += uint32(64 - offset) //nolint:gosec // G115
			}
		} else {
			if zeroBit, ok := getFirstZeroBit(q.tsnBitmask[index], offset, 64); ok {
				//nolint:gosec // G115
				ackBlock.end = uint16(tsn + uint32(zeroBit-offset) - 1 - q.cumulativeTSN)
				tsn += uint32(zeroBit - offset) //nolint:gosec // G115
				if sna32LTE(tsn, endTSN) {
					gapAckBlocks = append(gapAckBlocks, gapAckBlock{
						start: ackBlock.start,
						end:   ackBlock.end,
					})
				}
				findEnd = false
			} else {
				tsn += uint32(64 - offset) //nolint:gosec // G115
			}

			// no zero bit at the end, close and append the last gap
			if sna32GT(tsn, endTSN) {
				ackBlock.end = uint16(endTSN - q.cumulativeTSN) //nolint:gosec // G115
				gapAckBlocks = append(gapAckBlocks, gapAckBlock{
					start: ackBlock.start,
					end:   ackBlock.end,
				})

				break
			}
		}
	}

	return gapAckBlocks
}

func (q *receivePayloadQueue) getGapAckBlocksString() string {
	gapAckBlocks := q.getGapAckBlocks()
	str := fmt.Sprintf("cumTSN=%d", q.cumulativeTSN)
	for _, b := range gapAckBlocks {
		str += fmt.Sprintf(",%d-%d", b.start, b.end)
	}

	return str
}

func (q *receivePayloadQueue) getLastTSNReceived() (uint32, bool) {
	if q.chunkSize == 0 {
		return 0, false
	}

	return q.tailTSN, true
}

func (q *receivePayloadQueue) getcumulativeTSN() uint32 {
	return q.cumulativeTSN
}

func (q *receivePayloadQueue) size() int {
	return q.chunkSize
}

func getFirstNonZeroBit(val uint64, start, end int) (int, bool) {
	i := bits.TrailingZeros64(val >> uint64(start)) //nolint:gosec // G115

	return i + start, i+start < end
}

func getFirstZeroBit(val uint64, start, end int) (int, bool) {
	return getFirstNonZeroBit(^val, start, end)
}
