// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"bytes"
	"cmp"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"slices"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/pion/ice/v4"
	"github.com/pion/rtp"
	"github.com/pion/sdp/v3"
	"github.com/pion/webrtc/v4"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/ts"
)

// TestReadMP4 checks the samples of the WHIP test file: H.264 in Annex B
// with the parameter sets before each keyframe, and Opus, in decode order.
func TestReadMP4(t *testing.T) {
	path := media.Path(t, media.H264BaselineOpus)
	samples, err := ReadMP4(path)
	if err != nil {
		t.Fatal(err)
	}
	var videos, audios, keys int
	var last time.Duration
	for i, s := range samples {
		if s.DTS < last {
			t.Fatalf("sample %v at %v is before %v", i, s.DTS, last)
		}
		last = s.DTS
		if !s.Video {
			audios++
			continue
		}
		videos++
		types := nalTypes(s.Data)
		if s.Key {
			keys++
			if len(types) < 3 || types[0] != 7 || types[1] != 8 || !contains(types, 5) {
				t.Fatalf("keyframe at %v has NALs %v, want SPS, PPS, then an IDR", s.DTS, types)
			}
		} else if contains(types, 7) || contains(types, 5) {
			t.Fatalf("frame at %v has NALs %v, want no SPS or IDR", s.DTS, types)
		}
	}

	f, err := openMP4(t, path)
	if err != nil {
		t.Fatal(err)
	}
	if want := len(f.Track("vide").Samples); videos != want {
		t.Errorf("%v video samples, want %v", videos, want)
	}
	if want := len(f.Track("soun").Samples); audios != want {
		t.Errorf("%v audio samples, want %v", audios, want)
	}
	if keys != 30 {
		t.Errorf("%v keyframes, want 30 in 30s", keys)
	}
	if len(samples) == 0 || !samples[0].Video || !samples[0].Key || samples[0].DTS != 0 {
		t.Errorf("first sample %+v, want a keyframe at 0", first(samples))
	}
}

// TestAnalyzerFromSamples packetizes the first 2s of the test file as a
// publisher does and analyzes the packets: the report is the file's.
func TestAnalyzerFromSamples(t *testing.T) {
	samples := readSamples(t, 2*time.Second)
	video, audio := newVideoSender(), newAudioSender()
	var a Analyzer
	for _, s := range samples {
		snd, codec, clock := audio, "opus", uint32(48000)
		if s.Video {
			snd, codec, clock = video, "h264", 90000
		}
		for _, p := range snd.packets(s) {
			if err := a.Add(codec, clock, p); err != nil {
				t.Fatal(err)
			}
		}
	}
	r := a.Report()
	checkTwoSeconds(t, r)
}

// TestAnalyzerTimestampWrap checks decode times go on past the 32-bit RTP
// timestamp wrap.
func TestAnalyzerTimestampWrap(t *testing.T) {
	var a Analyzer
	base := uint32(0xffffffff - 90000/25)
	for i := range 4 {
		p := &rtp.Packet{Header: rtp.Header{Version: 2, Marker: true, SequenceNumber: uint16(i), Timestamp: base + uint32(i*90000/25)}, Payload: []byte{0x41, 0x9a, 0x01}}
		if err := a.Add("h264", 90000, p); err != nil {
			t.Fatal(err)
		}
	}
	v := a.Report().Video
	if v == nil || v.Frames != 4 || v.Backwards != 0 || v.LastDTS != 120*time.Millisecond {
		t.Fatalf("video %+v, want 4 frames to 120ms, none back", v)
	}
}

// TestAnalyzerLostMarker checks a frame whose last packet is lost is counted
// when the next frame starts.
func TestAnalyzerLostMarker(t *testing.T) {
	var a Analyzer
	add := func(seq uint16, ts uint32, marker bool) {
		p := &rtp.Packet{Header: rtp.Header{Version: 2, Marker: marker, SequenceNumber: seq, Timestamp: ts}, Payload: []byte{0x41, 0x9a, 0x01}}
		if err := a.Add("h264", 90000, p); err != nil {
			t.Fatal(err)
		}
	}
	add(1, 0, false)
	add(2, 3600, true)
	if v := a.Report().Video; v == nil || v.Frames != 2 || v.LastDTS != 40*time.Millisecond {
		t.Fatalf("video %+v, want 2 frames, the last at 40ms", v)
	}
}

