package main

import (
	"bytes"
	"math"
	"os"
	"reflect"
	"testing"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
)

var (
	testSPS = []byte{0x67, 0x64, 0x00, 0x1f, 0xac, 0xd9}
	testPPS = []byte{0x68, 0xee, 0x3c, 0x80}
)

// tsTestTags is a short FLV source: the AVC and AAC sequence headers, then
// two GOPs of an IDR and a P frame each, with AAC frames between them. An
// audio frame comes first, as in a real source, before any video frame.
func tsTestTags() []rtmpMessage {
	config := []byte{1, 0x64, 0x00, 0x1f, 0xff, 0xe1, 0, byte(len(testSPS))}
	config = append(config, testSPS...)
	config = append(config, 1, 0, byte(len(testPPS)))
	config = append(config, testPPS...)
	return []rtmpMessage{
		{timestamp: 0, typeID: 9, payload: append([]byte{0x17, 0, 0, 0, 0}, config...)},
		{timestamp: 0, typeID: 8, payload: []byte{0xaf, 0, 0x12, 0x10}},
		{timestamp: 0, typeID: 8, payload: []byte{0xaf, 1, 0x21, 0x10, 0x05}},
		{timestamp: 0, typeID: 9, payload: avcc(0x17, append([]byte{0x65}, bytes.Repeat([]byte{0xaa}, 3000)...))},
		{timestamp: 23, typeID: 8, payload: []byte{0xaf, 1, 0x21, 0x10, 0x06}},
		{timestamp: 40, typeID: 9, payload: avcc(0x27, []byte{0x41, 0x9a, 0x01})},
		{timestamp: 46, typeID: 8, payload: []byte{0xaf, 1, 0x21, 0x10, 0x07}},
		{timestamp: 2000, typeID: 9, payload: avcc(0x17, append([]byte{0x65}, bytes.Repeat([]byte{0xbb}, 500)...))},
		{timestamp: 2040, typeID: 9, payload: avcc(0x27, []byte{0x41, 0x9a, 0x02})},
		{timestamp: 2046, typeID: 8, payload: []byte{0xaf, 1, 0x21, 0x10, 0x08}},
	}
}

type tsTestFrame struct {
	video bool
	pts   int64
	data  [][]byte
}

// demuxAll feeds TS to a demuxer in chunks of the given size and returns the
// frames it reports.
func demuxAll(t *testing.T, ts []byte, chunk int) []tsTestFrame {
	t.Helper()
	var frames []tsTestFrame
	// The demuxer's data is valid only in its callback.
	d := newTSDemuxer(func(pts int64, au [][]byte) {
		frame := tsTestFrame{video: true, pts: pts}
		for _, nalu := range au {
			frame.data = append(frame.data, bytes.Clone(nalu))
		}
		frames = append(frames, frame)
	}, func(pts int64, data []byte) {
		data = bytes.Clone(data)
		var adts mpeg4audio.ADTSPackets
		if err := adts.Unmarshal(data); err != nil {
			t.Fatalf("audio at %d: %v", pts, err)
		}
		frame := tsTestFrame{pts: pts}
		for _, pkt := range adts {
			frame.data = append(frame.data, pkt.AU)
		}
		frames = append(frames, frame)
	})
	for len(ts) > 0 {
		n := min(chunk, len(ts))
		if err := d.write(ts[:n]); err != nil {
			t.Fatal(err)
		}
		ts = ts[n:]
	}
	return frames
}

func muxAll(t *testing.T, tags []rtmpMessage) ([]byte, []int) {
	t.Helper()
	m, err := newTSMuxer(tags)
	if err != nil {
		t.Fatal(err)
	}
	var ts []byte
	var starts []int
	for _, tag := range tags {
		b, err := m.mux(tag)
		if err != nil {
			t.Fatal(err)
		}
		if len(b)%188 != 0 {
			t.Fatalf("tag at %dms muxed to %d bytes, not whole TS packets", tag.timestamp, len(b))
		}
		starts = append(starts, len(ts))
		ts = append(ts, b...)
	}
	return ts, starts
}

