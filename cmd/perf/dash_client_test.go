package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// srsMPD is an MPD as SRS writes it: the audio set first, then the video set,
// each with a SegmentTemplate of its own init and numbered segments, timed in
// milliseconds by a SegmentTimeline.
const srsMPD = `<?xml version="1.0" encoding="utf-8"?>
<MPD profiles="urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple"
    ns1:schemaLocation="urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd"
    xmlns="urn:mpeg:dash:schema:mpd:2011" xmlns:ns1="http://www.w3.org/2001/XMLSchema-instance"
    type="dynamic"
    minimumUpdatePeriod="PT5.000S"
    timeShiftBufferDepth="PT50.000S"
    availabilityStartTime="2026-10-19T00:00:00Z"
    publishTime="2026-10-19T00:00:30Z"
    minBufferTime="PT20.000S" >
    <BaseURL>perf-run/</BaseURL>
    <Period start="PT0S">
        <AdaptationSet mimeType="audio/mp4" segmentAlignment="true" startWithSAP="1">
            <Representation id="audio" bandwidth="48000" codecs="mp4a.40.2">
                <SegmentTemplate initialization="$RepresentationID$-init.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="3" timescale="1000">
                    <SegmentTimeline>
                        <S t="20000" d="10023" />
                        <S t="30023" d="9977" />
                    </SegmentTimeline>
                </SegmentTemplate>
            </Representation>
        </AdaptationSet>
        <AdaptationSet mimeType="video/mp4" segmentAlignment="true" startWithSAP="1">
            <Representation id="video" bandwidth="800000" codecs="avc1.4d001f" width="768" height="320">
                <SegmentTemplate initialization="$RepresentationID$-init.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="3" timescale="1000">
                    <SegmentTimeline>
                        <S t="20000" d="10000" r="1" />
                    </SegmentTimeline>
                </SegmentTemplate>
            </Representation>
        </AdaptationSet>
    </Period>
</MPD>
`

// A player reads the MPD's update period, and each track's init and
// segments, numbered from startNumber and timed by the timeline, its repeats
// too; the video track comes first, whatever the MPD's order.
func TestParseMPD(t *testing.T) {
	mpd, err := parseMPD([]byte(srsMPD))
	if err != nil {
		t.Fatal(err)
	}
	if mpd.update != 5*time.Second || mpd.base != "perf-run/" || len(mpd.tracks) != 2 {
		t.Fatalf("mpd = %+v", mpd)
	}
	video, audio := mpd.tracks[0], mpd.tracks[1]
	if !video.video || video.init != "video-init.mp4" || video.timescale != 1000 || audio.video || audio.init != "audio-init.mp4" {
		t.Fatalf("tracks = %+v", mpd.tracks)
	}
	wantVideo := []dashSegment{{3, 10 * time.Second, "video-3.m4s"}, {4, 10 * time.Second, "video-4.m4s"}}
	wantAudio := []dashSegment{{3, 10023 * time.Millisecond, "audio-3.m4s"}, {4, 9977 * time.Millisecond, "audio-4.m4s"}}
	for _, tt := range []struct {
		got, want []dashSegment
	}{{video.segments, wantVideo}, {audio.segments, wantAudio}} {
		if fmt.Sprint(tt.got) != fmt.Sprint(tt.want) {
			t.Errorf("segments = %v, want %v", tt.got, tt.want)
		}
	}
	if _, err := parseMPD([]byte("<html>not found</html>")); err == nil {
		t.Fatal("a body that is not an MPD was accepted")
	}
}

func TestParseISODuration(t *testing.T) {
	for in, want := range map[string]time.Duration{"PT5.000S": 5 * time.Second, "PT0.5S": 500 * time.Millisecond, "PT1M30S": 90 * time.Second, "PT1H": time.Hour} {
		if got, err := parseISODuration(in); err != nil || got != want {
			t.Errorf("%s = %v, %v, want %v", in, got, err, want)
		}
	}
	for _, in := range []string{"", "5S", "PTxS", "P1D"} {
		if _, err := parseISODuration(in); err == nil {
			t.Errorf("%q was accepted", in)
		}
	}
}

func mp4Box(kind string, payload ...[]byte) []byte {
	b := make([]byte, 8)
	copy(b[4:], kind)
	for _, p := range payload {
		b = append(b, p...)
	}
	binary.BigEndian.PutUint32(b, uint32(len(b)))
	return b
}

