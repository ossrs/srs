// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

type payloadQueue struct {
	chunks *queue[*chunkPayloadData]
	nBytes int
}

func newPayloadQueue() *payloadQueue {
	return &payloadQueue{chunks: newQueue[*chunkPayloadData](128)}
}

func (q *payloadQueue) pushNoCheck(p *chunkPayloadData) {
	q.chunks.PushBack(p)
	q.nBytes += len(p.userData)
}

// pop pops only if the oldest chunk's TSN matches the given TSN.
func (q *payloadQueue) pop(tsn uint32) (*chunkPayloadData, bool) {
	if q.chunks.Len() > 0 && tsn == q.chunks.Front().tsn {
		c := q.chunks.PopFront()
		q.nBytes -= len(c.userData)

		return c, true
	}

	return nil, false
}

// get returns reference to chunkPayloadData with the given TSN value.
func (q *payloadQueue) get(tsn uint32) (*chunkPayloadData, bool) {
	length := q.chunks.Len()
	if length == 0 {
		return nil, false
	}
	head := q.chunks.Front().tsn
	if tsn < head || int(tsn-head) >= length {
		return nil, false
	}

	return q.chunks.At(int(tsn - head)), true
}

func (q *payloadQueue) markAsAcked(tsn uint32) int {
	var nBytesAcked int
	if c, ok := q.get(tsn); ok {
		c.acked = true
		c.retransmit = false
		nBytesAcked = len(c.userData)
		q.nBytes -= nBytesAcked
		c.userData = []byte{}
	}

	return nBytesAcked
}

func (q *payloadQueue) markAllToRetrasmit() {
	for i := 0; i < q.chunks.Len(); i++ {
		c := q.chunks.At(i)
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
	return q.chunks.Len()
}
