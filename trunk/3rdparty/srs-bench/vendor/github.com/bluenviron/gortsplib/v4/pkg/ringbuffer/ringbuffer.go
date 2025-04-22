// Package ringbuffer contains a ring buffer.
package ringbuffer

import (
	"fmt"
	"sync"
)

// RingBuffer is a ring buffer.
type RingBuffer struct {
	size       uint64
	mutex      sync.Mutex
	cond       *sync.Cond
	buffer     []interface{}
	readIndex  uint64
	writeIndex uint64
	closed     bool
}

// New allocates a RingBuffer.
func New(size uint64) (*RingBuffer, error) {
	// when writeIndex overflows, if size is not a power of
	// two, only a portion of the buffer is used.
	if (size & (size - 1)) != 0 {
		return nil, fmt.Errorf("size must be a power of two")
	}

	r := &RingBuffer{
		size:   size,
		buffer: make([]interface{}, size),
	}

	r.cond = sync.NewCond(&r.mutex)

	return r, nil
}

// Close makes Pull() return false.
func (r *RingBuffer) Close() {
	r.mutex.Lock()

	r.closed = true

	// discard pending data to make Pull() exit immediately
	for i := uint64(0); i < r.size; i++ {
		r.buffer[i] = nil
	}

	r.mutex.Unlock()
	r.cond.Broadcast()
}

// Reset restores Pull() behavior after a Close().
func (r *RingBuffer) Reset() {
	for i := uint64(0); i < r.size; i++ {
		r.buffer[i] = nil
	}

	r.writeIndex = 0
	r.readIndex = 0
	r.closed = false
}

// Push pushes data at the end of the buffer.
func (r *RingBuffer) Push(data interface{}) bool {
	r.mutex.Lock()

	if r.buffer[r.writeIndex] != nil {
		r.mutex.Unlock()
		return false
	}

	r.buffer[r.writeIndex] = data
	r.writeIndex = (r.writeIndex + 1) % r.size

	r.mutex.Unlock()

	r.cond.Broadcast()

	return true
}

// Pull pulls data from the beginning of the buffer.
func (r *RingBuffer) Pull() (interface{}, bool) {
	for {
		r.mutex.Lock()

		data := r.buffer[r.readIndex]

		if data != nil {
			r.buffer[r.readIndex] = nil
			r.readIndex = (r.readIndex + 1) % r.size
			r.mutex.Unlock()
			return data, true
		}

		if r.closed {
			r.mutex.Unlock()
			return nil, false
		}

		r.cond.Wait()

		r.mutex.Unlock()
	}
}
