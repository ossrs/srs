package gortsplib

import (
	"github.com/bluenviron/gortsplib/v4/pkg/description"
	"github.com/bluenviron/gortsplib/v4/pkg/format"
)

// ServerStreamStatsFormat are stream format statistics.
type ServerStreamStatsFormat struct {
	// number of sent RTP packets
	RTPPacketsSent uint64
}

// ServerStreamStatsMedia are stream media statistics.
type ServerStreamStatsMedia struct {
	// sent bytes
	BytesSent uint64
	// number of sent RTCP packets
	RTCPPacketsSent uint64

	// format statistics
	Formats map[format.Format]ServerStreamStatsFormat
}

// ServerStreamStats are stream statistics.
type ServerStreamStats struct {
	// sent bytes
	BytesSent uint64
	// number of sent RTP packets
	RTPPacketsSent uint64
	// number of sent RTCP packets
	RTCPPacketsSent uint64

	// media statistics
	Medias map[*description.Media]ServerStreamStatsMedia
}
