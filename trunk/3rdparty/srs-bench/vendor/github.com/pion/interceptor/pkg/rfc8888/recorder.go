// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rfc8888

import (
	"time"

	"github.com/pion/interceptor/internal/ntp"
	"github.com/pion/rtcp"
)

type packetReport struct {
	arrivalTime time.Time
	ecn         uint8
}

// Recorder records incoming RTP packets and their arrival times. Recorder can
// be used to create feedback reports as defined by RFC 8888.
type Recorder struct {
	ssrc    uint32
	streams map[uint32]*streamLog
}

// NewRecorder creates a new Recorder.
func NewRecorder() *Recorder {
	return &Recorder{
		streams: map[uint32]*streamLog{},
	}
}

// AddPacket writes a packet to the underlying stream.
func (r *Recorder) AddPacket(ts time.Time, ssrc uint32, seq uint16, ecn uint8) {
	stream, ok := r.streams[ssrc]
	if !ok {
		stream = newStreamLog(ssrc)
		r.streams[ssrc] = stream
	}
	stream.add(ts, seq, ecn)
}

// BuildReport creates a new rtcp.CCFeedbackReport containing all packets that
// were added by AddPacket and missing packets.
func (r *Recorder) BuildReport(now time.Time, maxSize int) *rtcp.CCFeedbackReport {
	report := &rtcp.CCFeedbackReport{
		SenderSSRC:      r.ssrc,
		ReportBlocks:    []rtcp.CCFeedbackReportBlock{},
		ReportTimestamp: ntp.ToNTP32(now),
	}

	maxReportBlocks := (maxSize - 12 - (8 * len(r.streams))) / 2
	var maxReportBlocksPerStream int
	if len(r.streams) > 1 {
		maxReportBlocksPerStream = maxReportBlocks / (len(r.streams) - 1)
	} else {
		maxReportBlocksPerStream = maxReportBlocks
	}

	for i, log := range r.streams {
		if len(r.streams) > 1 && int(i) == len(r.streams)-1 {
			maxReportBlocksPerStream = maxReportBlocks % len(r.streams)
		}
		block := log.metricsAfter(now, int64(maxReportBlocksPerStream))
		report.ReportBlocks = append(report.ReportBlocks, block)
	}

	return report
}
