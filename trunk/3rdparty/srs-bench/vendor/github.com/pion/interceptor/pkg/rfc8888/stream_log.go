// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rfc8888

import (
	"time"

	"github.com/pion/rtcp"
)

const maxReportsPerReportBlock = 16384

type streamLog struct {
	ssrc                       uint32
	sequence                   unwrapper
	init                       bool
	nextSequenceNumberToReport int64 // next to report
	lastSequenceNumberReceived int64 // highest received
	log                        map[int64]*packetReport
}

func newStreamLog(ssrc uint32) *streamLog {
	return &streamLog{
		ssrc:                       ssrc,
		sequence:                   unwrapper{},
		init:                       false,
		nextSequenceNumberToReport: 0,
		lastSequenceNumberReceived: 0,
		log:                        map[int64]*packetReport{},
	}
}

func (l *streamLog) add(ts time.Time, sequenceNumber uint16, ecn uint8) {
	unwrappedSequenceNumber := l.sequence.unwrap(sequenceNumber)
	if !l.init {
		l.init = true
		l.nextSequenceNumberToReport = unwrappedSequenceNumber
	}
	l.log[unwrappedSequenceNumber] = &packetReport{
		arrivalTime: ts,
		ecn:         ecn,
	}
	if l.lastSequenceNumberReceived < unwrappedSequenceNumber {
		l.lastSequenceNumberReceived = unwrappedSequenceNumber
	}
}

// metricsAfter iterates over all packets order of their sequence number.
// Packets are removed until the first loss is detected.
func (l *streamLog) metricsAfter(reference time.Time, maxReportBlocks int64) rtcp.CCFeedbackReportBlock {
	if len(l.log) == 0 {
		return rtcp.CCFeedbackReportBlock{
			MediaSSRC:     l.ssrc,
			BeginSequence: uint16(l.nextSequenceNumberToReport),
			MetricBlocks:  []rtcp.CCFeedbackMetricBlock{},
		}
	}
	numReports := l.lastSequenceNumberReceived - l.nextSequenceNumberToReport + 1
	if numReports > maxReportBlocks {
		numReports = maxReportBlocks
		l.nextSequenceNumberToReport = l.lastSequenceNumberReceived - maxReportBlocks + 1
	}
	metricBlocks := make([]rtcp.CCFeedbackMetricBlock, numReports)
	offset := l.nextSequenceNumberToReport
	lastReceived := l.nextSequenceNumberToReport
	gapDetected := false
	for i := offset; i <= l.lastSequenceNumberReceived; i++ {
		received := false
		ecn := uint8(0)
		ato := uint16(0)
		if report, ok := l.log[i]; ok {
			received = true
			ecn = report.ecn
			ato = getArrivalTimeOffset(reference, report.arrivalTime)
		}
		metricBlocks[i-offset] = rtcp.CCFeedbackMetricBlock{
			Received:          received,
			ECN:               rtcp.ECN(ecn),
			ArrivalTimeOffset: ato,
		}

		if !gapDetected {
			if received && i == l.nextSequenceNumberToReport {
				delete(l.log, i)
				l.nextSequenceNumberToReport++
				lastReceived = i
			}
			if i > lastReceived+1 {
				gapDetected = true
			}
		}
	}
	return rtcp.CCFeedbackReportBlock{
		MediaSSRC:     l.ssrc,
		BeginSequence: uint16(offset),
		MetricBlocks:  metricBlocks,
	}
}

func getArrivalTimeOffset(base time.Time, arrival time.Time) uint16 {
	if base.Before(arrival) {
		return 0x1FFF
	}
	ato := uint16(base.Sub(arrival).Seconds() * 1024.0)
	if ato > 0x1FFD {
		return 0x1FFE
	}
	return ato
}
