package sctp

import (
	"fmt"
	"sort"
)

type payloadQueue struct {
	chunkMap map[uint32]*chunkPayloadData
	sorted   []uint32
	dupTSN   []uint32
	nBytes   int
}

func newPayloadQueue() *payloadQueue {
	return &payloadQueue{chunkMap: map[uint32]*chunkPayloadData{}}
}

func (q *payloadQueue) updateSortedKeys() {
	if q.sorted != nil {
		return
	}

	q.sorted = make([]uint32, len(q.chunkMap))
	i := 0
	for k := range q.chunkMap {
		q.sorted[i] = k
		i++
	}

	sort.Slice(q.sorted, func(i, j int) bool {
		return sna32LT(q.sorted[i], q.sorted[j])
	})
}

func (q *payloadQueue) canPush(p *chunkPayloadData, cumulativeTSN uint32) bool {
	_, ok := q.chunkMap[p.tsn]
	if ok || sna32LTE(p.tsn, cumulativeTSN) {
		return false
	}
	return true
}

func (q *payloadQueue) pushNoCheck(p *chunkPayloadData) {
	q.chunkMap[p.tsn] = p
	q.nBytes += len(p.userData)
	q.sorted = nil
}

// push pushes a payload data. If the payload data is already in our queue or
// older than our cumulativeTSN marker, it will be recored as duplications,
// which can later be retrieved using popDuplicates.
func (q *payloadQueue) push(p *chunkPayloadData, cumulativeTSN uint32) bool {
	_, ok := q.chunkMap[p.tsn]
	if ok || sna32LTE(p.tsn, cumulativeTSN) {
		// Found the packet, log in dups
		q.dupTSN = append(q.dupTSN, p.tsn)
		return false
	}

	q.chunkMap[p.tsn] = p
	q.nBytes += len(p.userData)
	q.sorted = nil
	return true
}

// pop pops only if the oldest chunk's TSN matches the given TSN.
func (q *payloadQueue) pop(tsn uint32) (*chunkPayloadData, bool) {
	q.updateSortedKeys()

	if len(q.chunkMap) > 0 && tsn == q.sorted[0] {
		q.sorted = q.sorted[1:]
		if c, ok := q.chunkMap[tsn]; ok {
			delete(q.chunkMap, tsn)
			q.nBytes -= len(c.userData)
			return c, true
		}
	}

	return nil, false
}

// get returns reference to chunkPayloadData with the given TSN value.
func (q *payloadQueue) get(tsn uint32) (*chunkPayloadData, bool) {
	c, ok := q.chunkMap[tsn]
	return c, ok
}

// popDuplicates returns an array of TSN values that were found duplicate.
func (q *payloadQueue) popDuplicates() []uint32 {
	dups := q.dupTSN
	q.dupTSN = []uint32{}
	return dups
}

func (q *payloadQueue) getGapAckBlocks(cumulativeTSN uint32) (gapAckBlocks []gapAckBlock) {
	var b gapAckBlock

	if len(q.chunkMap) == 0 {
		return []gapAckBlock{}
	}

	q.updateSortedKeys()

	for i, tsn := range q.sorted {
		if i == 0 {
			b.start = uint16(tsn - cumulativeTSN)
			b.end = b.start
			continue
		}
		diff := uint16(tsn - cumulativeTSN)
		if b.end+1 == diff {
			b.end++
		} else {
			gapAckBlocks = append(gapAckBlocks, gapAckBlock{
				start: b.start,
				end:   b.end,
			})
			b.start = diff
			b.end = diff
		}
	}

	gapAckBlocks = append(gapAckBlocks, gapAckBlock{
		start: b.start,
		end:   b.end,
	})

	return gapAckBlocks
}

func (q *payloadQueue) getGapAckBlocksString(cumulativeTSN uint32) string {
	gapAckBlocks := q.getGapAckBlocks(cumulativeTSN)
	str := fmt.Sprintf("cumTSN=%d", cumulativeTSN)
	for _, b := range gapAckBlocks {
		str += fmt.Sprintf(",%d-%d", b.start, b.end)
	}
	return str
}

func (q *payloadQueue) markAsAcked(tsn uint32) int {
	var nBytesAcked int
	if c, ok := q.chunkMap[tsn]; ok {
		c.acked = true
		c.retransmit = false
		nBytesAcked = len(c.userData)
		q.nBytes -= nBytesAcked
		c.userData = []byte{}
	}

	return nBytesAcked
}

func (q *payloadQueue) getLastTSNReceived() (uint32, bool) {
	q.updateSortedKeys()

	qlen := len(q.sorted)
	if qlen == 0 {
		return 0, false
	}
	return q.sorted[qlen-1], true
}

func (q *payloadQueue) markAllToRetrasmit() {
	for _, c := range q.chunkMap {
		if c.acked || c.abandoned() {
			continue
		}
		c.retransmit = true
	}
}

func (q *payloadQueue) getNumBytes() int {
	return q.nBytes
}

func (q *payloadQueue) size() int {
	return len(q.chunkMap)
}