// An arrival is an RTP packet of a track and when it reaches a player.
type arrival struct {
	at    time.Duration
	codec string
	rate  uint32
	pkt   *rtp.Packet
}

// arrivals packetizes the samples of the test file before d as a publisher
// does, each packet arriving at its sample's decode time plus the lag of its
// track.
func arrivals(t *testing.T, d, videoLag, audioLag time.Duration) (video, audio []arrival) {
	t.Helper()
	vs, as := newVideoSender(), newAudioSender()
	for _, s := range readSamples(t, d) {
		if s.Video {
			for _, p := range vs.packets(s) {
				video = append(video, arrival{s.DTS + videoLag, "h264", 90000, p})
			}
		} else {
			for _, p := range as.packets(s) {
				audio = append(audio, arrival{s.DTS + audioLag, "opus", 48000, p})
			}
		}
	}
	return video, audio
}

// addAt adds the packets to an analysis whose clock reads start plus each
// packet's arrival.
func addAt(a *analysis, clock *time.Time, start time.Time, pkts []arrival) {
	for _, p := range pkts {
		*clock = start.Add(p.at)
		a.add(p.codec, p.rate, p.pkt)
	}
}

// TestAnalysisSyncOfBurst checks the A/V sync of 400ms of media that reaches
// a player at once, as after a stall: its two track readers may add their
// packets in either order, and the sync is the same, within a video frame,
// as for the packets in the order the publisher sent them.
func TestAnalysisSyncOfBurst(t *testing.T) {
	video, audio := arrivals(t, 400*time.Millisecond, 0, 0)
	inOrder := slices.Concat(video, audio)
	slices.SortStableFunc(inOrder, func(x, y arrival) int { return cmp.Compare(x.at, y.at) })
	start := time.Unix(1000, 0)
	for _, order := range []struct {
		name string
		pkts []arrival
	}{
		{"in order", inOrder},
		{"audio first", slices.Concat(audio, video)},
		{"video first", slices.Concat(video, audio)},
	} {
		a := newAnalysis()
		clock := start
		a.now = func() time.Time { return clock }
		for _, p := range order.pkts {
			a.add(p.codec, p.rate, p.pkt)
		}
		clock = start.Add(time.Second)
		r := a.Report()
		if r.Video == nil || r.Audio == nil || r.Video.Frames != 10 || r.Audio.Frames != 20 {
			t.Fatalf("%v: report %v, want 10 video and 20 audio frames", order.name, r)
		}
		if r.AVSync > 40*time.Millisecond {
			t.Errorf("%v: A/V sync %v, want at most one video frame", order.name, r.AVSync)
		}
	}
}

// TestAnalysisSyncOfLateAudio checks a stream whose audio reaches the player
// 300ms after the video of the same moment, in real time: the A/V sync is
// that lag.
func TestAnalysisSyncOfLateAudio(t *testing.T) {
	video, audio := arrivals(t, time.Second, 0, 300*time.Millisecond)
	pkts := slices.Concat(video, audio)
	slices.SortStableFunc(pkts, func(x, y arrival) int { return cmp.Compare(x.at, y.at) })
	a := newAnalysis()
	start := time.Unix(1000, 0)
	clock := start
	a.now = func() time.Time { return clock }
	addAt(a, &clock, start, pkts)
	clock = clock.Add(time.Second)
	if r := a.Report(); r.AVSync < 280*time.Millisecond || r.AVSync > 340*time.Millisecond {
		t.Errorf("A/V sync %v, want about 300ms: %v", r.AVSync, r)
	}
}

// TestAnalysisSyncOfLateAudioBurst checks a stream whose audio starts 300ms
// after its video, the first 300ms of it at once with the video of that
// moment: in either order of the track readers, the A/V sync is the video
// the player had before the first audio.
func TestAnalysisSyncOfLateAudioBurst(t *testing.T) {
	video, audio := arrivals(t, time.Second, 0, 0)
	for i := range audio {
		audio[i].at = max(audio[i].at, 300*time.Millisecond)
	}
	for _, order := range []struct {
		name string
		pkts []arrival
	}{
		{"video first", slices.Concat(video, audio)},
		{"audio first", slices.Concat(audio, video)},
	} {
		slices.SortStableFunc(order.pkts, func(x, y arrival) int { return cmp.Compare(x.at, y.at) })
		a := newAnalysis()
		start := time.Unix(1000, 0)
		clock := start
		a.now = func() time.Time { return clock }
		addAt(a, &clock, start, order.pkts)
		clock = clock.Add(time.Second)
		if r := a.Report(); r.AVSync < 260*time.Millisecond || r.AVSync > 320*time.Millisecond {
			t.Errorf("%v: A/V sync %v, want about 300ms: %v", order.name, r.AVSync, r)
		}
	}
}

