// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package packetio provides packet buffer
package packetio

import (
	"errors"
	"io"
	"sync"
	"time"

	"github.com/pion/transport/v2/deadline"
)

var errPacketTooBig = errors.New("packet too big")

// BufferPacketType allow the Buffer to know which packet protocol is writing.
type BufferPacketType int

const (
	// RTPBufferPacket indicates the Buffer that is handling RTP packets
	RTPBufferPacket BufferPacketType = 1
	// RTCPBufferPacket indicates the Buffer that is handling RTCP packets
	RTCPBufferPacket BufferPacketType = 2
)

// Buffer allows writing packets to an intermediate buffer, which can then be read form.
// This is verify similar to bytes.Buffer but avoids combining multiple writes into a single read.
type Buffer struct {
	mutex sync.Mutex

	// this is a circular buffer.  If head <= tail, then the useful
	// data is in the interval [head, tail[.  If tail < head, then
	// the useful data is the union of [head, len[ and [0, tail[.
	// In order to avoid ambiguity when head = tail, we always leave
	// an unused byte in the buffer.
	data       []byte
	head, tail int

	notify chan struct{} // non-nil when we have blocked readers
	closed bool

	count                 int
	limitCount, limitSize int

	readDeadline *deadline.Deadline
}

const (
	minSize    = 2048
	cutoffSize = 128 * 1024
	maxSize    = 4 * 1024 * 1024
)

// NewBuffer creates a new Buffer.
func NewBuffer() *Buffer {
	return &Buffer{
		readDeadline: deadline.New(),
	}
}

// available returns true if the buffer is large enough to fit a packet
// of the given size, taking overhead into account.
func (b *Buffer) available(size int) bool {
	available := b.head - b.tail
	if available <= 0 {
		available += len(b.data)
	}
	// we interpret head=tail as empty, so always keep a byte free
	if size+2+1 > available {
		return false
	}

	return true
}

// grow increases the size of the buffer.  If it returns nil, then the
// buffer has been grown.  It returns ErrFull if hits a limit.
func (b *Buffer) grow() error {
	var newSize int
	if len(b.data) < cutoffSize {
		newSize = 2 * len(b.data)
	} else {
		newSize = 5 * len(b.data) / 4
	}
	if newSize < minSize {
		newSize = minSize
	}
	if (b.limitSize <= 0 || sizeHardLimit) && newSize > maxSize {
		newSize = maxSize
	}

	// one byte slack
	if b.limitSize > 0 && newSize > b.limitSize+1 {
		newSize = b.limitSize + 1
	}

	if newSize <= len(b.data) {
		return ErrFull
	}

	newData := make([]byte, newSize)

	var n int
	if b.head <= b.tail {
		// data was contiguous
		n = copy(newData, b.data[b.head:b.tail])
	} else {
		// data was discontinuous
		n = copy(newData, b.data[b.head:])
		n += copy(newData[n:], b.data[:b.tail])
	}
	b.head = 0
	b.tail = n
	b.data = newData

	return nil
}

// Write appends a copy of the packet data to the buffer.
// Returns ErrFull if the packet doesn't fit.
//
// Note that the packet size is limited to 65536 bytes since v0.11.0 due to the internal data structure.
func (b *Buffer) Write(packet []byte) (int, error) {
	if len(packet) >= 0x10000 {
		return 0, errPacketTooBig
	}

	b.mutex.Lock()

	if b.closed {
		b.mutex.Unlock()
		return 0, io.ErrClosedPipe
	}

	if (b.limitCount > 0 && b.count >= b.limitCount) ||
		(b.limitSize > 0 && b.size()+2+len(packet) > b.limitSize) {
		b.mutex.Unlock()
		return 0, ErrFull
	}

	// grow the buffer until the packet fits
	for !b.available(len(packet)) {
		err := b.grow()
		if err != nil {
			b.mutex.Unlock()
			return 0, err
		}
	}

	var notify chan struct{}
	if b.notify != nil {
		// Prepare to notify readers, but only
		// actually do it after we release the lock.
		notify = b.notify
		b.notify = nil
	}

	// store the length of the packet
	b.data[b.tail] = uint8(len(packet) >> 8)
	b.tail++
	if b.tail >= len(b.data) {
		b.tail = 0
	}
	b.data[b.tail] = uint8(len(packet))
	b.tail++
	if b.tail >= len(b.data) {
		b.tail = 0
	}

	// store the packet
	n := copy(b.data[b.tail:], packet)
	b.tail += n
	if b.tail >= len(b.data) {
		// we reached the end, wrap around
		m := copy(b.data, packet[n:])
		b.tail = m
	}
	b.count++
	b.mutex.Unlock()

	if notify != nil {
		close(notify)
	}

	return len(packet), nil
}

