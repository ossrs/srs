// +build !js

package webrtc

import (
	"io"
	"sync/atomic"
	"time"

	"github.com/pion/rtp"
	"github.com/pion/srtp/v2"
)

// srtpWriterFuture blocks Read/Write calls until
// the SRTP Session is available
type srtpWriterFuture struct {
	rtpSender      *RTPSender
	rtcpReadStream atomic.Value // *srtp.ReadStreamSRTCP
	rtpWriteStream atomic.Value // *srtp.WriteStreamSRTP
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

	srtcpSession, err := s.rtpSender.transport.getSRTCPSession()
	if err != nil {
		return err
	}

	rtcpReadStream, err := srtcpSession.OpenReadStream(uint32(s.rtpSender.ssrc))
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
	if value := s.rtcpReadStream.Load(); value != nil {
		return value.(*srtp.ReadStreamSRTCP).Close()
	}

	return nil
}

func (s *srtpWriterFuture) Read(b []byte) (n int, err error) {
	if value := s.rtcpReadStream.Load(); value != nil {
		return value.(*srtp.ReadStreamSRTCP).Read(b)
	}

	if err := s.init(false); err != nil || s.rtcpReadStream.Load() == nil {
		return 0, err
	}

	return s.Read(b)
}

func (s *srtpWriterFuture) SetReadDeadline(t time.Time) error {
	if value := s.rtcpReadStream.Load(); value != nil {
		return value.(*srtp.ReadStreamSRTCP).SetReadDeadline(t)
	}

	if err := s.init(false); err != nil || s.rtcpReadStream.Load() == nil {
		return err
	}

	return s.SetReadDeadline(t)
}

func (s *srtpWriterFuture) WriteRTP(header *rtp.Header, payload []byte) (int, error) {
	if value := s.rtpWriteStream.Load(); value != nil {
		return value.(*srtp.WriteStreamSRTP).WriteRTP(header, payload)
	}

	if err := s.init(true); err != nil || s.rtpWriteStream.Load() == nil {
		return 0, err
	}

	return s.WriteRTP(header, payload)
}

func (s *srtpWriterFuture) Write(b []byte) (int, error) {
	if value := s.rtpWriteStream.Load(); value != nil {
		return value.(*srtp.WriteStreamSRTP).Write(b)
	}

	if err := s.init(true); err != nil || s.rtpWriteStream.Load() == nil {
		return 0, err
	}

	return s.Write(b)
}
