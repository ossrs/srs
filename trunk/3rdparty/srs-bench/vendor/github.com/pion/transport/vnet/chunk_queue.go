package vnet

import (
	"sync"
)

type chunkQueue struct {
	chunks  []Chunk
	maxSize int // 0 or negative value: unlimited
	mutex   sync.RWMutex
}

func newChunkQueue(maxSize int) *chunkQueue {
	return &chunkQueue{maxSize: maxSize}
}

func (q *chunkQueue) push(c Chunk) bool {
	q.mutex.Lock()
	defer q.mutex.Unlock()

	if q.maxSize > 0 && len(q.chunks) >= q.maxSize {
		return false // dropped
	}

	q.chunks = append(q.chunks, c)
	return true
}

func (q *chunkQueue) pop() (Chunk, bool) {
	q.mutex.Lock()
	defer q.mutex.Unlock()

	if len(q.chunks) == 0 {
		return nil, false
	}

	c := q.chunks[0]
	q.chunks = q.chunks[1:]

	return c, true
}

func (q *chunkQueue) peek() Chunk {
	q.mutex.RLock()
	defer q.mutex.RUnlock()

	if len(q.chunks) == 0 {
		return nil
	}

	return q.chunks[0]
}