// TestAnalysisUntilSettles checks Until returns a report of a burst only
// once the burst is over, with the sync of all of it, not of the part its
// condition saw first.
func TestAnalysisUntilSettles(t *testing.T) {
	video, audio := arrivals(t, 400*time.Millisecond, 0, 0)
	a := newAnalysis()
	for _, p := range slices.Concat(audio, video) {
		a.add(p.codec, p.rate, p.pkt)
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	r, err := a.Until(ctx, report.Until{VideoFrames: 1, AudioFrames: 1})
	if err != nil {
		t.Fatal(err)
	}
	if r.AVSync > 40*time.Millisecond {
		t.Errorf("A/V sync %v, want at most one video frame: %v", r.AVSync, r)
	}
}

// TestPublishPlay publishes the first second of the test file through a fake
// server to a player that connected first: it gets every frame.
func TestPublishPlay(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer player.Close()
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()

	samples := readSamples(t, 2*time.Second)
	if err := pub.Send(ctx, samples, burstPacer(false)); err != nil {
		t.Fatal(err)
	}
	r, err := player.Until(ctx, report.Until{VideoHeader: true, Keyframes: 2, VideoFrames: 50, AudioFrames: 100})
	if err != nil {
		t.Fatal(err)
	}
	checkTwoSeconds(t, r)
}

// TestPublishPaced checks Send paces by decode time.
func TestPublishPaced(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()

	samples := readSamples(t, 300*time.Millisecond)
	start := time.Now()
	if err := pub.Send(ctx, samples, nil); err != nil {
		t.Fatal(err)
	}
	if d := time.Since(start); d > 100*time.Millisecond {
		t.Errorf("unpaced send took %v", d)
	}
	start = time.Now()
	if err := pub.Send(ctx, samples, &rtmp.Pacer{}); err != nil {
		t.Fatal(err)
	}
	if d := time.Since(start); d < 250*time.Millisecond {
		t.Errorf("paced send of 300ms took %v", d)
	}
}

// TestSignaling checks the WHIP and WHEP requests and what the session keeps
// of the answers.
func TestSignaling(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	header := http.Header{"Authorization": {"Bearer abc"}}

	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{Header: header})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{Header: header})
	if err != nil {
		t.Fatal(err)
	}
	defer player.Close()

	reqs := sfu.requests()
	if len(reqs) != 2 {
		t.Fatalf("%v requests, want 2", len(reqs))
	}
	for i, want := range []string{"/rtc/v1/whip/", "/rtc/v1/whep/"} {
		r := reqs[i]
		if r.Method != http.MethodPost || r.Path != want || r.ContentType != "application/sdp" || r.Auth != "Bearer abc" {
			t.Errorf("request %v: %+v, want POST %v of application/sdp with the header", i, r, want)
		}
	}
	if !strings.Contains(reqs[0].Body, "a=sendonly") || !strings.Contains(reqs[1].Body, "a=recvonly") {
		t.Errorf("offers want sendonly for the publisher and recvonly for the player")
	}

	for name, s := range map[string]*Session{"publisher": &pub.Session, "player": &player.Session} {
		if s.Response.StatusCode != http.StatusCreated {
			t.Errorf("%v: status %v", name, s.Response.StatusCode)
		}
		if !strings.HasPrefix(s.Location, sfu.URL+"/rtc/v1/whip/?action=delete&") {
			t.Errorf("%v: location %q, want it resolved against %v", name, s.Location, sfu.URL)
		}
		if !strings.Contains(s.Answer, "a=fingerprint:") || !strings.Contains(s.Offer, "a=ice-ufrag:") {
			t.Errorf("%v: no offer or answer", name)
		}
	}
}

