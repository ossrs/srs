// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"sync"
)

type chunkQueue struct {
	chunks       []Chunk
	maxSize      int // 0 or negative value: unlimited
	maxBytes     int // 0 or negative value: unlimited
	currentBytes int
	mutex        sync.RWMutex
}

func newChunkQueue(maxSize int, maxBytes int) *chunkQueue {
	return &chunkQueue{
		chunks:       []Chunk{},
		maxSize:      maxSize,
		maxBytes:     maxBytes,
		currentBytes: 0,
		mutex:        sync.RWMutex{},
	}
}

func (q *chunkQueue) push(c Chunk) bool {
	q.mutex.Lock()
	defer q.mutex.Unlock()

	if q.maxSize > 0 && len(q.chunks) >= q.maxSize {
		return false // dropped
	}
	if q.maxBytes > 0 && q.currentBytes+len(c.UserData()) >= q.maxBytes {
		return false
	}

	q.currentBytes += len(c.UserData())
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
	q.currentBytes -= len(c.UserData())

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
