// Package rtcpsender contains a utility to generate RTCP sender reports.
package rtcpsender

import (
	"sync"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// seconds since 1st January 1900
// higher 32 bits are the integer part, lower 32 bits are the fractional part
func ntpTimeGoToRTCP(v time.Time) uint64 {
	s := uint64(v.UnixNano()) + 2208988800*1000000000
	return (s/1000000000)<<32 | (s % 1000000000)
}

// RTCPSender is a utility to generate RTCP sender reports.
type RTCPSender struct {
	ClockRate       int
	Period          time.Duration
	TimeNow         func() time.Time
	WritePacketRTCP func(rtcp.Packet)

	mutex sync.RWMutex

	// data from RTP packets
	firstRTPPacketSent bool
	lastTimeRTP        uint32
	lastTimeNTP        time.Time
	lastTimeSystem     time.Time
	localSSRC          uint32
	lastSequenceNumber uint16
	packetCount        uint32
	octetCount         uint32

	terminate chan struct{}
	done      chan struct{}
}

// New allocates a RTCPSender.
//
// Deprecated: replaced by Initialize().
func New(
	clockRate int,
	period time.Duration,
	timeNow func() time.Time,
	writePacketRTCP func(rtcp.Packet),
) *RTCPSender {
	rs := &RTCPSender{
		ClockRate:       clockRate,
		Period:          period,
		TimeNow:         timeNow,
		WritePacketRTCP: writePacketRTCP,
	}
	rs.Initialize()

	return rs
}

// Initialize initializes a RTCPSender.
func (rs *RTCPSender) Initialize() {
	if rs.TimeNow == nil {
		rs.TimeNow = time.Now
	}

	rs.terminate = make(chan struct{})
	rs.done = make(chan struct{})

	go rs.run()
}

func (rs *RTCPSender) run() {
	defer close(rs.done)

	t := time.NewTicker(rs.Period)
	defer t.Stop()

	for {
		select {
		case <-t.C:
			report := rs.report()
			if report != nil {
				rs.WritePacketRTCP(report)
			}

		case <-rs.terminate:
			return
		}
	}
}

func (rs *RTCPSender) report() rtcp.Packet {
	rs.mutex.Lock()
	defer rs.mutex.Unlock()

	if !rs.firstRTPPacketSent {
		return nil
	}

	systemTimeDiff := rs.TimeNow().Sub(rs.lastTimeSystem)
	ntpTime := rs.lastTimeNTP.Add(systemTimeDiff)
	rtpTime := rs.lastTimeRTP + uint32(systemTimeDiff.Seconds()*float64(rs.ClockRate))

	return &rtcp.SenderReport{
		SSRC:        rs.localSSRC,
		NTPTime:     ntpTimeGoToRTCP(ntpTime),
		RTPTime:     rtpTime,
		PacketCount: rs.packetCount,
		OctetCount:  rs.octetCount,
	}
}

// Close closes the RTCPSender.
func (rs *RTCPSender) Close() {
	close(rs.terminate)
	<-rs.done
}

// ProcessPacket extracts data from RTP packets.
func (rs *RTCPSender) ProcessPacket(pkt *rtp.Packet, ntp time.Time, ptsEqualsDTS bool) {
	rs.mutex.Lock()
	defer rs.mutex.Unlock()

	if ptsEqualsDTS {
		rs.firstRTPPacketSent = true
		rs.lastTimeRTP = pkt.Timestamp
		rs.lastTimeNTP = ntp
		rs.lastTimeSystem = rs.TimeNow()
		rs.localSSRC = pkt.SSRC
	}

	rs.lastSequenceNumber = pkt.SequenceNumber

	rs.packetCount++
	rs.octetCount += uint32(len(pkt.Payload))
}

// SenderSSRC returns the SSRC of outgoing RTP packets.
//
// Deprecated: replaced by Stats().
func (rs *RTCPSender) SenderSSRC() (uint32, bool) {
	stats := rs.Stats()
	if stats == nil {
		return 0, false
	}

	return stats.LocalSSRC, true
}

// LastPacketData returns metadata of the last RTP packet.
//
// Deprecated: replaced by Stats().
func (rs *RTCPSender) LastPacketData() (uint16, uint32, time.Time, bool) {
	stats := rs.Stats()
	if stats == nil {
		return 0, 0, time.Time{}, false
	}

	return stats.LastSequenceNumber, stats.LastRTP, stats.LastNTP, true
}

// Stats are statistics.
type Stats struct {
	LocalSSRC          uint32
	LastSequenceNumber uint16
	LastRTP            uint32
	LastNTP            time.Time
}

// Stats returns statistics.
func (rs *RTCPSender) Stats() *Stats {
	rs.mutex.RLock()
	defer rs.mutex.RUnlock()

	if !rs.firstRTPPacketSent {
		return nil
	}

	return &Stats{
		LocalSSRC:          rs.localSSRC,
		LastSequenceNumber: rs.lastSequenceNumber,
		LastRTP:            rs.lastTimeRTP,
		LastNTP:            rs.lastTimeNTP,
	}
}
