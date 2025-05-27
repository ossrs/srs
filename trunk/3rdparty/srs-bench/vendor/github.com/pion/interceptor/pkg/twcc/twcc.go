// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package twcc provides interceptors to implement transport wide congestion control.
package twcc

import (
	"math"

	"github.com/pion/interceptor/internal/sequencenumber"
	"github.com/pion/rtcp"
)

const (
	packetWindowMicroseconds  = 500_000
	maxMissingSequenceNumbers = 0x7FFE
)

// Recorder records incoming RTP packets and their delays and creates
// transport wide congestion control feedback reports as specified in
// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
type Recorder struct {
	arrivalTimeMap packetArrivalTimeMap

	sequenceUnwrapper sequencenumber.Unwrapper

	// startSequenceNumber is the first sequence number that will be included in the the
	// next feedback packet.
	startSequenceNumber *int64

	senderSSRC uint32
	mediaSSRC  uint32
	fbPktCnt   uint8

	packetsHeld int
}

// NewRecorder creates a new Recorder which uses the given senderSSRC in the created
// feedback packets.
func NewRecorder(senderSSRC uint32) *Recorder {
	return &Recorder{
		senderSSRC: senderSSRC,
	}
}

// Record marks a packet with mediaSSRC and a transport wide sequence number sequenceNumber as received at arrivalTime.
func (r *Recorder) Record(mediaSSRC uint32, sequenceNumber uint16, arrivalTime int64) {
	r.mediaSSRC = mediaSSRC

	// "Unwrap" the sequence number to get a monotonically increasing sequence number that
	// won't wrap around after math.MaxUint16.
	unwrappedSN := r.sequenceUnwrapper.Unwrap(sequenceNumber)
	r.maybeCullOldPackets(unwrappedSN, arrivalTime)
	if r.startSequenceNumber == nil || unwrappedSN < *r.startSequenceNumber {
		r.startSequenceNumber = &unwrappedSN
	}

	// We are only interested in the first time a packet is received.
	if r.arrivalTimeMap.HasReceived(unwrappedSN) {
		return
	}

	r.arrivalTimeMap.AddPacket(unwrappedSN, arrivalTime)
	r.packetsHeld++

	// Limit the range of sequence numbers to send feedback for.
	if *r.startSequenceNumber < r.arrivalTimeMap.BeginSequenceNumber() {
		sn := r.arrivalTimeMap.BeginSequenceNumber()
		r.startSequenceNumber = &sn
	}
}

func (r *Recorder) maybeCullOldPackets(sequenceNumber int64, arrivalTime int64) {
	if r.startSequenceNumber != nil && *r.startSequenceNumber >= r.arrivalTimeMap.EndSequenceNumber() && arrivalTime >= packetWindowMicroseconds {
		r.arrivalTimeMap.RemoveOldPackets(sequenceNumber, arrivalTime-packetWindowMicroseconds)
	}
}

// PacketsHeld returns the number of received packets currently held by the recorder
func (r *Recorder) PacketsHeld() int {
	return r.packetsHeld
}

// BuildFeedbackPacket creates a new RTCP packet containing a TWCC feedback report.
func (r *Recorder) BuildFeedbackPacket() []rtcp.Packet {
	if r.startSequenceNumber == nil {
		return nil
	}

	endSN := r.arrivalTimeMap.EndSequenceNumber()
	var feedbacks []rtcp.Packet
	for *r.startSequenceNumber < endSN {
		feedback := r.maybeBuildFeedbackPacket(*r.startSequenceNumber, endSN)
		if feedback == nil {
			break
		}
		feedbacks = append(feedbacks, feedback.getRTCP())

		// NOTE: we don't erase packets from the history in case they need to be resent
		// after a reordering. They will be removed instead in Record when they get too
		// old.
	}
	r.packetsHeld = 0
	return feedbacks
}