// TestDelete checks DELETE goes to the Location and ends the session.
func TestDelete(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()

	res, err := pub.Delete(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != http.StatusOK {
		t.Errorf("status %v, want 200", res.StatusCode)
	}
	reqs := sfu.requests()
	last := reqs[len(reqs)-1]
	if last.Method != http.MethodDelete || last.Path != "/rtc/v1/whip/" || !strings.Contains(last.Query, "action=delete") {
		t.Errorf("last request %+v, want DELETE of the location", last)
	}
	if err := sfu.waitClosed(ctx); err != nil {
		t.Error(err)
	}

	legacy := &Session{}
	if _, err := legacy.Delete(ctx); err == nil {
		t.Error("DELETE of a session without a location succeeded")
	}
}

// TestStatusError checks an answer other than 201 fails with its status and
// body.
func TestStatusError(t *testing.T) {
	sfu := newFakeSFU(t)
	sfu.reject = &StatusError{Code: http.StatusConflict, Body: "busy"}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", nil)
	var se *StatusError
	if !errors.As(err, &se) || se.Code != http.StatusConflict || se.Body != "busy" {
		t.Fatalf("error %v, want status 409 with body busy", err)
	}
	_, err = Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", nil)
	if !errors.As(err, &se) || se.Code != http.StatusConflict {
		t.Fatalf("play error %v, want status 409", err)
	}
}

// TestLegacy checks the legacy API: the JSON request and answer.
func TestLegacy(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/publish/", &Options{Legacy: "webrtc://localhost/live/s"})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	player, err := Play(ctx, sfu.URL+"/rtc/v1/play/", &Options{Legacy: "webrtc://localhost/live/s"})
	if err != nil {
		t.Fatal(err)
	}
	defer player.Close()

	for i, r := range sfu.requests() {
		var body struct{ API, StreamURL, SDP string }
		if err := json.Unmarshal([]byte(r.Body), &body); err != nil {
			t.Fatalf("request %v: %v", i, err)
		}
		if body.API != sfu.URL+r.Path || body.StreamURL != "webrtc://localhost/live/s" || !strings.Contains(body.SDP, "a=ice-ufrag:") {
			t.Errorf("request %v body %+v, want api, streamurl and sdp", i, body)
		}
	}
	for _, s := range []*Session{&pub.Session, &player.Session} {
		if s.SessionID != "legacy-session" || s.Location != "" || !strings.Contains(s.Answer, "a=fingerprint:") {
			t.Errorf("session id %q, location %q, want the answer's sessionid and no location", s.SessionID, s.Location)
		}
	}

	sfu.reject = &StatusError{Code: 400}
	_, err = Publish(ctx, sfu.URL+"/rtc/v1/publish/", &Options{Legacy: "webrtc://localhost/live/s"})
	var ce *CodeError
	if !errors.As(err, &ce) || ce.Code != 400 {
		t.Errorf("error %v, want code 400", err)
	}
}

// TestUntilTimeout checks Until returns the partial report when ctx ends.
func TestUntilTimeout(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer player.Close()

	short, cancel2 := context.WithTimeout(ctx, 100*time.Millisecond)
	defer cancel2()
	r, err := player.Until(short, report.Until{VideoFrames: 1})
	if err == nil || r == nil || !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("report %v, error %v, want a deadline error with a report", r, err)
	}
}

// checkTwoSeconds checks the report of the first 2s of the test file.
func checkTwoSeconds(t *testing.T, r *report.Report) {
	t.Helper()
	v, a := r.Video, r.Audio
	if v == nil || a == nil {
		t.Fatalf("report %v, want video and audio", r)
	}
	if v.Codec != "h264" || v.Profile != "Baseline" || v.Width != 768 || v.Height != 320 || v.SequenceHeaders != 1 {
		t.Errorf("video %v, want h264 Baseline 768x320 with 1 sequence header", r)
	}
	if v.Keyframes != 2 || v.Frames != 50 || v.FirstDTS != 0 || v.LastDTS != 1960*time.Millisecond {
		t.Errorf("video %v, want 2 keyframes, 50 frames from 0 to 1.96s", r)
	}
	if a.Codec != "opus" || a.SampleRate != 48000 || a.Channels != 2 || a.Frames < 99 || a.Frames > 101 {
		t.Errorf("audio %v, want opus 48000Hz 2ch, 100 frames", r)
	}
	if err := r.CheckTimestamps(25 * time.Millisecond * 2); err != nil {
		t.Error(err)
	}
}

// burstSpeed is how many times real time a test sends its samples over UDP.
const burstSpeed = 4

// burstPacer paces the samples a test sends over UDP at burstSpeed times
// real time, or returns nil over TCP, whose flow control lets them go at
// once. Sent at once, 2s of media outruns the receiving socket's buffer
// (about 200 KB on Linux) while the receiver waits for a CPU, and the kernel
// drops the end of the burst.
func burstPacer(tcp bool) *rtmp.Pacer {
	if tcp {
		return nil
	}
	start := time.Now()
	return &rtmp.Pacer{
		Now: func() time.Time { return start.Add(time.Since(start) * burstSpeed) },
		Sleep: func(ctx context.Context, d time.Duration) error {
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(d / burstSpeed):
				return nil
			}
		},
	}
}

