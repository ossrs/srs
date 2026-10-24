package main

import (
	"bytes"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/webrtc/v4"
)

// oggPage is one Ogg page with the packets' lacing values; the reader does
// not check the CRC, so it is left 0.
func oggPage(packets ...[]byte) []byte {
	var lacing, body []byte
	for _, p := range packets {
		n := len(p)
		for ; n >= 255; n -= 255 {
			lacing = append(lacing, 255)
		}
		lacing = append(lacing, byte(n))
		body = append(body, p...)
	}
	page := append([]byte("OggS"), make([]byte, 22)...)
	page = append(page, byte(len(lacing)))
	page = append(page, lacing...)
	return append(page, body...)
}

// opusCELT20 is a 20ms CELT fullband Opus packet of one frame (TOC config
// 31, code 0) of the given size, its bytes marked by id.
func opusCELT20(size int, id byte) []byte {
	p := bytes.Repeat([]byte{id}, size)
	p[0] = 31<<3 | 0x04
	return p
}

// writeOpus writes an Ogg Opus file of the head, the tags and the packets,
// a few packets per page, as ffmpeg writes them.
func writeOpus(t *testing.T, dir string, packets [][]byte) string {
	t.Helper()
	data := oggPage([]byte("OpusHead\x01\x02\x38\x01\x80\xbb\x00\x00\x00\x00\x00"))
	data = append(data, oggPage([]byte("OpusTags\x00\x00\x00\x00\x00\x00\x00\x00"))...)
	for len(packets) > 0 {
		n := min(len(packets), 3)
		data = append(data, oggPage(packets[:n]...)...)
		packets = packets[n:]
	}
	path := filepath.Join(dir, "cbr-opus.ogg")
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

// cbrOpus is seconds of 20ms packets of 80 bytes, 32 kbps, each marked by
// its index.
func cbrOpus(seconds int) [][]byte {
	var packets [][]byte
	for i := 0; i < seconds*50; i++ {
		packets = append(packets, opusCELT20(80, byte(i)))
	}
	return packets
}

func TestOpusSourcePath(t *testing.T) {
	if got := opusSourcePath("/srs/perf/cbr.flv"); got != "/srs/perf/cbr-opus.ogg" {
		t.Fatalf("opusSourcePath = %q", got)
	}
}

// A packet's duration comes from its TOC byte: the frame size of its config
// times its frame count (RFC 6716 3.1).
func TestOpusPacketMillis(t *testing.T) {
	for _, tt := range []struct {
		packet []byte
		want   float64
	}{
		{[]byte{31<<3 | 0}, 20},       // CELT 20ms, one frame
		{[]byte{28<<3 | 0}, 2.5},      // CELT 2.5ms
		{[]byte{1<<3 | 0}, 20},        // SILK 20ms
		{[]byte{3<<3 | 0}, 60},        // SILK 60ms
		{[]byte{13<<3 | 0}, 20},       // hybrid 20ms
		{[]byte{30<<3 | 1}, 20},       // two 10ms frames, equal size
		{[]byte{30<<3 | 2}, 20},       // two 10ms frames, different sizes
		{[]byte{31<<3 | 3, 0x82}, 40}, // code 3, two 20ms frames, padded
		{[]byte{31<<3 | 3}, 0},        // code 3 without its count
		{nil, 0},
	} {
		if got := opusPacketMillis(tt.packet); got != tt.want {
			t.Errorf("opusPacketMillis(%x) = %v, want %v", tt.packet, got, tt.want)
		}
	}
}

// The reader splits pages into packets by their lacing values, across a
// packet of 255 bytes or more, and skips the head and the tags.
func TestLoadOpusReadsPackets(t *testing.T) {
	packets := cbrOpus(4)
	packets[7] = opusCELT20(80, 7)
	path := writeOpus(t, t.TempDir(), packets)
	got, err := loadOpus(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != len(packets) {
		t.Fatalf("got %d packets, want %d", len(got), len(packets))
	}
	for i := range packets {
		if !bytes.Equal(got[i], packets[i]) {
			t.Fatalf("packet %d differs", i)
		}
	}
	// A 600-byte packet takes three lacing values: 255, 255, 90.
	big := oggPage(opusCELT20(600, 1), opusCELT20(80, 2))
	split, err := oggPackets(big)
	if err != nil || len(split) != 2 || len(split[0]) != 600 || len(split[1]) != 80 {
		t.Fatalf("oggPackets = %d packets, %v", len(split), err)
	}
}

// Like the source check, each whole second after the first is within 3% of
// the mean; only 20ms packets are paced right, so other durations are
// refused; a file that is not Ogg Opus is refused.
func TestLoadOpusRefusesUnflatOrOther(t *testing.T) {
	dir := t.TempDir()
	unflat := cbrOpus(4)
	for i := 100; i < 150; i++ {
		unflat[i] = opusCELT20(100, byte(i))
	}
	if _, err := loadOpus(writeOpus(t, dir, unflat)); err == nil || !strings.Contains(err.Error(), "not flat") {
		t.Fatalf("unflat Opus: err = %v", err)
	}
	other := cbrOpus(4)
	other[3] = []byte{28<<3 | 0, 1, 2}
	if _, err := loadOpus(writeOpus(t, dir, other)); err == nil || !strings.Contains(err.Error(), "20ms") {
		t.Fatalf("2.5ms packet: err = %v", err)
	}
	if _, err := loadOpus(writeOpus(t, dir, cbrOpus(1))); err == nil {
		t.Fatal("a one-second Opus file was accepted")
	}
	bad := filepath.Join(dir, "bad.ogg")
	os.WriteFile(bad, []byte("not ogg"), 0o644)
	if _, err := loadOpus(bad); err == nil {
		t.Fatal("a file that is not Ogg was accepted")
	}
	if _, err := loadOpus(filepath.Join(dir, "missing.ogg")); err == nil {
		t.Fatal("a missing file was accepted")
	}
}

// The publisher sends the Opus packets in step with the source: packet k is
// due at k*20ms of publisher time, and loops over the file.
func TestOpusPacer(t *testing.T) {
	p := opusPacer{packets: [][]byte{{1}, {2}, {3}}}
	var sent []string
	send := func(ts uint32) {
		p.due(ts, func(rtpTime uint32, payload []byte) {
			sent = append(sent, fmt.Sprintf("%d:%d", rtpTime, payload[0]))
		})
	}
	send(0)
	send(39)
	send(40)
	send(100)
	want := "0:1 960:2 1920:3 2880:1 3840:2 4800:3"
	if got := strings.Join(sent, " "); got != want {
		t.Fatalf("sent %s, want %s", got, want)
	}
}

// rtcTestSFU is a WHIP and WHEP server with pion: it forwards the RTP of the
// stream /live/<stream> from its one publisher to every player.
type rtcTestSFU struct {
	stream       string
	video, audio *webrtc.TrackLocalStaticRTP
	mu           sync.Mutex
	pcs          []*webrtc.PeerConnection
	// deleted counts the DELETEs of the publisher's Location.
	deleted atomic.Int32
	// videoReports and audioReports count the publisher's RTCP sender reports
	// of each track's SSRC.
	videoReports, audioReports atomic.Int32
	// reports are the publisher's sender reports of each track's SSRC, by
	// kind, "video" or "audio", in the order received.
	reports map[string][]rtcp.SenderReport
}

func startRTCTestSFU(t *testing.T, stream string) *httptest.Server {
	server, _ := startRTCTestSFUWith(t, stream)
	return server
}

func startRTCTestSFUWith(t *testing.T, stream string) (*httptest.Server, *rtcTestSFU) {
	t.Helper()
	video, err := webrtc.NewTrackLocalStaticRTP(rtcVideoCodec.RTPCodecCapability, "video", "sfu")
	if err != nil {
		t.Fatal(err)
	}
	audio, err := webrtc.NewTrackLocalStaticRTP(rtcAudioCodec.RTPCodecCapability, "audio", "sfu")
	if err != nil {
		t.Fatal(err)
	}
	sfu := &rtcTestSFU{stream: stream, video: video, audio: audio, reports: map[string][]rtcp.SenderReport{}}
	server := httptest.NewServer(sfu)
	t.Cleanup(func() {
		server.Close()
		sfu.mu.Lock()
		defer sfu.mu.Unlock()
		for _, pc := range sfu.pcs {
			pc.Close()
		}
	})
	return server, sfu
}

func (s *rtcTestSFU) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	publish := r.URL.Path == "/rtc/v1/whip/"
	if r.Method == http.MethodDelete && publish && q.Get("session") == "1" {
		s.deleted.Add(1)
		w.WriteHeader(http.StatusOK)
		return
	}
	if r.Method != http.MethodPost || (!publish && r.URL.Path != "/rtc/v1/whep/") || q.Get("app") != "live" || q.Get("stream") != s.stream {
		http.Error(w, "no stream", http.StatusNotFound)
		return
	}
	offer, _ := io.ReadAll(r.Body)
	api, err := rtcAPI()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	pc, err := api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	s.mu.Lock()
	s.pcs = append(s.pcs, pc)
	s.mu.Unlock()
	if publish {
		pc.OnTrack(func(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {
			out, reports, kind := s.audio, &s.audioReports, "audio"
			if track.Kind() == webrtc.RTPCodecTypeVideo {
				out, reports, kind = s.video, &s.videoReports, "video"
			}
			go func() {
				for {
					pkts, _, err := receiver.ReadRTCP()
					if err != nil {
						return
					}
					for _, p := range pkts {
						if sr, ok := p.(*rtcp.SenderReport); ok && sr.SSRC == uint32(track.SSRC()) {
							reports.Add(1)
							s.mu.Lock()
							s.reports[kind] = append(s.reports[kind], *sr)
							s.mu.Unlock()
						}
					}
				}
			}()
			for {
				pkt, _, err := track.ReadRTP()
				if err != nil {
					return
				}
				out.WriteRTP(pkt)
			}
		})
	}
	if err := pc.SetRemoteDescription(webrtc.SessionDescription{Type: webrtc.SDPTypeOffer, SDP: string(offer)}); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	if !publish {
		for _, track := range []webrtc.TrackLocal{s.video, s.audio} {
			if _, err := pc.AddTrack(track); err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
		}
	}
	answer, err := pc.CreateAnswer(nil)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	gathered := webrtc.GatheringCompletePromise(pc)
	if err := pc.SetLocalDescription(answer); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	<-gathered
	w.Header().Set("Location", r.URL.Path+"?session=1")
	w.WriteHeader(http.StatusCreated)
	io.WriteString(w, pc.LocalDescription().SDP)
}

// The WHIP publisher sends the source's H.264, with the SPS and PPS before
// each IDR, and the Opus track in step; a WHEP player rebuilds each H.264
// frame from RTP, which the timeline finds by its first slice, counts the
// Opus packets as audio at the newest video time, and stops cleanly. The
// stopped publisher DELETEs its WHIP resource before it returns, so the next
// step's publisher of the same stream is not refused as busy while SRS has
// yet to read the DTLS close alert.
func TestWHIPPublisherAndWHEPPlayer(t *testing.T) {
	server, sfu := startRTCTestSFUWith(t, "perf-run")
	apiAddr := strings.TrimPrefix(server.URL, "http://")
	dir := t.TempDir()
	media := filepath.Join(dir, "cbr.flv")
	writeOpus(t, dir, cbrOpus(3))
	tags := tsTestTags()
	timeline := newSourceTimeline(tags)
	clock := &mediaClock{}
	stop := make(chan struct{})
	pubDone := make(chan error, 1)
	go func() { pubDone <- publishWHIP(apiAddr, "live", "perf-run", media, tags, clock, stop) }()

	c, err := dialWHEP(apiAddr, "live")
	if err != nil {
		t.Fatal(err)
	}
	playStop := make(chan struct{})
	report := make(chan rtmpMessage, 128)
	done := make(chan error, 1)
	go func() { done <- c.play("perf-run", playStop, report) }()
	anchored, audios := 0, 0
	deadline := time.After(10 * time.Second)
	for anchored < 3 || audios < 10 {
		select {
		case m := <-report:
			switch m.typeID {
			case 9:
				if _, ok := timeline.inLoop(m); !ok {
					t.Fatalf("video frame of %d bytes at %dms is not in the source", len(m.payload), m.timestamp)
				}
				anchored++
			case 8:
				if len(m.payload) != 80 {
					t.Fatalf("audio of %d bytes, want an 80-byte Opus packet", len(m.payload))
				}
				audios++
			}
		case err := <-done:
			t.Fatalf("player ended: %v", err)
		case err := <-pubDone:
			t.Fatalf("publisher ended: %v", err)
		case <-deadline:
			t.Fatalf("got %d anchored video frames and %d audio packets", anchored, audios)
		}
	}
	close(playStop)
	go func() {
		for range report {
		}
	}()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("stopped player returned %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("player did not stop")
	}
	close(report)
	close(stop)
	select {
	case err := <-pubDone:
		if err != nil {
			t.Fatalf("stopped publisher returned %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("publisher did not stop")
	}
	if n := sfu.deleted.Load(); n != 1 {
		t.Fatalf("publisher sent %d DELETEs of its Location, want 1", n)
	}
	// SRS converts WebRTC to RTMP only after two sender reports of each track,
	// which put its RTP time on the wall clock; the first goes with the first
	// frame.
	if v, a := sfu.videoReports.Load(), sfu.audioReports.Load(); v < 1 || a < 1 {
		t.Fatalf("publisher sent %d video and %d audio sender reports, want one each at least", v, a)
	}
}

// A sender report pairs the wall time with the publisher time at that wall
// time, as a browser's does: the time since the pace began, in RTP units of 90
// per ms for H.264 and 48 for Opus, the clocks the publisher stamps its
// packets with. It does not use the newest tag's time, which a late send puts
// behind the wall clock: SRS takes each track's rate from two reports, so a
// late report would change the rate and shift the stream's timestamps.
func TestSenderReports(t *testing.T) {
	begun := time.Date(2026, 10, 19, 11, 59, 58, 500_000_000, time.UTC)
	now := begun.Add(2000*time.Millisecond + 500*time.Microsecond)
	pkts := senderReports(now, begun, rtpCount{ssrc: 11, packets: 3, octets: 3000}, rtpCount{ssrc: 22, packets: 100, octets: 8000})
	if len(pkts) != 2 {
		t.Fatalf("got %d packets, want 2", len(pkts))
	}
	video, audio := pkts[0].(*rtcp.SenderReport), pkts[1].(*rtcp.SenderReport)
	if video.SSRC != 11 || video.RTPTime != 180045 || video.PacketCount != 3 || video.OctetCount != 3000 {
		t.Errorf("video report = %+v", video)
	}
	if audio.SSRC != 22 || audio.RTPTime != 96024 || audio.PacketCount != 100 || audio.OctetCount != 8000 {
		t.Errorf("audio report = %+v", audio)
	}
	// 2026-10-19 12:00:00.5005 UTC is 4,001,400,000 s after 1900, and the
	// fraction is 0.5005 of 2^32.
	want := uint64(4001400000)<<32 | uint64(500_500_000)<<32/1_000_000_000
	if video.NTPTime != want || audio.NTPTime != want {
		t.Errorf("NTP times %x and %x, want %x", video.NTPTime, audio.NTPTime, want)
	}
}

// The WHIP publisher's sender reports are its own, one of each track per
// interval, all on one clock: each maps its RTP time to the same wall time of
// publisher time 0. pion's default sender report interceptor would send a
// second series a fraction of a second apart, from its own estimate of the RTP
// time; SRS takes each track's rate from the last two reports of either
// series, so the rate would swing from 87 to 93 per ms for video, and the
// stream's timestamps with it.
func TestWHIPPublisherReportsOnOneClock(t *testing.T) {
	server, sfu := startRTCTestSFUWith(t, "perf-run")
	apiAddr := strings.TrimPrefix(server.URL, "http://")
	dir := t.TempDir()
	media := filepath.Join(dir, "cbr.flv")
	writeOpus(t, dir, cbrOpus(3))
	clock := &mediaClock{}
	stop := make(chan struct{})
	pubDone := make(chan error, 1)
	go func() { pubDone <- publishWHIP(apiAddr, "live", "perf-run", media, tsTestTags(), clock, stop) }()
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) && (clock.begunAt().IsZero() || time.Since(clock.begunAt()) < 2*rtcReportInterval+600*time.Millisecond) {
		time.Sleep(50 * time.Millisecond)
	}
	close(stop)
	select {
	case err := <-pubDone:
		if err != nil {
			t.Fatalf("stopped publisher returned %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("publisher did not stop")
	}
	begun := clock.begunAt()
	if begun.IsZero() {
		t.Fatal("publisher never began")
	}
	sfu.mu.Lock()
	defer sfu.mu.Unlock()
	for kind, rate := range map[string]float64{"video": 90, "audio": 48} {
		reports := sfu.reports[kind]
		// At 0, 1 and 2 intervals, and a third may not have arrived yet.
		if n := len(reports); n < 2 || n > 3 {
			t.Errorf("%s: %d sender reports in %v, want 2 or 3, one per interval", kind, n, 2*rtcReportInterval+600*time.Millisecond)
		}
		for i, sr := range reports {
			secs, frac := int64(sr.NTPTime>>32)-2208988800, sr.NTPTime&0xffffffff
			wall := time.Unix(secs, int64(frac*1_000_000_000>>32))
			zero := wall.Add(-time.Duration(float64(sr.RTPTime) / rate * float64(time.Millisecond)))
			if d := zero.Sub(begun); d < -time.Millisecond || d > time.Millisecond {
				t.Errorf("%s report %d: RTP %d at %v puts publisher time 0 %v from the pace's start", kind, i, sr.RTPTime, wall, d)
			}
		}
	}
}

// The publisher sends its first reports with the first frame, then one per
// interval, as a browser does about each second.
func TestSenderReportDue(t *testing.T) {
	start := time.Unix(100, 0)
	var r reportClock
	for _, tt := range []struct {
		at   time.Duration
		want bool
	}{{0, true}, {10 * time.Millisecond, false}, {rtcReportInterval - time.Millisecond, false}, {rtcReportInterval, true}, {rtcReportInterval + 500*time.Millisecond, false}, {2 * rtcReportInterval, true}} {
		if got := r.due(start.Add(tt.at)); got != tt.want {
			t.Errorf("due at %v = %v, want %v", tt.at, got, tt.want)
		}
	}
}

// A stream the server does not serve is an error with its status, not a
// player that waits.
func TestWHEPPlayerRejectsMissingStream(t *testing.T) {
	server := startRTCTestSFU(t, "perf-run")
	c, err := dialWHEP(strings.TrimPrefix(server.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	err = c.play("missing", make(chan struct{}), make(chan rtmpMessage, 1))
	if err == nil || !strings.Contains(err.Error(), "404") {
		t.Fatalf("err = %v, want a 404 error", err)
	}
}

// The WHIP publisher refuses a source without its flat Opus track.
func TestWHIPPublisherNeedsOpus(t *testing.T) {
	err := publishWHIP("127.0.0.1:1", "live", "perf-run", filepath.Join(t.TempDir(), "cbr.flv"), tsTestTags(), &mediaClock{}, make(chan struct{}))
	if err == nil || !strings.Contains(err.Error(), "opus") {
		t.Fatalf("err = %v, want an Opus source error", err)
	}
}

// SRS_PERF_SOURCE names a real source, such as a copy of the image's
// /srs/perf/cbr.flv with its cbr-opus.ogg beside it, to check the Opus track.
func TestRealSourceOpusIsFlat(t *testing.T) {
	media := os.Getenv("SRS_PERF_SOURCE")
	if media == "" {
		t.Skip("SRS_PERF_SOURCE not set")
	}
	packets, err := loadOpus(opusSourcePath(media))
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("%s: %d packets of 20ms", opusSourcePath(media), len(packets))
}