// maybeBuildFeedbackPacket builds a feedback packet starting from startSN (inclusive) until
// endSN (exclusive).
func (r *Recorder) maybeBuildFeedbackPacket(beginSeqNumInclusive, endSeqNumExclusive int64) *feedback {
	// NOTE: The logic of this method is inspired by the implementation in Chrome.
	// See https://source.chromium.org/chromium/chromium/src/+/refs/heads/main:third_party/webrtc/modules/remote_bitrate_estimator/remote_estimator_proxy.cc;l=276;drc=b5cd13bb6d5d157a5fbe3628b2dd1c1e106203c6
	startSNInclusive, endSNExclusive := r.arrivalTimeMap.Clamp(beginSeqNumInclusive), r.arrivalTimeMap.Clamp(endSeqNumExclusive)

	// Create feedback on demand, as we don't yet know if there are packets in the range that have been
	// received.
	var fb *feedback

	nextSequenceNumber := beginSeqNumInclusive

	for seq := startSNInclusive; seq < endSNExclusive; seq++ {
		foundSeq, arrivalTime, ok := r.arrivalTimeMap.FindNextAtOrAfter(seq)
		seq = foundSeq
		if !ok || seq >= endSNExclusive {
			break
		}

		if fb == nil {
			fb = newFeedback(r.senderSSRC, r.mediaSSRC, r.fbPktCnt)
			r.fbPktCnt++

			// It should be possible to add seq to this new packet.
			// If the difference between seq and beginSeqNumInclusive is too large, discard
			// reporting too old missing packets.
			baseSequenceNumber := max64(beginSeqNumInclusive, seq-maxMissingSequenceNumbers)

			// baseSequenceNumber is the expected first sequence number. This is known,
			// but we may not have actually received it, so the base time should be the time
			// of the first received packet in the feedback.
			fb.setBase(uint16(baseSequenceNumber), arrivalTime)

			if !fb.addReceived(uint16(seq), arrivalTime) {
				// Could not add a single received packet to the feedback.
				// This is unexpected to actually occur, but if it does, we'll
				// try again after skipping any missing packets.
				// NOTE: It's fine that we already incremented fbPktCnt, as in essence
				// we did actually "skip" a feedback (and this matches Chrome's behavior).
				r.startSequenceNumber = &seq
				return nil
			}
		} else if !fb.addReceived(uint16(seq), arrivalTime) {
			// Could not add timestamp. Packet may be full. Return
			// and try again with a fresh packet.
			break
		}

		nextSequenceNumber = seq + 1
	}

	r.startSequenceNumber = &nextSequenceNumber
	return fb
}

type feedback struct {
	rtcp                *rtcp.TransportLayerCC
	baseSequenceNumber  uint16
	refTimestamp64MS    int64
	lastTimestampUS     int64
	nextSequenceNumber  uint16
	sequenceNumberCount uint16
	len                 int
	lastChunk           chunk
	chunks              []rtcp.PacketStatusChunk
	deltas              []*rtcp.RecvDelta
}

func newFeedback(senderSSRC, mediaSSRC uint32, count uint8) *feedback {
	return &feedback{
		rtcp: &rtcp.TransportLayerCC{
			SenderSSRC: senderSSRC,
			MediaSSRC:  mediaSSRC,
			FbPktCount: count,
		},
	}
}

func (f *feedback) setBase(sequenceNumber uint16, timeUS int64) {
	f.baseSequenceNumber = sequenceNumber
	f.nextSequenceNumber = f.baseSequenceNumber
	f.refTimestamp64MS = timeUS / 64e3
	f.lastTimestampUS = f.refTimestamp64MS * 64e3
}

func (f *feedback) getRTCP() *rtcp.TransportLayerCC {
	f.rtcp.PacketStatusCount = f.sequenceNumberCount
	f.rtcp.ReferenceTime = uint32(f.refTimestamp64MS)
	f.rtcp.BaseSequenceNumber = f.baseSequenceNumber
	for len(f.lastChunk.deltas) > 0 {
		f.chunks = append(f.chunks, f.lastChunk.encode())
	}
	f.rtcp.PacketChunks = append(f.rtcp.PacketChunks, f.chunks...)
	f.rtcp.RecvDeltas = f.deltas

	padLen := 20 + len(f.rtcp.PacketChunks)*2 + f.len // 4 bytes header + 16 bytes twcc header + 2 bytes for each chunk + length of deltas
	padding := padLen%4 != 0
	for padLen%4 != 0 {
		padLen++
	}
	f.rtcp.Header = rtcp.Header{
		Count:   rtcp.FormatTCC,
		Type:    rtcp.TypeTransportSpecificFeedback,
		Padding: padding,
		Length:  uint16((padLen / 4) - 1),
	}

	return f.rtcp
}