// Read populates the given byte slice, returning the number of bytes read.
// Blocks until data is available or the buffer is closed.
// Returns io.ErrShortBuffer is the packet is too small to copy the Write.
// Returns io.EOF if the buffer is closed.
func (b *Buffer) Read(packet []byte) (n int, err error) { //nolint:gocognit
	// Return immediately if the deadline is already exceeded.
	select {
	case <-b.readDeadline.Done():
		return 0, &netError{ErrTimeout, true, true}
	default:
	}

	for {
		b.mutex.Lock()

		if b.head != b.tail {
			// decode the packet size
			n1 := b.data[b.head]
			b.head++
			if b.head >= len(b.data) {
				b.head = 0
			}
			n2 := b.data[b.head]
			b.head++
			if b.head >= len(b.data) {
				b.head = 0
			}
			count := int((uint16(n1) << 8) | uint16(n2))

			// determine the number of bytes we'll actually copy
			copied := count
			if copied > len(packet) {
				copied = len(packet)
			}

			// copy the data
			if b.head+copied < len(b.data) {
				copy(packet, b.data[b.head:b.head+copied])
			} else {
				k := copy(packet, b.data[b.head:])
				copy(packet[k:], b.data[:copied-k])
			}

			// advance head, discarding any data that wasn't copied
			b.head += count
			if b.head >= len(b.data) {
				b.head -= len(b.data)
			}

			if b.head == b.tail {
				// the buffer is empty, reset to beginning
				// in order to improve cache locality.
				b.head = 0
				b.tail = 0
			}

			b.count--

			b.mutex.Unlock()

			if copied < count {
				return copied, io.ErrShortBuffer
			}
			return copied, nil
		}

		if b.closed {
			b.mutex.Unlock()
			return 0, io.EOF
		}

		if b.notify == nil {
			b.notify = make(chan struct{})
		}
		notify := b.notify
		b.mutex.Unlock()

		select {
		case <-b.readDeadline.Done():
			return 0, &netError{ErrTimeout, true, true}
		case <-notify:
		}
	}
}

// Close the buffer, unblocking any pending reads.
// Data in the buffer can still be read, Read will return io.EOF only when empty.
func (b *Buffer) Close() (err error) {
	b.mutex.Lock()

	if b.closed {
		b.mutex.Unlock()
		return nil
	}

	notify := b.notify
	b.notify = nil
	b.closed = true

	b.mutex.Unlock()

	if notify != nil {
		close(notify)
	}

	return nil
}

// Count returns the number of packets in the buffer.
func (b *Buffer) Count() int {
	b.mutex.Lock()
	defer b.mutex.Unlock()
	return b.count
}

// SetLimitCount controls the maximum number of packets that can be buffered.
// Causes Write to return ErrFull when this limit is reached.
// A zero value will disable this limit.
func (b *Buffer) SetLimitCount(limit int) {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	b.limitCount = limit
}

// Size returns the total byte size of packets in the buffer, including
// a small amount of administrative overhead.
func (b *Buffer) Size() int {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	return b.size()
}

func (b *Buffer) size() int {
	size := b.tail - b.head
	if size < 0 {
		size += len(b.data)
	}
	return size
}

// SetLimitSize controls the maximum number of bytes that can be buffered.
// Causes Write to return ErrFull when this limit is reached.
// A zero value means 4MB since v0.11.0.
//
// User can set packetioSizeHardLimit build tag to enable 4MB hard limit.
// When packetioSizeHardLimit build tag is set, SetLimitSize exceeding
// the hard limit will be silently discarded.
func (b *Buffer) SetLimitSize(limit int) {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	b.limitSize = limit
}

// SetReadDeadline sets the deadline for the Read operation.
// Setting to zero means no deadline.
func (b *Buffer) SetReadDeadline(t time.Time) error {
	b.readDeadline.Set(t)
	return nil
}
