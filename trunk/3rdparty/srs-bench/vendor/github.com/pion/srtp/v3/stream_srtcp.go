// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import (
	"errors"
	"io"
	"sync"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/transport/v3/packetio"
)

// Limit the buffer size to 100KB
const srtcpBufferSize = 100 * 1000

// ReadStreamSRTCP handles decryption for a single RTCP SSRC
type ReadStreamSRTCP struct {
	mu sync.Mutex

	isClosed chan bool

	session  *SessionSRTCP
	ssrc     uint32
	isInited bool

	buffer io.ReadWriteCloser
}

func (r *ReadStreamSRTCP) write(buf []byte) (n int, err error) {
	n, err = r.buffer.Write(buf)

	if errors.Is(err, packetio.ErrFull) {
		// Silently drop data when the buffer is full.
		return len(buf), nil
	}

	return n, err
}

// Used by getOrCreateReadStream
func newReadStreamSRTCP() readStream {
	return &ReadStreamSRTCP{}
}

// ReadRTCP reads and decrypts full RTCP packet and its header from the nextConn
func (r *ReadStreamSRTCP) ReadRTCP(buf []byte) (int, *rtcp.Header, error) {
	n, err := r.Read(buf)
	if err != nil {
		return 0, nil, err
	}

	header := &rtcp.Header{}
	err = header.Unmarshal(buf[:n])
	if err != nil {
		return 0, nil, err
	}

	return n, header, nil
}

// Read reads and decrypts full RTCP packet from the nextConn
func (r *ReadStreamSRTCP) Read(buf []byte) (int, error) {
	return r.buffer.Read(buf)
}

// SetReadDeadline sets the deadline for the Read operation.
// Setting to zero means no deadline.
func (r *ReadStreamSRTCP) SetReadDeadline(t time.Time) error {
	if b, ok := r.buffer.(interface {
		SetReadDeadline(time.Time) error
	}); ok {
		return b.SetReadDeadline(t)
	}
	return nil
}

// Close removes the ReadStream from the session and cleans up any associated state
func (r *ReadStreamSRTCP) Close() error {
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

func (r *ReadStreamSRTCP) init(child streamSession, ssrc uint32) error {
	sessionSRTCP, ok := child.(*SessionSRTCP)

	r.mu.Lock()
	defer r.mu.Unlock()
	if !ok {
		return errFailedTypeAssertion
	} else if r.isInited {
		return errStreamAlreadyInited
	}

	r.session = sessionSRTCP
	r.ssrc = ssrc
	r.isInited = true
	r.isClosed = make(chan bool)

	if r.session.bufferFactory != nil {
		r.buffer = r.session.bufferFactory(packetio.RTCPBufferPacket, ssrc)
	} else {
		// Create a buffer and limit it to 100KB
		buff := packetio.NewBuffer()
		buff.SetLimitSize(srtcpBufferSize)
		r.buffer = buff
	}

	return nil
}

// GetSSRC returns the SSRC we are demuxing for
func (r *ReadStreamSRTCP) GetSSRC() uint32 {
	return r.ssrc
}

// WriteStreamSRTCP is stream for a single Session that is used to encrypt RTCP
type WriteStreamSRTCP struct {
	session *SessionSRTCP
}

// WriteRTCP encrypts a RTCP header and its payload to the nextConn
func (w *WriteStreamSRTCP) WriteRTCP(header *rtcp.Header, payload []byte) (int, error) {
	headerRaw, err := header.Marshal()
	if err != nil {
		return 0, err
	}

	return w.session.write(append(headerRaw, payload...))
}

// Write encrypts and writes a full RTCP packets to the nextConn
func (w *WriteStreamSRTCP) Write(b []byte) (int, error) {
	return w.session.write(b)
}

// SetWriteDeadline sets the deadline for the Write operation.
// Setting to zero means no deadline.
func (w *WriteStreamSRTCP) SetWriteDeadline(t time.Time) error {
	return w.session.setWriteDeadline(t)
}
