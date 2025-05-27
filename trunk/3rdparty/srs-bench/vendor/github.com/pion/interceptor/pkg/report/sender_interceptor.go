// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package report

import (
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// TickerFactory is a factory to create new tickers
type TickerFactory func(d time.Duration) Ticker

// SenderInterceptorFactory is a interceptor.Factory for a SenderInterceptor
type SenderInterceptorFactory struct {
	opts []SenderOption
}

// NewInterceptor constructs a new SenderInterceptor
func (s *SenderInterceptorFactory) NewInterceptor(_ string) (interceptor.Interceptor, error) {
	i := &SenderInterceptor{
		interval: 1 * time.Second,
		now:      time.Now,
		newTicker: func(d time.Duration) Ticker {
			return &timeTicker{time.NewTicker(d)}
		},
		log:   logging.NewDefaultLoggerFactory().NewLogger("sender_interceptor"),
		close: make(chan struct{}),
	}

	for _, opt := range s.opts {
		if err := opt(i); err != nil {
			return nil, err
		}
	}

	return i, nil
}

// NewSenderInterceptor returns a new SenderInterceptorFactory
func NewSenderInterceptor(opts ...SenderOption) (*SenderInterceptorFactory, error) {
	return &SenderInterceptorFactory{opts}, nil
}

// SenderInterceptor interceptor generates sender reports.
type SenderInterceptor struct {
	interceptor.NoOp
	interval  time.Duration
	now       func() time.Time
	newTicker TickerFactory
	streams   sync.Map
	log       logging.LeveledLogger
	m         sync.Mutex
	wg        sync.WaitGroup
	close     chan struct{}
	started   chan struct{}

	useLatestPacket bool
}

func (s *SenderInterceptor) isClosed() bool {
	select {
	case <-s.close:
		return true
	default:
		return false
	}
}

// Close closes the interceptor.
func (s *SenderInterceptor) Close() error {
	defer s.wg.Wait()
	s.m.Lock()
	defer s.m.Unlock()

	if !s.isClosed() {
		close(s.close)
	}

	return nil
}

// BindRTCPWriter lets you modify any outgoing RTCP packets. It is called once per PeerConnection. The returned method
// will be called once per packet batch.
func (s *SenderInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	s.m.Lock()
	defer s.m.Unlock()

	if s.isClosed() {
		return writer
	}

	s.wg.Add(1)

	go s.loop(writer)

	return writer
}

func (s *SenderInterceptor) loop(rtcpWriter interceptor.RTCPWriter) {
	defer s.wg.Done()

	ticker := s.newTicker(s.interval)
	defer ticker.Stop()
	if s.started != nil {
		// This lets us synchronize in tests to know whether the loop has begun or not.
		// It only happens if started was initialized, which should not occur in non-tests.
		close(s.started)
	}
	for {
		select {
		case <-ticker.Ch():
			now := s.now()
			s.streams.Range(func(_, value interface{}) bool {
				if stream, ok := value.(*senderStream); !ok {
					s.log.Warnf("failed to cast SenderInterceptor stream")
				} else if _, err := rtcpWriter.Write([]rtcp.Packet{stream.generateReport(now)}, interceptor.Attributes{}); err != nil {
					s.log.Warnf("failed sending: %+v", err)
				}

				return true
			})

		case <-s.close:
			return
		}
	}
}

// BindLocalStream lets you modify any outgoing RTP packets. It is called once for per LocalStream. The returned method
// will be called once per rtp packet.
func (s *SenderInterceptor) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	stream := newSenderStream(info.SSRC, info.ClockRate, s.useLatestPacket)
	s.streams.Store(info.SSRC, stream)

	return interceptor.RTPWriterFunc(func(header *rtp.Header, payload []byte, a interceptor.Attributes) (int, error) {
		stream.processRTP(s.now(), header, payload)

		return writer.Write(header, payload, a)
	})
}

// UnbindLocalStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (s *SenderInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
	s.streams.Delete(info.SSRC)
}