func be32(v uint32) []byte { return binary.BigEndian.AppendUint32(nil, v) }

// fmp4TestSample is one sample of a test segment, timed in milliseconds.
type fmp4TestSample struct {
	dts, cts int64
	data     []byte
}

// fmp4Segment writes a segment as SRS does: styp, sidx, then one moof whose
// traf has a tfhd with default-base-is-moof, a version 1 tfdt and a trun of
// every sample's duration, size, flags and composition offset, then the mdat.
// The last sample lasts until end.
func fmp4Segment(samples []fmp4TestSample, end int64) []byte {
	trun := append([]byte{0, 0, 0x0f, 0x01}, be32(uint32(len(samples)))...)
	trun = append(trun, 0, 0, 0, 0) // data_offset, set below
	var mdat []byte
	for i, s := range samples {
		next := end
		if i+1 < len(samples) {
			next = samples[i+1].dts
		}
		trun = append(trun, be32(uint32(next-s.dts))...)
		trun = append(trun, be32(uint32(len(s.data)))...)
		trun = append(trun, be32(0x01000000)...)
		trun = append(trun, be32(uint32(s.cts))...)
		mdat = append(mdat, s.data...)
	}
	tfdt := append([]byte{1, 0, 0, 0}, binary.BigEndian.AppendUint64(nil, uint64(samples[0].dts))...)
	moof := mp4Box("moof", mp4Box("mfhd", []byte{0, 0, 0, 0}, be32(1)), mp4Box("traf",
		mp4Box("tfhd", []byte{0, 0x02, 0, 0}, be32(1)), mp4Box("tfdt", tfdt), mp4Box("trun", trun)))
	// trun's data_offset is the moof's size plus the mdat's header.
	at := bytes.Index(moof, []byte("trun")) + 4 + 8
	binary.BigEndian.PutUint32(moof[at:], uint32(len(moof)+8))
	styp := mp4Box("styp", []byte("msdh"), be32(0), []byte("msdhmsix"))
	sidx := mp4Box("sidx", make([]byte, 32))
	return bytes.Join([][]byte{styp, sidx, moof, mp4Box("mdat", mdat)}, nil)
}

// dashTestSegments splits the TS test tags into fMP4 segments as SRS does: a
// video segment per GOP of the AVCC samples, an audio one of the raw AAC
// frames, each numbered from 1.
func dashTestSegments() (video, audio [][]byte) {
	var gops [][]fmp4TestSample
	var frames []fmp4TestSample
	for _, tag := range tsTestTags() {
		if tag.payload[1] != 1 {
			continue
		}
		if tag.typeID == 9 {
			if tag.payload[0]>>4 == 1 {
				gops = append(gops, nil)
			}
			gops[len(gops)-1] = append(gops[len(gops)-1], fmp4TestSample{dts: int64(tag.timestamp), cts: int64(get24(tag.payload[2:])), data: tag.payload[5:]})
		} else {
			frames = append(frames, fmp4TestSample{dts: int64(tag.timestamp), data: tag.payload[2:]})
		}
	}
	video = [][]byte{fmp4Segment(gops[0], 2000), fmp4Segment(gops[1], 2080)}
	audio = [][]byte{fmp4Segment(frames[:3], 2046), fmp4Segment(frames[3:], 2069)}
	return video, audio
}

// The demuxer gives each sample of a segment, its time from the tfdt and the
// durations before it, plus its composition offset, and its bytes.
func TestFMP4DemuxerReadsSamples(t *testing.T) {
	video, audio := dashTestSegments()
	var d fmp4Demuxer
	var got []string
	for _, segment := range [][]byte{video[1], audio[0]} {
		// One byte at a time, as a slow body.
		err := d.read(&oneByteReader{bytes.NewReader(segment)}, func(dts int64, cts int32, data []byte) {
			got = append(got, fmt.Sprintf("%d+%d:%x", dts, cts, data[:min(len(data), 6)]))
		})
		if err != nil {
			t.Fatal(err)
		}
	}
	want := []string{"2000+0:000001f565bb", "2040+0:00000003419a", "0+0:211005", "23+0:211006", "46+0:211007"}
	if fmt.Sprint(got) != fmt.Sprint(want) {
		t.Fatalf("samples = %v, want %v", got, want)
	}
	if err := d.read(bytes.NewReader(video[0][:len(video[0])-10]), func(int64, int32, []byte) {}); err == nil {
		t.Fatal("a truncated mdat was accepted")
	}
}

