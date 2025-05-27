// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"errors"
	"io"
	"sync"
	"time"

	"github.com/pion/rtp"
	"github.com/pion/transport/v3/packetio"
)

// Limit the buffer size to 1MB
const srtpBufferSize = 1000 * 1000

// ReadStreamSRTP handles decryption for a single RTP SSRC
type ReadStreamSRTP struct {
	mu sync.Mutex

	isClosed chan bool

	session  *SessionSRTP
	ssrc     uint32
	isInited bool

	buffer io.ReadWriteCloser
}

// Used by getOrCreateReadStream
func newReadStreamSRTP() readStream {
	return &ReadStreamSRTP{}
}

func (r *ReadStreamSRTP) init(child streamSession, ssrc uint32) error {
	sessionSRTP, ok := child.(*SessionSRTP)

	r.mu.Lock()
	defer r.mu.Unlock()

	if !ok {
		return errFailedTypeAssertion
	} else if r.isInited {
		return errStreamAlreadyInited
	}

	r.session = sessionSRTP
	r.ssrc = ssrc
	r.isInited = true
	r.isClosed = make(chan bool)

	// Create a buffer with a 1MB limit
	if r.session.bufferFactory != nil {
		r.buffer = r.session.bufferFactory(packetio.RTPBufferPacket, ssrc)
	} else {
		buff := packetio.NewBuffer()
		buff.SetLimitSize(srtpBufferSize)
		r.buffer = buff
	}

	return nil
}

func (r *ReadStreamSRTP) write(buf []byte) (n int, err error) {
	n, err = r.buffer.Write(buf)

	if errors.Is(err, packetio.ErrFull) {
		// Silently drop data when the buffer is full.
		return len(buf), nil
	}

	return n, err
}

// Read reads and decrypts full RTP packet from the nextConn
func (r *ReadStreamSRTP) Read(buf []byte) (int, error) {
	return r.buffer.Read(buf)
}

// ReadRTP reads and decrypts full RTP packet and its header from the nextConn
func (r *ReadStreamSRTP) ReadRTP(buf []byte) (int, *rtp.Header, error) {
	n, err := r.Read(buf)
	if err != nil {
		return 0, nil, err
	}

	header := &rtp.Header{}

	_, err = header.Unmarshal(buf[:n])
	if err != nil {
		return 0, nil, err
	}

	return n, header, nil
}

// SetReadDeadline sets the deadline for the Read operation.
// Setting to zero means no deadline.
func (r *ReadStreamSRTP) SetReadDeadline(t time.Time) error {
	if b, ok := r.buffer.(interface {
		SetReadDeadline(time.Time) error
	}); ok {
		return b.SetReadDeadline(t)
	}
	return nil
}

// Close removes the ReadStream from the session and cleans up any associated state
func (r *ReadStreamSRTP) Close() error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if !r.isInited {
		return errStreamNotInited
	}

	select {
	case <-r.isClosed:
		return errStreamAlreadyClosed
	default:
		err := r.buffer.Close()
		if err != nil {
			return err
		}

		r.session.removeReadStream(r.ssrc)
		return nil
	}
}

// GetSSRC returns the SSRC we are demuxing for
func (r *ReadStreamSRTP) GetSSRC() uint32 {
	return r.ssrc
}

// WriteStreamSRTP is stream for a single Session that is used to encrypt RTP
type WriteStreamSRTP struct {
	session *SessionSRTP
}

// WriteRTP encrypts a RTP packet and writes to the connection
func (w *WriteStreamSRTP) WriteRTP(header *rtp.Header, payload []byte) (int, error) {
	return w.session.writeRTP(header, payload)
}

// Write encrypts and writes a full RTP packets to the nextConn
func (w *WriteStreamSRTP) Write(b []byte) (int, error) {
	return w.session.write(b)
}

// SetWriteDeadline sets the deadline for the Write operation.
// Setting to zero means no deadline.
func (w *WriteStreamSRTP) SetWriteDeadline(t time.Time) error {
	return w.session.setWriteDeadline(t)
}
