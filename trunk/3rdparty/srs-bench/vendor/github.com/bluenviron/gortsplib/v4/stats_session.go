package gortsplib

import (
	"time"

	"github.com/bluenviron/gortsplib/v4/pkg/description"
	"github.com/bluenviron/gortsplib/v4/pkg/format"
)

// StatsSessionFormat are session format statistics.
type StatsSessionFormat struct {
	// number of RTP packets correctly received and processed
	RTPPacketsReceived uint64
	// number of sent RTP packets
	RTPPacketsSent uint64
	// number of lost RTP packets
	RTPPacketsLost uint64
	// mean jitter of received RTP packets
	RTPPacketsJitter float64
	// local SSRC
	LocalSSRC uint32
	// remote SSRC
	RemoteSSRC uint32
	// last sequence number of incoming/outgoing RTP packets
	RTPPacketsLastSequenceNumber uint16
	// last RTP time of incoming/outgoing RTP packets
	RTPPacketsLastRTP uint32
	// last NTP time of incoming/outgoing NTP packets
	RTPPacketsLastNTP time.Time
}

// StatsSessionMedia are session media statistics.
type StatsSessionMedia struct {
	// received bytes
	BytesReceived uint64
	// sent bytes
	BytesSent uint64
	// number of RTP packets that could not be processed
	RTPPacketsInError uint64
	// number of RTCP packets correctly received and processed
	RTCPPacketsReceived uint64
	// number of sent RTCP packets
	RTCPPacketsSent uint64
	// number of RTCP packets that could not be processed
	RTCPPacketsInError uint64

	// format statistics
	Formats map[format.Format]StatsSessionFormat
}

// StatsSession are session statistics.
type StatsSession struct {
	// received bytes
	BytesReceived uint64
	// sent bytes
	BytesSent uint64
	// number of RTP packets correctly received and processed
	RTPPacketsReceived uint64
	// number of sent RTP packets
	RTPPacketsSent uint64
	// number of lost RTP packets
	RTPPacketsLost uint64
	// number of RTP packets that could not be processed
	RTPPacketsInError uint64
	// mean jitter of received RTP packets
	RTPPacketsJitter float64
	// number of RTCP packets correctly received and processed
	RTCPPacketsReceived uint64
	// number of sent RTCP packets
	RTCPPacketsSent uint64
	// number of RTCP packets that could not be processed
	RTCPPacketsInError uint64

	// media statistics
	Medias map[*description.Media]StatsSessionMedia
}
