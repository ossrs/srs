package report

import (
	"sync"
	"time"

	"github.com/pion/rtp"
)

type senderStream struct {
	clockRate float64
	m         sync.Mutex

	// data from rtp packets
	lastRTPTimeRTP  uint32
	lastRTPTimeTime time.Time
	packetCount     uint32
	octetCount      uint32
}

func newSenderStream(clockRate uint32) *senderStream {
	return &senderStream{
		clockRate: float64(clockRate),
	}
}

func (stream *senderStream) processRTP(now time.Time, header *rtp.Header, payload []byte) {
	stream.m.Lock()
	defer stream.m.Unlock()

	// always update time to minimize errors
	stream.lastRTPTimeRTP = header.Timestamp
	stream.lastRTPTimeTime = now

	stream.packetCount++
	stream.octetCount += uint32(len(payload))
}
