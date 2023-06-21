// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package twcc provides interceptors to implement transport wide congestion control.
package twcc

import (
	"math"

	"github.com/pion/rtcp"
)

type pktInfo struct {
	sequenceNumber uint32
	arrivalTime    int64
}

// Recorder records incoming RTP packets and their delays and creates
// transport wide congestion control feedback reports as specified in
// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
type Recorder struct {
	receivedPackets []pktInfo

	cycles             uint32
	lastSequenceNumber uint16

	senderSSRC uint32
	mediaSSRC  uint32
	fbPktCnt   uint8
}

// NewRecorder creates a new Recorder which uses the given senderSSRC in the created
// feedback packets.
func NewRecorder(senderSSRC uint32) *Recorder {
	return &Recorder{
		receivedPackets: []pktInfo{},
		senderSSRC:      senderSSRC,
	}
}

// Record marks a packet with mediaSSRC and a transport wide sequence number sequenceNumber as received at arrivalTime.
func (r *Recorder) Record(mediaSSRC uint32, sequenceNumber uint16, arrivalTime int64) {
	r.mediaSSRC = mediaSSRC
	if sequenceNumber < 0x0fff && (r.lastSequenceNumber&0xffff) > 0xf000 {
		r.cycles += 1 << 16
	}
	r.receivedPackets = insertSorted(r.receivedPackets, pktInfo{
		sequenceNumber: r.cycles | uint32(sequenceNumber),
		arrivalTime:    arrivalTime,
	})
	r.lastSequenceNumber = sequenceNumber
}

func insertSorted(list []pktInfo, element pktInfo) []pktInfo {
	if len(list) == 0 {
		return append(list, element)
	}
	for i := len(list) - 1; i >= 0; i-- {
		if list[i].sequenceNumber < element.sequenceNumber {
			list = append(list, pktInfo{})
			copy(list[i+2:], list[i+1:])
			list[i+1] = element
			return list
		}
		if list[i].sequenceNumber == element.sequenceNumber {
			list[i] = element
			return list
		}
	}
	// element.sequenceNumber is between 0 and first ever received sequenceNumber
	return append([]pktInfo{element}, list...)
}

// BuildFeedbackPacket creates a new RTCP packet containing a TWCC feedback report.
func (r *Recorder) BuildFeedbackPacket() []rtcp.Packet {
	if len(r.receivedPackets) < 2 {
		return nil
	}

	feedback := newFeedback(r.senderSSRC, r.mediaSSRC, r.fbPktCnt)
	r.fbPktCnt++
	feedback.setBase(uint16(r.receivedPackets[0].sequenceNumber&0xffff), r.receivedPackets[0].arrivalTime)

	var pkts []rtcp.Packet
	for _, pkt := range r.receivedPackets {
		ok := feedback.addReceived(uint16(pkt.sequenceNumber&0xffff), pkt.arrivalTime)
		if !ok {
			pkts = append(pkts, feedback.getRTCP())
			feedback = newFeedback(r.senderSSRC, r.mediaSSRC, r.fbPktCnt)
			r.fbPktCnt++
			feedback.addReceived(uint16(pkt.sequenceNumber&0xffff), pkt.arrivalTime)
		}
	}
	r.receivedPackets = []pktInfo{}
	pkts = append(pkts, feedback.getRTCP())

	return pkts
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
	delta250US := deltaUS / 250
	if delta250US < math.MinInt16 || delta250US > math.MaxInt16 { // delta doesn't fit into 16 bit, need to create new packet
		return false
	}

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
		Delta: deltaUS,
	})
	f.lastTimestampUS = timestampUS
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

	minCap := min(maxTwoBitCap, len(c.deltas))
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

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
