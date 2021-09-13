package report

import (
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

func ntpTime(t time.Time) uint64 {
	// seconds since 1st January 1900
	s := (float64(t.UnixNano()) / 1000000000) + 2208988800

	// higher 32 bits are the integer part, lower 32 bits are the fractional part
	integerPart := uint32(s)
	fractionalPart := uint32((s - float64(integerPart)) * 0xFFFFFFFF)
	return uint64(integerPart)<<32 | uint64(fractionalPart)
}

// SenderInterceptor interceptor generates sender reports.
type SenderInterceptor struct {
	interceptor.NoOp
	interval time.Duration
	now      func() time.Time
	streams  sync.Map
	log      logging.LeveledLogger
	m        sync.Mutex
	wg       sync.WaitGroup
	close    chan struct{}
}

// NewSenderInterceptor returns a new SenderInterceptor interceptor.
func NewSenderInterceptor(opts ...SenderOption) (*SenderInterceptor, error) {
	s := &SenderInterceptor{
		interval: 1 * time.Second,
		now:      time.Now,
		log:      logging.NewDefaultLoggerFactory().NewLogger("sender_interceptor"),
		close:    make(chan struct{}),
	}

	for _, opt := range opts {
		if err := opt(s); err != nil {
			return nil, err
		}
	}

	return s, nil
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

	ticker := time.NewTicker(s.interval)
	for {
		select {
		case <-ticker.C:
			now := s.now()
			s.streams.Range(func(key, value interface{}) bool {
				ssrc := key.(uint32)
				stream := value.(*senderStream)

				stream.m.Lock()
				defer stream.m.Unlock()

				sr := &rtcp.SenderReport{
					SSRC:        ssrc,
					NTPTime:     ntpTime(now),
					RTPTime:     stream.lastRTPTimeRTP + uint32(now.Sub(stream.lastRTPTimeTime).Seconds()*stream.clockRate),
					PacketCount: stream.packetCount,
					OctetCount:  stream.octetCount,
				}

				if _, err := rtcpWriter.Write([]rtcp.Packet{sr}, interceptor.Attributes{}); err != nil {
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
	stream := newSenderStream(info.ClockRate)
	s.streams.Store(info.SSRC, stream)

	return interceptor.RTPWriterFunc(func(header *rtp.Header, payload []byte, a interceptor.Attributes) (int, error) {
		stream.processRTP(s.now(), header, payload)

		return writer.Write(header, payload, a)
	})
}
