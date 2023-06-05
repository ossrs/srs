// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"io"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/rtp"
	"github.com/pion/srtp/v2"
)

// srtpWriterFuture blocks Read/Write calls until
// the SRTP Session is available
type srtpWriterFuture struct {
	ssrc           SSRC
	rtpSender      *RTPSender
	rtcpReadStream atomic.Value // *srtp.ReadStreamSRTCP
	rtpWriteStream atomic.Value // *srtp.WriteStreamSRTP
	mu             sync.Mutex
	closed         bool
}

func (s *srtpWriterFuture) init(returnWhenNoSRTP bool) error {
	if returnWhenNoSRTP {
		select {
		case <-s.rtpSender.stopCalled:
			return io.ErrClosedPipe
		case <-s.rtpSender.transport.srtpReady:
		default:
			return nil
		}
	} else {
		select {
		case <-s.rtpSender.stopCalled:
			return io.ErrClosedPipe
		case <-s.rtpSender.transport.srtpReady:
		}
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return io.ErrClosedPipe
	}

	srtcpSession, err := s.rtpSender.transport.getSRTCPSession()
	if err != nil {
		return err
	}

	rtcpReadStream, err := srtcpSession.OpenReadStream(uint32(s.ssrc))
	if err != nil {
		return err
	}

	srtpSession, err := s.rtpSender.transport.getSRTPSession()
	if err != nil {
		return err
	}

	rtpWriteStream, err := srtpSession.OpenWriteStream()
	if err != nil {
		return err
	}

	s.rtcpReadStream.Store(rtcpReadStream)
	s.rtpWriteStream.Store(rtpWriteStream)
	return nil
}

func (s *srtpWriterFuture) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return nil
	}
	s.closed = true

	if value, ok := s.rtcpReadStream.Load().(*srtp.ReadStreamSRTCP); ok {
		return value.Close()
	}

	return nil
}

func (s *srtpWriterFuture) Read(b []byte) (n int, err error) {
	if value, ok := s.rtcpReadStream.Load().(*srtp.ReadStreamSRTCP); ok {
		return value.Read(b)
	}

	if err := s.init(false); err != nil || s.rtcpReadStream.Load() == nil {
		return 0, err
	}

	return s.Read(b)
}

func (s *srtpWriterFuture) SetReadDeadline(t time.Time) error {
	if value, ok := s.rtcpReadStream.Load().(*srtp.ReadStreamSRTCP); ok {
		return value.SetReadDeadline(t)
	}

	if err := s.init(false); err != nil || s.rtcpReadStream.Load() == nil {
		return err
	}

	return s.SetReadDeadline(t)
}

func (s *srtpWriterFuture) WriteRTP(header *rtp.Header, payload []byte) (int, error) {
	if value, ok := s.rtpWriteStream.Load().(*srtp.WriteStreamSRTP); ok {
		return value.WriteRTP(header, payload)
	}

	if err := s.init(true); err != nil || s.rtpWriteStream.Load() == nil {
		return 0, err
	}

	return s.WriteRTP(header, payload)
}

func (s *srtpWriterFuture) Write(b []byte) (int, error) {
	if value, ok := s.rtpWriteStream.Load().(*srtp.WriteStreamSRTP); ok {
		return value.Write(b)
	}

	if err := s.init(true); err != nil || s.rtpWriteStream.Load() == nil {
		return 0, err
	}

	return s.Write(b)
}