type oneByteReader struct{ r *bytes.Reader }

func (o *oneByteReader) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	return o.r.Read(p[:1])
}

// dashTestServer serves a stream as SRS does with dash on: before missing
// requests the MPD is a 404, as before SRS reaps the first segment; then the
// MPD lists the first segment of each track, and from its second request on,
// the second too. The inits and segments are under the stream's directory.
type dashTestServer struct {
	*httptest.Server
	mu       sync.Mutex
	missing  int
	mpds     int
	fetched  []string
	conns    atomic.Int32
	duration string
	segDelay time.Duration
	files    map[string][]byte
}

func newDASHTestServer(t *testing.T, missing int) *dashTestServer {
	video, audio := dashTestSegments()
	s := &dashTestServer{missing: missing, duration: "2000", files: map[string][]byte{
		"/live/perf-run/video-init.mp4": []byte("video init"),
		"/live/perf-run/audio-init.mp4": []byte("audio init"),
		"/live/perf-run/video-1.m4s":    video[0],
		"/live/perf-run/video-2.m4s":    video[1],
		"/live/perf-run/audio-1.m4s":    audio[0],
		"/live/perf-run/audio-2.m4s":    audio[1],
	}}
	s.Server = httptest.NewUnstartedServer(http.HandlerFunc(s.serve))
	s.Server.Config.ConnState = func(_ net.Conn, state http.ConnState) {
		if state == http.StateNew {
			s.conns.Add(1)
		}
	}
	s.Start()
	t.Cleanup(s.Close)
	return s
}

func (s *dashTestServer) serve(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if r.URL.Path == "/live/perf-run.mpd" {
		if s.missing > 0 {
			s.missing--
			http.NotFound(w, r)
			return
		}
		s.mpds++
		timeline := func(start string) string {
			ss := `<S t="` + start + `" d="` + s.duration + `" />`
			if s.mpds > 1 {
				ss += `<S d="` + s.duration + `" />`
			}
			return ss
		}
		fmt.Fprintf(w, `<?xml version="1.0" encoding="utf-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="dynamic" minimumUpdatePeriod="PT0.500S">
<BaseURL>perf-run/</BaseURL><Period start="PT0S">
<AdaptationSet mimeType="audio/mp4"><Representation id="audio"><SegmentTemplate initialization="$RepresentationID$-init.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="1" timescale="1000"><SegmentTimeline>%s</SegmentTimeline></SegmentTemplate></Representation></AdaptationSet>
<AdaptationSet mimeType="video/mp4"><Representation id="video"><SegmentTemplate initialization="$RepresentationID$-init.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="1" timescale="1000"><SegmentTimeline>%s</SegmentTimeline></SegmentTemplate></Representation></AdaptationSet>
</Period></MPD>
`, timeline("0"), timeline("0"))
		return
	}
	file, ok := s.files[r.URL.Path]
	if !ok {
		http.NotFound(w, r)
		return
	}
	s.fetched = append(s.fetched, r.URL.Path)
	if strings.HasSuffix(r.URL.Path, ".m4s") {
		time.Sleep(s.segDelay)
	}
	w.Write(file)
}

func (s *dashTestServer) fetchedFiles() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]string(nil), s.fetched...)
}