// The SRT publisher muxes the FLV source to TS: each H.264 frame with SPS and
// PPS before an IDR, each AAC frame as ADTS, in 90kHz time from the tag's
// timestamp. Audio before the first video frame is dropped, so video carries
// the PCR. The players' demuxer gets the same access units and AAC frames
// back, however the TS is split into messages. A video PES has no length, so
// it ends at the next one: each video frame comes one frame late, after the
// audio sent with it, and the last one waits for a next.
func TestTSMuxDemuxRoundTrip(t *testing.T) {
	ts, _ := muxAll(t, tsTestTags())
	want := []tsTestFrame{
		{pts: 23 * 90, data: [][]byte{{0x21, 0x10, 0x06}}},
		{video: true, pts: 0, data: [][]byte{testSPS, testPPS, append([]byte{0x65}, bytes.Repeat([]byte{0xaa}, 3000)...)}},
		{pts: 46 * 90, data: [][]byte{{0x21, 0x10, 0x07}}},
		{video: true, pts: 40 * 90, data: [][]byte{{0x41, 0x9a, 0x01}}},
		{video: true, pts: 2000 * 90, data: [][]byte{testSPS, testPPS, append([]byte{0x65}, bytes.Repeat([]byte{0xbb}, 500)...)}},
		{pts: 2046 * 90, data: [][]byte{{0x21, 0x10, 0x08}}},
	}
	for _, chunk := range []int{188, 1316, 100, len(ts)} {
		got := demuxAll(t, ts, chunk)
		if !reflect.DeepEqual(got, want) {
			t.Errorf("chunk %d: frames =\n%+v\nwant\n%+v", chunk, got, want)
		}
	}
}

// PAT and PMT go out before each IDR, so a player that joins mid-stream, as
// every SRT player does since SRS keeps no GOP cache for SRT, starts at the
// next keyframe.
func TestTSDemuxerJoinsAtTheNextKeyframe(t *testing.T) {
	ts, starts := muxAll(t, tsTestTags())
	got := demuxAll(t, ts[starts[5]:], 1316)
	if len(got) == 0 || !got[0].video || got[0].pts != 2000*90 || !bytes.Equal(got[0].data[0], testSPS) {
		t.Fatalf("a late joiner's first frame = %+v, want the IDR at 2000ms", got)
	}
}

func TestTSDemuxerRejectsLostSync(t *testing.T) {
	d := newTSDemuxer(func(int64, [][]byte) {}, func(int64, []byte) {})
	if err := d.write(bytes.Repeat([]byte{0x46}, 188)); err == nil {
		t.Fatal("a packet without the 0x47 sync byte was accepted")
	}
}

func TestTSMuxerNeedsAVCSequenceHeader(t *testing.T) {
	if _, err := newTSMuxer([]rtmpMessage{{typeID: 8, payload: []byte{0xaf, 0, 0x12, 0x10}}}); err == nil {
		t.Fatal("a source without an AVC sequence header was accepted")
	}
}

// A player times its frames in milliseconds from its first video PTS, across
// the 33-bit PTS wrap.
func TestPTSClockWraps(t *testing.T) {
	var c ptsClock
	const wrap = int64(1) << 33
	for _, tt := range []struct {
		pts  int64
		want uint32
	}{{wrap - 90*40, 0}, {wrap - 90*20, 20}, {10 * 90, 50}, {100 * 90, 140}} {
		if got := c.millis(tt.pts); got != tt.want {
			t.Errorf("millis(%d) = %d, want %d", tt.pts, got, tt.want)
		}
	}
}

// SRT carries the CBR source as TS, so its bitrate is checked like the
// source's: each whole second after the first within 3% of their mean.
// SRS_PERF_SOURCE names a real source, such as a copy of the image's
// /srs/perf/cbr.flv.
func TestRealSourceTSIsFlat(t *testing.T) {
	media := os.Getenv("SRS_PERF_SOURCE")
	if media == "" {
		t.Skip("SRS_PERF_SOURCE not set")
	}
	tags, err := loadFLV(media)
	if err != nil {
		t.Fatal(err)
	}
	m, err := newTSMuxer(tags)
	if err != nil {
		t.Fatal(err)
	}
	perSecond := map[uint32]int{}
	for _, tag := range tags {
		b, err := m.mux(tag)
		if err != nil {
			t.Fatal(err)
		}
		perSecond[tag.timestamp/1000] += len(b)
	}
	whole := int(loopDuration(tags) / 1000)
	var sum float64
	for s := 1; s < whole; s++ {
		sum += float64(perSecond[uint32(s)])
	}
	mean := sum / float64(whole-1)
	lo, hi := math.Inf(1), math.Inf(-1)
	for s := 1; s < whole; s++ {
		kbps := float64(perSecond[uint32(s)]) * 8 / 1000
		lo, hi = math.Min(lo, kbps), math.Max(hi, kbps)
		if math.Abs(float64(perSecond[uint32(s)])-mean) > 0.03*mean {
			t.Errorf("second %d: %.1f kbps, more than 3%% from the mean %.1f", s, kbps, mean*8/1000)
		}
	}
	t.Logf("%s as TS: %d whole seconds after the first, %.1f-%.1f kbps, mean %.1f, first second %.1f", media, whole-1, lo, hi, mean*8/1000, float64(perSecond[0])*8/1000)
}
