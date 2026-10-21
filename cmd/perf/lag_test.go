package main

import (
	"os"
	"testing"
	"time"
)

// cbrLikeTags is a 6s source like the CBR one: H.264 at 25 fps with an IDR
// every 2s and every frame a different payload, AAC every 23ms, and the
// sequence headers at 0.
func cbrLikeTags() []rtmpMessage {
	tags := []rtmpMessage{
		{timestamp: 0, typeID: 9, payload: []byte{0x17, 0, 0, 0, 0, 1, 0x64}},
		{timestamp: 0, typeID: 8, payload: []byte{0xaf, 0, 0x12, 0x10}},
	}
	for ts := uint32(0); ts < 6000; ts += 40 {
		frame := byte(0x27)
		if ts%2000 == 0 {
			frame = 0x17
		}
		tags = append(tags, rtmpMessage{timestamp: ts, typeID: 9, payload: []byte{frame, 1, 0, 0, 0, byte(ts >> 8), byte(ts)}})
		tags = append(tags, rtmpMessage{timestamp: ts + 20, typeID: 8, payload: []byte{0xaf, 1, 0x21}})
	}
	return tags
}

// fullJitter stamps a player's messages as SRS's default time_jitter full does:
// a jump of more than 250ms, such as from the cached sequence headers to the
// GOP cache's first frame, becomes 10ms, so the stream starts near zero.
type fullJitter struct {
	last, corrected int64
}

func (j *fullJitter) correct(ts uint32) uint32 {
	delta := int64(ts) - j.last
	if delta < -250 || delta > 250 {
		delta = 10
	}
	j.corrected += delta
	if j.corrected < 0 {
		j.corrected = 0
	}
	j.last = int64(ts)
	return uint32(j.corrected)
}

// joinAt returns what a player that joined at publisher time join gets up to
// publisher time newest: the sequence headers, then the GOP cache from the
// last IDR, as SRS stamps them with time_jitter full.
func joinAt(tags []rtmpMessage, join, newest uint32) []rtmpMessage {
	duration := loopDuration(tags)
	idr := join - join%duration%2000
	jitter := &fullJitter{last: 0, corrected: -1}
	var got []rtmpMessage
	for _, sh := range tags[:2] {
		m := sh
		m.timestamp = jitter.correct(m.timestamp)
		got = append(got, m)
	}
	for loop := uint32(0); loop*duration <= newest; loop++ {
		for _, tag := range tags[2:] {
			ts := loop*duration + tag.timestamp
			if ts < idr || ts > newest {
				continue
			}
			m := tag
			m.timestamp = jitter.correct(ts)
			got = append(got, m)
		}
	}
	return got
}

func newTestPlayers(clients int, tags []rtmpMessage) *workerPlayers {
	return newWorkerPlayers(clients, newSourceTimeline(tags))
}

func TestSourceTimelineNamesUniqueVideoFrames(t *testing.T) {
	tags := cbrLikeTags()
	tags = append(tags, rtmpMessage{timestamp: 6000, typeID: 9, payload: []byte{0x27, 1, 0, 0, 0, 0, 80}})
	timeline := newSourceTimeline(tags)
	if timeline.duration != 6040 {
		t.Fatalf("duration = %d, want 6040", timeline.duration)
	}
	if at, ok := timeline.inLoop(tags[4]); !ok || at != 40 {
		t.Fatalf("frame at 40: at=%d ok=%v", at, ok)
	}
	for _, m := range []rtmpMessage{tags[0], tags[1], tags[3], tags[6]} {
		if at, ok := timeline.inLoop(m); ok {
			t.Fatalf("type %d payload %x: at=%d, want no time (header, audio or repeated payload)", m.typeID, m.payload, at)
		}
	}
	if timeline.frames == 0 {
		t.Fatal("no unique video frames")
	}
}

// A source whose video frames all repeat cannot measure lag, so a run refuses it.
func TestLagTimelineNeedsUniqueFrames(t *testing.T) {
	same := []rtmpMessage{{timestamp: 0, typeID: 9, payload: []byte{0x17, 1, 9}}, {timestamp: 40, typeID: 9, payload: []byte{0x17, 1, 9}}}
	if _, err := lagTimeline("same.flv", same); err == nil {
		t.Fatal("a source with no unique video frame was accepted")
	}
	if _, err := lagTimeline("cbr.flv", cbrLikeTags()); err != nil {
		t.Fatal(err)
	}
}

