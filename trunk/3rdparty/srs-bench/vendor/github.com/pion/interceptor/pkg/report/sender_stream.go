// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package report

import (
	"sync"
	"time"

	"github.com/pion/interceptor/internal/ntp"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

type senderStream struct {
	ssrc      uint32
	clockRate float64
	m         sync.Mutex

	// data from rtp packets
	lastRTPTimeRTP  uint32
	lastRTPTimeTime time.Time
	packetCount     uint32
	octetCount      uint32
}

func newSenderStream(ssrc uint32, clockRate uint32) *senderStream {
	return &senderStream{
		ssrc:      ssrc,
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

func (stream *senderStream) generateReport(now time.Time) *rtcp.SenderReport {
	stream.m.Lock()
	defer stream.m.Unlock()

	return &rtcp.SenderReport{
		SSRC:        stream.ssrc,
		NTPTime:     ntp.ToNTP(now),
		RTPTime:     stream.lastRTPTimeRTP + uint32(now.Sub(stream.lastRTPTimeTime).Seconds()*stream.clockRate),
		PacketCount: stream.packetCount,
		OctetCount:  stream.octetCount,
	}
}