// playDASH plays the test server's stream until the reported messages are n,
// or the player ends.
func playDASH(t *testing.T, s *dashTestServer, n int) ([]rtmpMessage, error) {
	t.Helper()
	c, err := dialDASH(strings.TrimPrefix(s.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	stop := make(chan struct{})
	report := make(chan rtmpMessage, 16)
	done := make(chan error, 1)
	go func() { done <- c.play("perf-run", stop, report) }()
	var got []rtmpMessage
	for len(got) < n {
		select {
		case m := <-report:
			got = append(got, m)
		case err := <-done:
			return got, err
		case <-time.After(5 * time.Second):
			t.Fatalf("got %d of %d messages", len(got), n)
		}
	}
	close(stop)
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("stopped player returned %v", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("player did not stop")
	}
	return got, nil
}

// A player fetches each track's init once, skips the segments that were in
// the MPD when it joined, and fetches each new one whole on one keep-alive
// connection: the video segment first, its frames as FLV AVC payloads timed
// in milliseconds, then the segment itself, then the audio segment's frames
// at the newest video time.
func TestDASHPlayerPlaysNewSegments(t *testing.T) {
	s := newDASHTestServer(t, 0)
	got, err := playDASH(t, s, 4)
	if err != nil {
		t.Fatal(err)
	}
	want := []struct {
		typeID    byte
		timestamp uint32
	}{{9, 2000}, {9, 2040}, {segmentMessage, 2040}, {8, 2040}}
	for i, w := range want {
		if got[i].typeID != w.typeID || got[i].timestamp != w.timestamp {
			t.Errorf("message %d = type %d at %d, want type %d at %d", i, got[i].typeID, got[i].timestamp, w.typeID, w.timestamp)
		}
	}
	if idr := tsTestTags()[7].payload; !bytes.Equal(got[0].payload, idr) {
		t.Fatalf("first frame = %x, want the IDR of the second GOP %x", got[0].payload, idr)
	}
	if frame := tsTestTags()[8].payload; !bytes.Equal(got[1].payload, frame) {
		t.Fatalf("second frame = %x, want %x", got[1].payload, frame)
	}
	if fetched := s.fetchedFiles(); fmt.Sprint(fetched) != "[/live/perf-run/video-init.mp4 /live/perf-run/audio-init.mp4 /live/perf-run/video-2.m4s /live/perf-run/audio-2.m4s]" {
		t.Fatalf("fetched %v, want both inits, then only the new segments", fetched)
	}
	if n := s.conns.Load(); n != 1 {
		t.Fatalf("player opened %d connections, want 1 kept alive", n)
	}
}

// A player that joins before SRS has its first segment waits for the MPD;
// then every segment in it is new.
func TestDASHPlayerJoinsBeforeTheStream(t *testing.T) {
	s := newDASHTestServer(t, 2)
	got, err := playDASH(t, s, 3)
	if err != nil {
		t.Fatal(err)
	}
	if got[0].typeID != 9 || got[0].timestamp != 0 || got[1].typeID != 9 || got[1].timestamp != 40 || got[2].typeID != segmentMessage {
		t.Fatalf("messages = %+v, want the first GOP, then its segment", got)
	}
	if fetched := s.fetchedFiles(); len(fetched) < 3 || fetched[2] != "/live/perf-run/video-1.m4s" {
		t.Fatalf("fetched %v, want the inits, then the first segment", fetched)
	}
}

// A segment that takes longer to fetch than its duration fails the player:
// it would stall a real one.
func TestDASHPlayerFailsSlowSegment(t *testing.T) {
	s := newDASHTestServer(t, 1)
	s.duration, s.segDelay = "100", 300*time.Millisecond
	if _, err := playDASH(t, s, 100); err == nil || !strings.Contains(err.Error(), "longer than") {
		t.Fatalf("err = %v, want a slow segment error", err)
	}
}

// A segment or init SRS does not serve fails the player.
func TestDASHPlayerFailsMissingFile(t *testing.T) {
	for _, path := range []string{"/live/perf-run/video-1.m4s", "/live/perf-run/audio-init.mp4"} {
		s := newDASHTestServer(t, 1)
		delete(s.files, path)
		if _, err := playDASH(t, s, 100); err == nil || !strings.Contains(err.Error(), "404") {
			t.Fatalf("without %s: err = %v, want a 404 error", path, err)
		}
	}
}

// The DASH scenario writes DASH with the shipped defaults and serves it from
// SRS's HTTP server on 8080, the port its players dial.
func TestDASHScenarioEnvServesDASH(t *testing.T) {
	data, err := os.ReadFile(filepath.Join("env", "rtmp-dash.env"))
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range []string{"SRS_RTMP_LISTEN=1935", "SRS_HTTP_SERVER_ENABLED=on", "SRS_HTTP_SERVER_LISTEN=" + dashPort, "SRS_VHOST_DASH_ENABLED=on"} {
		if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
			t.Errorf("rtmp-dash.env has no %s", line)
		}
	}
	if regexp.MustCompile(`(?m)^SRS_VHOST_DASH_DASH_`).Match(data) {
		t.Error("rtmp-dash.env changes a dash setting; D30 takes the shipped defaults")
	}
	s, ok := scenarioByName("rtmp-dash")
	if !ok || s.Publisher != "rtmp" || s.Player != "dash" || s.segment != 10 || s.rampStart() != 100 || s.rampCap() != defaultClientCap {
		t.Fatalf("rtmp-dash = %+v, %v", s, ok)
	}
	if _, err := playerDialer("dash"); err != nil {
		t.Fatal(err)
	}
}