// SRS_PERF_SOURCE names a real source, such as a copy of the image's
// /srs/perf/cbr.flv, to check that every video frame has a unique payload.
func TestRealSourceFramesAreUnique(t *testing.T) {
	media := os.Getenv("SRS_PERF_SOURCE")
	if media == "" {
		t.Skip("SRS_PERF_SOURCE not set")
	}
	tags, err := loadFLV(media)
	if err != nil {
		t.Fatal(err)
	}
	video := 0
	for _, tag := range tags {
		if isVideoFrame(tag) {
			video++
		}
	}
	timeline, err := lagTimeline(media, tags)
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("%s: %d of %d video frames unique, loop %dms", media, timeline.frames, video, timeline.duration)
	if timeline.frames != video {
		t.Fatalf("%d of %d video frames unique", timeline.frames, video)
	}
}

// P4.8a: a player that joins after the first IDR gets the GOP cache stamped
// from about 10ms by SRS's time_jitter full; comparing its timestamps with the
// publisher's read it about one GOP (2s) behind while it was live.
func TestLateJoinerIsNotAGOPBehind(t *testing.T) {
	tags := cbrLikeTags()
	w := newTestPlayers(2, tags)
	for _, m := range joinAt(tags, 300, 4400) {
		w.received(0, m)
	}
	late := joinAt(tags, 2300, 4400)
	if late[2].timestamp > 20 {
		t.Fatalf("the late player's first frame is at %d, want about 10 (time_jitter full)", late[2].timestamp)
	}
	for _, m := range late {
		w.received(1, m)
	}
	if got := w.sample([]uint32{4400}, false); got.LagMillis < 0 || got.LagMillis > 100 {
		t.Fatalf("lag = %dms, want about 0; the late joiner is live", got.LagMillis)
	}
}

// A player that really falls behind still fails the 2s rule, and its lag keeps
// growing while it gets nothing.
func TestPlayerBehindIsStillLagging(t *testing.T) {
	tags := cbrLikeTags()
	w := newTestPlayers(1, tags)
	for _, m := range joinAt(tags, 2300, 2400) {
		w.received(0, m)
	}
	if got := w.sample([]uint32{2440}, false); got.LagMillis > 100 {
		t.Fatalf("first lag = %dms, want about 40", got.LagMillis)
	}
	if got := w.sample([]uint32{4900}, false); got.LagMillis < 2400 || got.LagMillis > 2600 {
		t.Fatalf("lag = %dms, want about 2500", got.LagMillis)
	}
}

// Players split over several streams each measure lag against their own
// stream's publisher, which started at its own time.
func TestPlayersLagAgainstTheirOwnStream(t *testing.T) {
	tags := cbrLikeTags()
	w := newTestPlayers(2, tags)
	w.streams = []int{0, 1}
	for _, m := range joinAt(tags, 300, 4400) {
		w.received(0, m)
	}
	for _, m := range joinAt(tags, 300, 2400) {
		w.received(1, m)
	}
	if got := w.sample([]uint32{4400, 2400}, false); got.LagMillis < 0 || got.LagMillis > 100 {
		t.Fatalf("lag = %dms, want about 0; each player is live on its own stream", got.LagMillis)
	}
}

// The publisher loops the source; a frame of the next loop that arrives
// before the parent's newest time moved on is not a loop behind.
func TestLagAcrossTheSourceLoop(t *testing.T) {
	tags := cbrLikeTags()
	d := loopDuration(tags)
	w := newTestPlayers(1, tags)
	for _, m := range joinAt(tags, 4100, d+40) {
		w.received(0, m)
	}
	if got := w.sample([]uint32{d + 20}, false); got.LagMillis > 100 {
		t.Fatalf("lag at the loop end = %dms, want about 0", got.LagMillis)
	}
	w = newTestPlayers(1, tags)
	for _, m := range joinAt(tags, 2*d+2100, 2*d+2200) {
		w.received(0, m)
	}
	if got := w.sample([]uint32{2*d + 2300}, false); got.LagMillis < 50 || got.LagMillis > 150 {
		t.Fatalf("lag in the third loop = %dms, want about 100", got.LagMillis)
	}
}

// With no source, as with -no-publisher, players have no lag to measure.
func TestPlayersWithoutSourceHaveNoLag(t *testing.T) {
	w := newWorkerPlayers(1, nil)
	w.received(0, rtmpMessage{timestamp: 10, typeID: 9, payload: []byte{0x17, 1}})
	if got := w.sample([]uint32{5000}, false); got.LagMillis != 0 || got.Frames != 1 {
		t.Fatalf("sample = %+v", got)
	}
}

func TestWorkerPlayersCountFrames(t *testing.T) {
	w := newWorkerPlayers(1, nil)
	w.received(0, rtmpMessage{timestamp: 10, typeID: 8, payload: []byte{1, 2, 3}})
	got := w.sample(nil, true)
	if got.Frames != 1 || got.Bytes != 3 || got.Failures != 0 {
		t.Fatalf("sample = %+v", got)
	}
	if last := w.last[0].Load(); time.Since(time.Unix(0, last)) > time.Second {
		t.Fatal("received did not mark the player's last frame")
	}
}