func (f *feedback) addReceived(sequenceNumber uint16, timestampUS int64) bool {
	deltaUS := timestampUS - f.lastTimestampUS
	var delta250US int64
	if deltaUS >= 0 {
		delta250US = (deltaUS + rtcp.TypeTCCDeltaScaleFactor/2) / rtcp.TypeTCCDeltaScaleFactor
	} else {
		delta250US = (deltaUS - rtcp.TypeTCCDeltaScaleFactor/2) / rtcp.TypeTCCDeltaScaleFactor
	}
	if delta250US < math.MinInt16 || delta250US > math.MaxInt16 { // delta doesn't fit into 16 bit, need to create new packet
		return false
	}
	deltaUSRounded := delta250US * rtcp.TypeTCCDeltaScaleFactor

	for ; f.nextSequenceNumber != sequenceNumber; f.nextSequenceNumber++ {
		if !f.lastChunk.canAdd(rtcp.TypeTCCPacketNotReceived) {
			f.chunks = append(f.chunks, f.lastChunk.encode())
		}
		f.lastChunk.add(rtcp.TypeTCCPacketNotReceived)
		f.sequenceNumberCount++
	}

	var recvDelta uint16
	switch {
	case delta250US >= 0 && delta250US <= 0xff:
		f.len++
		recvDelta = rtcp.TypeTCCPacketReceivedSmallDelta
	default:
		f.len += 2
		recvDelta = rtcp.TypeTCCPacketReceivedLargeDelta
	}

	if !f.lastChunk.canAdd(recvDelta) {
		f.chunks = append(f.chunks, f.lastChunk.encode())
	}
	f.lastChunk.add(recvDelta)
	f.deltas = append(f.deltas, &rtcp.RecvDelta{
		Type:  recvDelta,
		Delta: deltaUSRounded,
	})
	f.lastTimestampUS += deltaUSRounded
	f.sequenceNumberCount++
	f.nextSequenceNumber++
	return true
}

const (
	maxRunLengthCap = 0x1fff // 13 bits
	maxOneBitCap    = 14     // bits
	maxTwoBitCap    = 7      // bits
)

type chunk struct {
	hasLargeDelta     bool
	hasDifferentTypes bool
	deltas            []uint16
}

func (c *chunk) canAdd(delta uint16) bool {
	if len(c.deltas) < maxTwoBitCap {
		return true
	}
	if len(c.deltas) < maxOneBitCap && !c.hasLargeDelta && delta != rtcp.TypeTCCPacketReceivedLargeDelta {
		return true
	}
	if len(c.deltas) < maxRunLengthCap && !c.hasDifferentTypes && delta == c.deltas[0] {
		return true
	}
	return false
}

func (c *chunk) add(delta uint16) {
	c.deltas = append(c.deltas, delta)
	c.hasLargeDelta = c.hasLargeDelta || delta == rtcp.TypeTCCPacketReceivedLargeDelta
	c.hasDifferentTypes = c.hasDifferentTypes || delta != c.deltas[0]
}

func (c *chunk) encode() rtcp.PacketStatusChunk {
	if !c.hasDifferentTypes {
		defer c.reset()
		return &rtcp.RunLengthChunk{
			PacketStatusSymbol: c.deltas[0],
			RunLength:          uint16(len(c.deltas)),
		}
	}
	if len(c.deltas) == maxOneBitCap {
		defer c.reset()
		return &rtcp.StatusVectorChunk{
			SymbolSize: rtcp.TypeTCCSymbolSizeOneBit,
			SymbolList: c.deltas,
		}
	}

	minCap := minInt(maxTwoBitCap, len(c.deltas))
	svc := &rtcp.StatusVectorChunk{
		SymbolSize: rtcp.TypeTCCSymbolSizeTwoBit,
		SymbolList: c.deltas[:minCap],
	}
	c.deltas = c.deltas[minCap:]
	c.hasDifferentTypes = false
	c.hasLargeDelta = false

	if len(c.deltas) > 0 {
		tmp := c.deltas[0]
		for _, d := range c.deltas {
			if tmp != d {
				c.hasDifferentTypes = true
			}
			if d == rtcp.TypeTCCPacketReceivedLargeDelta {
				c.hasLargeDelta = true
			}
		}
	}

	return svc
}

func (c *chunk) reset() {
	c.deltas = []uint16{}
	c.hasLargeDelta = false
	c.hasDifferentTypes = false
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func max64(a, b int64) int64 {
	if a > b {
		return a
	}
	return b
}

func min64(a, b int64) int64 {
	if a < b {
		return a
	}
	return b
}
