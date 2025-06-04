// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package rfc8888 provides an interceptor that generates congestion control
// feedback reports as defined by RFC 8888.
package rfc8888

import (
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
)

// TickerFactory is a factory to create new tickers
type TickerFactory func(d time.Duration) ticker

// SenderInterceptorFactory is a interceptor.Factory for a SenderInterceptor
type SenderInterceptorFactory struct {
	opts []Option
}

// NewInterceptor constructs a new SenderInterceptor
func (s *SenderInterceptorFactory) NewInterceptor(_ string) (interceptor.Interceptor, error) {
	i := &SenderInterceptor{
		NoOp:          interceptor.NoOp{},
		log:           logging.NewDefaultLoggerFactory().NewLogger("rfc8888_interceptor"),
		lock:          sync.Mutex{},
		wg:            sync.WaitGroup{},
		recorder:      NewRecorder(),
		interval:      100 * time.Millisecond,
		maxReportSize: 1200,
		packetChan:    make(chan packet),
		newTicker: func(d time.Duration) ticker {
			return &timeTicker{time.NewTicker(d)}
		},
		now:   time.Now,
		close: make(chan struct{}),
	}
	for _, opt := range s.opts {
		err := opt(i)
		if err != nil {
			return nil, err
		}
	}
	return i, nil
}

// NewSenderInterceptor returns a new SenderInterceptorFactory configured with the given options.
func NewSenderInterceptor(opts ...Option) (*SenderInterceptorFactory, error) {
	return &SenderInterceptorFactory{opts: opts}, nil
}

// SenderInterceptor sends congestion control feedback as specified in RFC 8888.
type SenderInterceptor struct {
	interceptor.NoOp
	log           logging.LeveledLogger
	lock          sync.Mutex
	wg            sync.WaitGroup
	recorder      *Recorder
	interval      time.Duration
	maxReportSize int64
	packetChan    chan packet
	newTicker     TickerFactory
	now           func() time.Time
	close         chan struct{}
}

type packet struct {
	arrival        time.Time
	ssrc           uint32
	sequenceNumber uint16
	ecn            uint8
}

// BindRTCPWriter lets you modify any outgoing RTCP packets. It is called once per PeerConnection. The returned method
// will be called once per packet batch.
func (s *SenderInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	s.lock.Lock()
	defer s.lock.Unlock()

	if s.isClosed() {
		return writer
	}

	s.wg.Add(1)
	go s.loop(writer)

	return writer
}

// BindRemoteStream lets you modify any incoming RTP packets. It is called once for per RemoteStream. The returned method
// will be called once per rtp packet.
func (s *SenderInterceptor) BindRemoteStream(_ *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	return interceptor.RTPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		i, attr, err := reader.Read(b, a)
		if err != nil {
			return 0, nil, err
		}

		if attr == nil {
			attr = make(interceptor.Attributes)
		}
		header, err := attr.GetRTPHeader(b[:i])
		if err != nil {
			return 0, nil, err
		}

		p := packet{
			arrival:        s.now(),
			ssrc:           header.SSRC,
			sequenceNumber: header.SequenceNumber,
			ecn:            0, // ECN is not supported (yet).
		}
		s.packetChan <- p
		return i, attr, nil
	})
}

// Close closes the interceptor.
func (s *SenderInterceptor) Close() error {
	s.log.Trace("close")
	defer s.wg.Wait()

	if !s.isClosed() {
		close(s.close)
	}

	return nil
}

func (s *SenderInterceptor) isClosed() bool {
	select {
	case <-s.close:
		return true
	default:
		return false
	}
}

func (s *SenderInterceptor) loop(writer interceptor.RTCPWriter) {
	defer s.wg.Done()

	select {
	case <-s.close:
		return
	case pkt := <-s.packetChan:
		s.log.Tracef("got first packet: %v", pkt)
		s.recorder.AddPacket(pkt.arrival, pkt.ssrc, pkt.sequenceNumber, pkt.ecn)
	}

	s.log.Trace("start loop")
	t := s.newTicker(s.interval)
	for {
		select {
		case <-s.close:
			t.Stop()
			return

		case pkt := <-s.packetChan:
			s.log.Tracef("got packet: %v", pkt)
			s.recorder.AddPacket(pkt.arrival, pkt.ssrc, pkt.sequenceNumber, pkt.ecn)

		case now := <-t.Ch():
			s.log.Tracef("report triggered at %v", now)
			if writer == nil {
				s.log.Trace("no writer added, continue")
				continue
			}
			pkts := s.recorder.BuildReport(now, int(s.maxReportSize))
			if pkts == nil {
				continue
			}
			s.log.Tracef("got report: %v", pkts)
			if _, err := writer.Write([]rtcp.Packet{pkts}, nil); err != nil {
				s.log.Error(err.Error())
			}
		}
	}
}
