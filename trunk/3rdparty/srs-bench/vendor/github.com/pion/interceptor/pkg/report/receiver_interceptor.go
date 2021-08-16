package report

import (
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// ReceiverInterceptor interceptor generates receiver reports.
type ReceiverInterceptor struct {
	interceptor.NoOp
	interval time.Duration
	now      func() time.Time
	streams  sync.Map
	log      logging.LeveledLogger
	m        sync.Mutex
	wg       sync.WaitGroup
	close    chan struct{}
}

// NewReceiverInterceptor returns a new ReceiverInterceptor interceptor.
func NewReceiverInterceptor(opts ...ReceiverOption) (*ReceiverInterceptor, error) {
	r := &ReceiverInterceptor{
		interval: 1 * time.Second,
		now:      time.Now,
		log:      logging.NewDefaultLoggerFactory().NewLogger("receiver_interceptor"),
		close:    make(chan struct{}),
	}

	for _, opt := range opts {
		if err := opt(r); err != nil {
			return nil, err
		}
	}

	return r, nil
}

func (r *ReceiverInterceptor) isClosed() bool {
	select {
	case <-r.close:
		return true
	default:
		return false
	}
}

// Close closes the interceptor.
func (r *ReceiverInterceptor) Close() error {
	defer r.wg.Wait()
	r.m.Lock()
	defer r.m.Unlock()

	if !r.isClosed() {
		close(r.close)
	}

	return nil
}

// BindRTCPWriter lets you modify any outgoing RTCP packets. It is called once per PeerConnection. The returned method
// will be called once per packet batch.
func (r *ReceiverInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	r.m.Lock()
	defer r.m.Unlock()

	if r.isClosed() {
		return writer
	}

	r.wg.Add(1)

	go r.loop(writer)

	return writer
}

func (r *ReceiverInterceptor) loop(rtcpWriter interceptor.RTCPWriter) {
	defer r.wg.Done()

	ticker := time.NewTicker(r.interval)
	for {
		select {
		case <-ticker.C:
			now := r.now()
			r.streams.Range(func(key, value interface{}) bool {
				stream := value.(*receiverStream)

				var pkts []rtcp.Packet

				pkts = append(pkts, stream.generateReport(now))

				if _, err := rtcpWriter.Write(pkts, interceptor.Attributes{}); err != nil {
					r.log.Warnf("failed sending: %+v", err)
				}

				return true
			})

		case <-r.close:
			return
		}
	}
}

// BindRemoteStream lets you modify any incoming RTP packets. It is called once for per RemoteStream. The returned method
// will be called once per rtp packet.
func (r *ReceiverInterceptor) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	stream := newReceiverStream(info.SSRC, info.ClockRate)
	r.streams.Store(info.SSRC, stream)

	return interceptor.RTPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		i, attr, err := reader.Read(b, a)
		if err != nil {
			return 0, nil, err
		}

		pkt := rtp.Packet{}
		if err = pkt.Unmarshal(b[:i]); err != nil {
			return 0, nil, err
		}

		stream.processRTP(r.now(), &pkt)

		return i, attr, nil
	})
}

// UnbindLocalStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (r *ReceiverInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
	r.streams.Delete(info.SSRC)
}

// BindRTCPReader lets you modify any incoming RTCP packets. It is called once per sender/receiver, however this might
// change in the future. The returned method will be called once per packet batch.
func (r *ReceiverInterceptor) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	return interceptor.RTCPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		i, attr, err := reader.Read(b, a)
		if err != nil {
			return 0, nil, err
		}

		pkts, err := rtcp.Unmarshal(b[:i])
		if err != nil {
			return 0, nil, err
		}

		for _, pkt := range pkts {
			if sr, ok := (pkt).(*rtcp.SenderReport); ok {
				value, ok := r.streams.Load(sr.SSRC)
				if !ok {
					continue
				}

				stream := value.(*receiverStream)
				stream.processSenderReport(r.now(), sr)
			}
		}

		return i, attr, nil
	})
}
