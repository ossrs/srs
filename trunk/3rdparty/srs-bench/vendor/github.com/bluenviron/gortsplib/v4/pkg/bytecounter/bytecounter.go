// Package bytecounter contains a io.ReadWriter wrapper that allows to count read and written bytes.
package bytecounter

import (
	"io"
	"sync/atomic"
)

// ByteCounter is a io.ReadWriter wrapper that allows to count read and written bytes.
type ByteCounter struct {
	rw       io.ReadWriter
	received *uint64
	sent     *uint64
}

// New allocates a ByteCounter.
func New(rw io.ReadWriter, received *uint64, sent *uint64) *ByteCounter {
	if received == nil {
		received = new(uint64)
	}
	if sent == nil {
		sent = new(uint64)
	}

	return &ByteCounter{
		rw:       rw,
		received: received,
		sent:     sent,
	}
}

// Read implements io.ReadWriter.
func (bc *ByteCounter) Read(p []byte) (int, error) {
	n, err := bc.rw.Read(p)
	atomic.AddUint64(bc.received, uint64(n))
	return n, err
}

// Write implements io.ReadWriter.
func (bc *ByteCounter) Write(p []byte) (int, error) {
	n, err := bc.rw.Write(p)
	atomic.AddUint64(bc.sent, uint64(n))
	return n, err
}

// BytesReceived returns the number of bytes received.
func (bc *ByteCounter) BytesReceived() uint64 {
	return atomic.LoadUint64(bc.received)
}

// BytesSent returns the number of bytes sent.
func (bc *ByteCounter) BytesSent() uint64 {
	return atomic.LoadUint64(bc.sent)
}
