package rtptime

import (
	"time"

	"github.com/pion/rtp"
)

var timeNow = time.Now

// avoid an int64 overflow and preserve resolution by splitting division into two parts:
// first add the integer part, then the decimal part.
func multiplyAndDivide(v, m, d time.Duration) time.Duration {
	secs := v / d
	dec := v % d
	return (secs*m + dec*m/d)
}

type globalDecoderTrackData struct {
	startPTS  time.Duration
	clockRate time.Duration
	overall   time.Duration
	prev      uint32
}

func newGlobalDecoderTrackData(
	startPTS time.Duration,
	clockRate int,
	startTimestamp uint32,
) *globalDecoderTrackData {
	return &globalDecoderTrackData{
		startPTS:  startPTS,
		clockRate: time.Duration(clockRate),
		prev:      startTimestamp,
	}
}

func (d *globalDecoderTrackData) decode(ts uint32) time.Duration {
	diff := int32(ts - d.prev)
	d.prev = ts
	d.overall += time.Duration(diff)

	return d.startPTS + multiplyAndDivide(d.overall, time.Second, d.clockRate)
}

// GlobalDecoderTrack is a track (RTSP format or WebRTC track) of a GlobalDecoder.
//
// Deprecated: replaced by GlobalDecoderTrack2
type GlobalDecoderTrack interface {
	ClockRate() int
	PTSEqualsDTS(*rtp.Packet) bool
}

// GlobalDecoder is a RTP timestamp decoder.
//
// Deprecated: replaced by GlobalDecoder2.
type GlobalDecoder struct {
	wrapped *GlobalDecoder2
}

// NewGlobalDecoder allocates a GlobalDecoder.
//
// Deprecated: replaced by NewGlobalDecoder2.
func NewGlobalDecoder() *GlobalDecoder {
	return &GlobalDecoder{
		wrapped: NewGlobalDecoder2(),
	}
}

// Decode decodes a timestamp.
func (d *GlobalDecoder) Decode(
	track GlobalDecoderTrack,
	pkt *rtp.Packet,
) (time.Duration, bool) {
	v, ok := d.wrapped.Decode(track, pkt)
	if !ok {
		return 0, false
	}

	return multiplyAndDivide(time.Duration(v), time.Second, time.Duration(track.ClockRate())), true
}
