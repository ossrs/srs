package rtptime

import (
	"sync"
	"time"

	"github.com/pion/rtp"
)

// avoid an int64 overflow and preserve resolution by splitting division into two parts:
// first add the integer part, then the decimal part.
func multiplyAndDivide2(v, m, d int64) int64 {
	secs := v / d
	dec := v % d
	return (secs*m + dec*m/d)
}

type globalDecoder2TrackData struct {
	overall int64
	prev    uint32
}

func (d *globalDecoder2TrackData) decode(ts uint32) int64 {
	d.overall += int64(int32(ts - d.prev))
	d.prev = ts
	return d.overall
}

// GlobalDecoder2Track is a track (RTSP format or WebRTC track) of GlobalDecoder2.
type GlobalDecoder2Track interface {
	ClockRate() int
	PTSEqualsDTS(*rtp.Packet) bool
}

// NewGlobalDecoder2 allocates a GlobalDecoder.
//
// Deprecated: replaced by GlobalDecoder2.Initialize().
func NewGlobalDecoder2() *GlobalDecoder2 {
	d := &GlobalDecoder2{}
	d.Initialize()
	return d
}

// GlobalDecoder2 is a RTP timestamp decoder.
type GlobalDecoder2 struct {
	mutex             sync.Mutex
	leadingTrack      GlobalDecoderTrack
	startNTP          time.Time
	startPTS          int64
	startPTSClockRate int64
	tracks            map[GlobalDecoder2Track]*globalDecoder2TrackData
}

// Initialize initializes a GlobalDecoder2.
func (d *GlobalDecoder2) Initialize() {
	d.tracks = make(map[GlobalDecoder2Track]*globalDecoder2TrackData)
}

// Decode decodes a timestamp.
func (d *GlobalDecoder2) Decode(
	track GlobalDecoder2Track,
	pkt *rtp.Packet,
) (int64, bool) {
	if track.ClockRate() == 0 {
		return 0, false
	}

	d.mutex.Lock()
	defer d.mutex.Unlock()

	df, ok := d.tracks[track]

	// never seen before track
	if !ok {
		if !track.PTSEqualsDTS(pkt) {
			return 0, false
		}

		now := timeNow()

		if d.leadingTrack == nil {
			d.leadingTrack = track
			d.startNTP = now
			d.startPTS = 0
			d.startPTSClockRate = int64(track.ClockRate())
		}

		// start from the PTS of the leading track
		startPTS := multiplyAndDivide2(d.startPTS, int64(track.ClockRate()), d.startPTSClockRate)
		startPTS += multiplyAndDivide2(int64(now.Sub(d.startNTP)), int64(track.ClockRate()), int64(time.Second))

		d.tracks[track] = &globalDecoder2TrackData{
			overall: startPTS,
			prev:    pkt.Timestamp,
		}

		return startPTS, true
	}

	pts := df.decode(pkt.Timestamp)

	// update startNTP / startPTS
	if d.leadingTrack == track && track.PTSEqualsDTS(pkt) {
		now := timeNow()
		d.startNTP = now
		d.startPTS = pts
	}

	return pts, true
}
