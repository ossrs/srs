// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package net implements DTLS specific networking primitives.
// NOTE: this package is an adaption of pion/transport/packetio that allows for
// storing a remote address alongside each packet in the buffer and implements
// relevant methods of net.PacketConn. If possible, the updates made in this
// repository will be reflected back upstream. If not, it is likely that this
// will be moved to a public package in this repository.
//
// This package was migrated from pion/transport/packetio at
// https://github.com/pion/transport/commit/6890c795c807a617c054149eee40a69d7fdfbfdb
package net

import (
	"bytes"
	"errors"
	"io"
	"net"
	"sync"
	"time"

	"github.com/pion/transport/v3/deadline"
)

// ErrTimeout indicates that deadline was reached before operation could be
// completed.
var ErrTimeout = errors.New("buffer: i/o timeout")

// AddrPacket is a packet payload and the associated remote address from which
// it was received.
type AddrPacket struct {
	addr net.Addr
	data bytes.Buffer
}

// PacketBuffer is a circular buffer for network packets. Each slot in the
// buffer contains the remote address from which the packet was received, as
// well as the packet data.
type PacketBuffer struct {
	mutex sync.Mutex

	packets     []AddrPacket
	write, read int

	// full indicates whether the buffer is full, which is needed to distinguish
	// when the write pointer and read pointer are at the same index.
	full bool

	notify chan struct{}
	closed bool

	readDeadline *deadline.Deadline
}

// NewPacketBuffer creates a new PacketBuffer.
func NewPacketBuffer() *PacketBuffer {
	return &PacketBuffer{
		readDeadline: deadline.New(),
		// In the narrow context in which this package is currently used, there
		// will always be at least one packet written to the buffer. Therefore,
		// we opt to allocate with size of 1 during construction, rather than
		// waiting until that first packet is written.
		packets: make([]AddrPacket, 1),
		full:    false,
	}
}

// WriteTo writes a single packet to the buffer. The supplied address will
// remain associated with the packet.
func (b *PacketBuffer) WriteTo(pkt []byte, addr net.Addr) (int, error) {
	b.mutex.Lock()

	if b.closed {
		b.mutex.Unlock()

		return 0, io.ErrClosedPipe
	}

	var notify chan struct{}
	if b.notify != nil {
		notify = b.notify
		b.notify = nil
	}

	// Check to see if we are full.
	if b.full {
		// If so, grow AddrPacket buffer.
		var newSize int
		if len(b.packets) < 128 {
			// Double the number of packets.
			newSize = len(b.packets) * 2
		} else {
			// Increase the number of packets by 25%.
			newSize = 5 * len(b.packets) / 4
		}
		newBuf := make([]AddrPacket, newSize)
		var n int
		if b.read < b.write {
			n = copy(newBuf, b.packets[b.read:b.write])
		} else {
			n = copy(newBuf, b.packets[b.read:])
			n += copy(newBuf[n:], b.packets[:b.write])
		}

		b.packets = newBuf

		// Update write pointer to point to new location and mark buffer as not
		// full.
		b.write = n
		b.full = false
	}

	// Store the packet at the write pointer.
	packet := &b.packets[b.write]
	packet.data.Reset()
	n, err := packet.data.Write(pkt)
	if err != nil {
		b.mutex.Unlock()

		return n, err
	}
	packet.addr = addr

	// Increment write pointer.
	b.write++

	// If the write pointer is equal to the length of the buffer, wrap around.
	if len(b.packets) == b.write {
		b.write = 0
	}

	// If a write resulted in making write and read pointers equivalent, then we
	// are full.
	if b.write == b.read {
		b.full = true
	}

	b.mutex.Unlock()

	if notify != nil {
		close(notify)
	}

	return n, nil
}

// ReadFrom reads a single packet from the buffer, or blocks until one is
// available.
func (b *PacketBuffer) ReadFrom(packet []byte) (n int, addr net.Addr, err error) { //nolint:cyclop
	select {
	case <-b.readDeadline.Done():
		return 0, nil, ErrTimeout
	default:
	}

	for {
		b.mutex.Lock()

		if b.read != b.write || b.full {
			ap := b.packets[b.read]
			if len(packet) < ap.data.Len() {
				b.mutex.Unlock()

				return 0, nil, io.ErrShortBuffer
			}

			// Copy packet data from buffer.
			n, err := ap.data.Read(packet)
			if err != nil {
				b.mutex.Unlock()

				return n, nil, err
			}

			// Advance read pointer.
			b.read++
			if len(b.packets) == b.read {
				b.read = 0
			}

			// If we were full before reading and have successfully read, we are
			// no longer full.
			if b.full {
				b.full = false
			}

			b.mutex.Unlock()

			return n, ap.addr, nil
		}

		if b.closed {
			b.mutex.Unlock()

			return 0, nil, io.EOF
		}

		if b.notify == nil {
			b.notify = make(chan struct{})
		}
		notify := b.notify
		b.mutex.Unlock()

		select {
		case <-b.readDeadline.Done():
			return 0, nil, ErrTimeout
		case <-notify:
		}
	}
}

// Close closes the buffer, allowing unread packets to be read, but erroring on
// any new writes.
func (b *PacketBuffer) Close() (err error) {
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

// SetReadDeadline sets the read deadline for the buffer.
func (b *PacketBuffer) SetReadDeadline(t time.Time) error {
	b.readDeadline.Set(t)

	return nil
}
