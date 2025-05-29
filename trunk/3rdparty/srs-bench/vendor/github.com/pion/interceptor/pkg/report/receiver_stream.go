// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package report

import (
	"math/rand"
	"sync"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

const (
	// packetsPerHistoryEntry represents how many packets are in the bitmask for
	// each entry in the `packets` slice in the receiver stream. Because we use
	// a uint64, we can keep track of 64 packets per entry.
	packetsPerHistoryEntry = 64
)

type receiverStream struct {
	ssrc         uint32
	receiverSSRC uint32
	clockRate    float64

	m                    sync.Mutex
	size                 uint16
	packets              []uint64
	started              bool
	seqnumCycles         uint16
	lastSeqnum           uint16
	lastReportSeqnum     uint16
	lastRTPTimeRTP       uint32
	lastRTPTimeTime      time.Time
	jitter               float64
	lastSenderReport     uint32
	lastSenderReportTime time.Time
	totalLost            uint32
}

func newReceiverStream(ssrc uint32, clockRate uint32) *receiverStream {
	receiverSSRC := rand.Uint32() // #nosec
	return &receiverStream{
		ssrc:         ssrc,
		receiverSSRC: receiverSSRC,
		clockRate:    float64(clockRate),
		size:         128,
		packets:      make([]uint64, 128),
	}
}

func (stream *receiverStream) processRTP(now time.Time, pktHeader *rtp.Header) {
	stream.m.Lock()
	defer stream.m.Unlock()

	if !stream.started { // first frame
		stream.started = true
		stream.setReceived(pktHeader.SequenceNumber)
		stream.lastSeqnum = pktHeader.SequenceNumber
		stream.lastReportSeqnum = pktHeader.SequenceNumber - 1
		stream.lastRTPTimeRTP = pktHeader.Timestamp
		stream.lastRTPTimeTime = now
	} else { // following frames
		stream.setReceived(pktHeader.SequenceNumber)

		diff := pktHeader.SequenceNumber - stream.lastSeqnum
		if diff > 0 && diff < (1<<15) {
			// wrap around
			if pktHeader.SequenceNumber < stream.lastSeqnum {
				stream.seqnumCycles++
			}

			// set missing packets as missing
			for i := stream.lastSeqnum + 1; i != pktHeader.SequenceNumber; i++ {
				stream.delReceived(i)
			}

			stream.lastSeqnum = pktHeader.SequenceNumber
		}

		// compute jitter
		// https://tools.ietf.org/html/rfc3550#page-39
		D := now.Sub(stream.lastRTPTimeTime).Seconds()*stream.clockRate -
			(float64(pktHeader.Timestamp) - float64(stream.lastRTPTimeRTP))
		if D < 0 {
			D = -D
		}
		stream.jitter += (D - stream.jitter) / 16
		stream.lastRTPTimeRTP = pktHeader.Timestamp
		stream.lastRTPTimeTime = now
	}
}

func (stream *receiverStream) setReceived(seq uint16) {
	pos := seq % (stream.size * packetsPerHistoryEntry)
	stream.packets[pos/packetsPerHistoryEntry] |= 1 << (pos % packetsPerHistoryEntry)
}

func (stream *receiverStream) delReceived(seq uint16) {
	pos := seq % (stream.size * packetsPerHistoryEntry)
	stream.packets[pos/packetsPerHistoryEntry] &^= 1 << (pos % packetsPerHistoryEntry)
}

func (stream *receiverStream) getReceived(seq uint16) bool {
	pos := seq % (stream.size * packetsPerHistoryEntry)
	return (stream.packets[pos/packetsPerHistoryEntry] & (1 << (pos % packetsPerHistoryEntry))) != 0
}

func (stream *receiverStream) processSenderReport(now time.Time, sr *rtcp.SenderReport) {
	stream.m.Lock()
	defer stream.m.Unlock()

	stream.lastSenderReport = uint32(sr.NTPTime >> 16)
	stream.lastSenderReportTime = now
}

func (stream *receiverStream) generateReport(now time.Time) *rtcp.ReceiverReport {
	stream.m.Lock()
	defer stream.m.Unlock()

	totalSinceReport := stream.lastSeqnum - stream.lastReportSeqnum
	totalLostSinceReport := func() uint32 {
		if stream.lastSeqnum == stream.lastReportSeqnum {
			return 0
		}

		ret := uint32(0)
		for i := stream.lastReportSeqnum + 1; i != stream.lastSeqnum; i++ {
			if !stream.getReceived(i) {
				ret++
			}
		}
		return ret
	}()
	stream.totalLost += totalLostSinceReport

	// allow up to 24 bits
	if totalLostSinceReport > 0xFFFFFF {
		totalLostSinceReport = 0xFFFFFF
	}
	if stream.totalLost > 0xFFFFFF {
		stream.totalLost = 0xFFFFFF
	}

	r := &rtcp.ReceiverReport{
		SSRC: stream.receiverSSRC,
		Reports: []rtcp.ReceptionReport{
			{
				SSRC:               stream.ssrc,
				LastSequenceNumber: uint32(stream.seqnumCycles)<<16 | uint32(stream.lastSeqnum),
				LastSenderReport:   stream.lastSenderReport,
				FractionLost:       uint8(float64(totalLostSinceReport*256) / float64(totalSinceReport)),
				TotalLost:          stream.totalLost,
				Delay: func() uint32 {
					if stream.lastSenderReportTime.IsZero() {
						return 0
					}
					return uint32(now.Sub(stream.lastSenderReportTime).Seconds() * 65536)
				}(),
				Jitter: uint32(stream.jitter),
			},
		},
	}

	stream.lastReportSeqnum = stream.lastSeqnum

	return r
}