// readSamples reads the samples of the WHIP test file before d.
func readSamples(t *testing.T, d time.Duration) []Sample {
	t.Helper()
	all, err := ReadMP4(media.Path(t, media.H264BaselineOpus))
	if err != nil {
		t.Fatal(err)
	}
	var out []Sample
	for _, s := range all {
		if s.DTS < d {
			out = append(out, s)
		}
	}
	return out
}

func openMP4(t *testing.T, path string) (*media.MP4File, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	info, err := f.Stat()
	if err != nil {
		return nil, err
	}
	return media.ReadMP4(f, info.Size())
}

func first(s []Sample) Sample {
	if len(s) == 0 {
		return Sample{}
	}
	return s[0]
}

func nalTypes(annexB []byte) []byte {
	var out []byte
	for _, nal := range ts.SplitNALs(annexB) {
		if len(nal) > 0 {
			out = append(out, nal[0]&0x1f)
		}
	}
	return out
}

func contains(b []byte, v byte) bool {
	return bytes.IndexByte(b, v) >= 0
}

// A fakeSFU is a WHIP and WHEP server in the test: it forwards what its
// publishers send to its players, and answers the legacy API too.
type fakeSFU struct {
	URL string

	api          *webrtc.API
	video, audio *webrtc.TrackLocalStaticRTP
	// reject makes the next signaling requests fail with its status, or for
	// the legacy API with its code.
	reject *StatusError

	mu     sync.Mutex
	reqs   []fakeRequest
	pcs    map[string]*webrtc.PeerConnection
	closed chan struct{}
}

type fakeRequest struct {
	Method, Path, Query, ContentType, Auth, Body string
}

func newFakeSFU(t *testing.T) *fakeSFU {
	t.Helper()
	return newFakeSFUWith(t,
		webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeH264, ClockRate: 90000,
			SDPFmtpLine: "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f"},
		webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeOpus, ClockRate: 48000, Channels: 2})
}

// newFakeSFUWith makes a fake server whose players get a video and an audio
// track of these codecs.
func newFakeSFUWith(t *testing.T, video, audio webrtc.RTPCodecCapability) *fakeSFU {
	t.Helper()
	return newFakeSFUOn(t, false, video, audio)
}

// newFakeSFUTCP makes a fake server of H.264 and Opus that takes ICE only
// over TCP, on a passive TCP candidate, as SRS does.
func newFakeSFUTCP(t *testing.T) *fakeSFU {
	t.Helper()
	f := newFakeSFU(t)
	return newFakeSFUOn(t, true, f.video.Codec(), f.audio.Codec())
}

// newFakeSFUOn makes a fake server that takes ICE over UDP, or only over
// TCP when tcp is set.
func newFakeSFUOn(t *testing.T, tcp bool, video, audio webrtc.RTPCodecCapability) *fakeSFU {
	t.Helper()
	m := &webrtc.MediaEngine{}
	if err := m.RegisterDefaultCodecs(); err != nil {
		t.Fatal(err)
	}
	// It takes the transport-wide sequence numbers, as SRS does.
	for _, kind := range []webrtc.RTPCodecType{webrtc.RTPCodecTypeVideo, webrtc.RTPCodecTypeAudio} {
		if err := m.RegisterHeaderExtension(webrtc.RTPHeaderExtensionCapability{URI: sdp.TransportCCURI}, kind); err != nil {
			t.Fatal(err)
		}
	}
	var se webrtc.SettingEngine
	se.SetNetworkTypes([]webrtc.NetworkType{webrtc.NetworkTypeUDP4})
	if tcp {
		ln, err := net.ListenTCP("tcp4", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
		if err != nil {
			t.Fatal(err)
		}
		mux := webrtc.NewICETCPMux(nil, ln, 8)
		t.Cleanup(func() { mux.Close() })
		se.SetNetworkTypes([]webrtc.NetworkType{webrtc.NetworkTypeTCP4})
		se.SetICETCPMux(mux)
	}
	se.SetIncludeLoopbackCandidate(true)
	se.SetIPFilter(func(ip net.IP) bool { return ip.IsLoopback() })
	se.SetICEMulticastDNSMode(ice.MulticastDNSModeDisabled)
	f := &fakeSFU{
		api:    webrtc.NewAPI(webrtc.WithMediaEngine(m), webrtc.WithSettingEngine(se)),
		pcs:    map[string]*webrtc.PeerConnection{},
		closed: make(chan struct{}, 16),
	}
	var err error
	f.video, err = webrtc.NewTrackLocalStaticRTP(video, "video", "sfu")
	if err != nil {
		t.Fatal(err)
	}
	f.audio, err = webrtc.NewTrackLocalStaticRTP(audio, "audio", "sfu")
	if err != nil {
		t.Fatal(err)
	}
	srv := httptest.NewServer(f)
	f.URL = srv.URL
	t.Cleanup(func() {
		srv.Close()
		f.mu.Lock()
		defer f.mu.Unlock()
		for _, pc := range f.pcs {
			pc.Close()
		}
	})
	return f
}

func (f *fakeSFU) requests() []fakeRequest {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]fakeRequest(nil), f.reqs...)
}

