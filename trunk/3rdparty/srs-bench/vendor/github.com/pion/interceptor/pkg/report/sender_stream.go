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

	useLatestPacket bool

	// data from rtp packets
	lastRTPTimeRTP  uint32
	lastRTPTimeTime time.Time
	lastRTPSN       uint16
	packetCount     uint32
	octetCount      uint32
}

func newSenderStream(ssrc uint32, clockRate uint32, useLatestPacket bool) *senderStream {
	return &senderStream{
		ssrc:            ssrc,
		clockRate:       float64(clockRate),
		useLatestPacket: useLatestPacket,
	}
}

func (stream *senderStream) processRTP(now time.Time, header *rtp.Header, payload []byte) {
	stream.m.Lock()
	defer stream.m.Unlock()

	diff := header.SequenceNumber - stream.lastRTPSN
	if stream.useLatestPacket || stream.packetCount == 0 || (diff > 0 && diff < (1<<15)) {
		// Told to consider every packet, or this was the first packet, or it's in-order
		stream.lastRTPSN = header.SequenceNumber
		stream.lastRTPTimeRTP = header.Timestamp
		stream.lastRTPTimeTime = now
	}

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