func (f *fakeSFU) waitClosed(ctx context.Context) error {
	select {
	case <-f.closed:
		return nil
	case <-ctx.Done():
		return errors.New("the fake server closed no session")
	}
}

func (f *fakeSFU) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	body, _ := io.ReadAll(r.Body)
	f.mu.Lock()
	f.reqs = append(f.reqs, fakeRequest{r.Method, r.URL.Path, r.URL.RawQuery, r.Header.Get("Content-Type"), r.Header.Get("Authorization"), string(body)})
	reject := f.reject
	f.mu.Unlock()

	if r.Method == http.MethodDelete {
		f.mu.Lock()
		pc := f.pcs[r.URL.Query().Get("session")]
		delete(f.pcs, r.URL.Query().Get("session"))
		f.mu.Unlock()
		if pc != nil {
			pc.Close()
			f.closed <- struct{}{}
		}
		return
	}

	legacy := r.URL.Path == "/rtc/v1/publish/" || r.URL.Path == "/rtc/v1/play/"
	if reject != nil {
		if legacy {
			fmt.Fprintf(w, `{"code":%d}`, reject.Code)
			return
		}
		w.WriteHeader(reject.Code)
		io.WriteString(w, reject.Body)
		return
	}

	offer := string(body)
	if legacy {
		var req struct{ SDP string }
		json.Unmarshal(body, &req)
		offer = req.SDP
	}
	publish := r.URL.Path == "/rtc/v1/whip/" || r.URL.Path == "/rtc/v1/publish/"
	answer, id, err := f.answer(offer, publish)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	if legacy {
		json.NewEncoder(w).Encode(map[string]any{"code": 0, "sdp": answer, "sessionid": "legacy-session"})
		return
	}
	w.Header().Set("Content-Type", "application/sdp")
	w.Header().Set("Location", "/rtc/v1/whip/?action=delete&session="+id)
	w.WriteHeader(http.StatusCreated)
	io.WriteString(w, answer)
}

// answer answers an offer: a publisher's tracks are forwarded to the
// server's tracks, which a player gets.
func (f *fakeSFU) answer(offer string, publish bool) (string, string, error) {
	pc, err := f.api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return "", "", err
	}
	if publish {
		pc.OnTrack(func(track *webrtc.TrackRemote, _ *webrtc.RTPReceiver) {
			out := f.audio
			if track.Kind() == webrtc.RTPCodecTypeVideo {
				out = f.video
			}
			for {
				p, _, err := track.ReadRTP()
				if err != nil {
					return
				}
				out.WriteRTP(p)
			}
		})
	} else {
		for _, track := range []*webrtc.TrackLocalStaticRTP{f.video, f.audio} {
			if _, err := pc.AddTrack(track); err != nil {
				return "", "", err
			}
		}
	}
	if err := pc.SetRemoteDescription(webrtc.SessionDescription{Type: webrtc.SDPTypeOffer, SDP: offer}); err != nil {
		return "", "", err
	}
	answer, err := pc.CreateAnswer(nil)
	if err != nil {
		return "", "", err
	}
	gathered := webrtc.GatheringCompletePromise(pc)
	if err := pc.SetLocalDescription(answer); err != nil {
		return "", "", err
	}
	<-gathered
	id := fmt.Sprintf("%p", pc)
	f.mu.Lock()
	f.pcs[id] = pc
	f.mu.Unlock()
	return pc.LocalDescription().SDP, id, nil
}
