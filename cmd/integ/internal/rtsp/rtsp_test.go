// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtsp

import (
	"bufio"
	"context"
	"errors"
	"net"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/bluenviron/gortsplib/v5"
	"github.com/bluenviron/gortsplib/v5/pkg/base"
	"github.com/bluenviron/gortsplib/v5/pkg/description"
	"github.com/bluenviron/gortsplib/v5/pkg/format"
	"github.com/bluenviron/gortsplib/v5/pkg/format/rtph264"
	"github.com/bluenviron/gortsplib/v5/pkg/format/rtph265"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h264"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
	"github.com/pion/rtp"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
)

// testDuration is how much of the test media the tests send.
const testDuration = 2 * time.Second

// videoAUs are the access units of the first testDuration of a test MP4's
// video, each a list of NALs, with their decode times.
func videoAUs(t *testing.T, name string) (aus [][][]byte, dts []time.Duration) {
	t.Helper()
	samples, err := rtc.ReadMP4(media.Path(t, name))
	if err != nil {
		t.Fatal(err)
	}
	for _, s := range samples {
		if !s.Video || s.DTS >= testDuration {
			continue
		}
		var nals h264.AnnexB
		if err := nals.Unmarshal(s.Data); err != nil {
			t.Fatal(err)
		}
		aus, dts = append(aus, nals), append(dts, s.DTS)
	}
	return aus, dts
}

// aacFrames are the AudioSpecificConfig and the frames of the first
// testDuration of the AAC-only test file, with their decode times.
func aacFrames(t *testing.T) (asc []byte, frames [][]byte, dts []time.Duration) {
	t.Helper()
	f, err := os.Open(media.Path(t, media.AACOnly))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	r, err := media.NewFLVReader(f)
	if err != nil {
		t.Fatal(err)
	}
	for {
		tag, err := r.ReadTag()
		if err != nil {
			t.Fatal(err)
		}
		at := time.Duration(tag.Timestamp) * time.Millisecond
		if at >= testDuration {
			return asc, frames, dts
		}
		if tag.Type != media.TagAudio {
			continue
		}
		a, err := media.ParseAudio(tag.Data)
		if err != nil {
			t.Fatal(err)
		}
		if a.SequenceHeader {
			asc = a.Payload
			continue
		}
		frames, dts = append(frames, a.Payload), append(dts, at)
	}
}

// testStream is the media the fake server sends: H.264 or HEVC, and AAC, as
// RTP packets in decode order with their medias.
type testStream struct {
	desc    *description.Session
	packets []testPacket
}

type testPacket struct {
	media *description.Media
	pkt   *rtp.Packet
}

// newTestStream packetizes the first testDuration of the test media, the
// video of an MP4 file and the AAC-only file's audio, as gortsplib does.
func newTestStream(t *testing.T, video string) *testStream {
	t.Helper()
	ascBytes, frames, adts := aacFrames(t)
	var asc mpeg4audio.AudioSpecificConfig
	if err := asc.Unmarshal(ascBytes); err != nil {
		t.Fatal(err)
	}
	audioFormat := &format.MPEG4Audio{PayloadTyp: 97, Config: &asc, SizeLength: 13, IndexLength: 3, IndexDeltaLength: 3}
	audio := &description.Media{Type: description.MediaTypeAudio, Formats: []format.Format{audioFormat}}

	aus, vdts := videoAUs(t, video)
	var videoFormat format.Format
	var encode func([][]byte) ([]*rtp.Packet, error)
	if video == media.HEVCOpus {
		videoFormat = &format.H265{PayloadTyp: 96}
		enc := &rtph265.Encoder{PayloadType: 96}
		if err := enc.Init(); err != nil {
			t.Fatal(err)
		}
		encode = enc.Encode
	} else {
		videoFormat = &format.H264{PayloadTyp: 96, PacketizationMode: 1}
		enc := &rtph264.Encoder{PayloadType: 96, PacketizationMode: 1}
		if err := enc.Init(); err != nil {
			t.Fatal(err)
		}
		encode = enc.Encode
	}
	v := &description.Media{Type: description.MediaTypeVideo, Formats: []format.Format{videoFormat}}

	aenc, err := audioFormat.CreateEncoder()
	if err != nil {
		t.Fatal(err)
	}
	s := &testStream{desc: &description.Session{Medias: []*description.Media{v, audio}}}
	i, j := 0, 0
	for i < len(aus) || j < len(frames) {
		if j >= len(frames) || (i < len(aus) && vdts[i] <= adts[j]) {
			pkts, err := encode(aus[i])
			if err != nil {
				t.Fatal(err)
			}
			for _, p := range pkts {
				p.Timestamp = uint32(vdts[i] * 90000 / time.Second)
				s.packets = append(s.packets, testPacket{v, p})
			}
			i++
			continue
		}
		pkts, err := aenc.Encode([][]byte{frames[j]})
		if err != nil {
			t.Fatal(err)
		}
		for _, p := range pkts {
			p.Timestamp = uint32(adts[j] * time.Duration(asc.SampleRate) / time.Second)
			s.packets = append(s.packets, testPacket{audio, p})
		}
		j++
	}
	return s
}

// fakeServer is an RTSP server that plays a test stream to each reader at
// once after its PLAY, or answers DESCRIBE with a status.
type fakeServer struct {
	server   *gortsplib.Server
	addr     string
	stream   *gortsplib.ServerStream
	test     *testStream
	describe base.StatusCode
	mu       sync.Mutex
	paths    []string
}

func startFakeServer(t *testing.T, s *testStream) *fakeServer {
	t.Helper()
	f := &fakeServer{test: s, describe: base.StatusOK}
	ln := make(chan net.Listener, 1)
	f.server = &gortsplib.Server{
		Handler:     f,
		RTSPAddress: "127.0.0.1:0",
		Listen: func(network, address string) (net.Listener, error) {
			l, err := net.Listen(network, address)
			if err == nil {
				ln <- l
			}
			return l, err
		},
	}
	if err := f.server.Start(); err != nil {
		t.Fatal(err)
	}
	f.addr = (<-ln).Addr().String()
	if s != nil {
		f.stream = &gortsplib.ServerStream{Server: f.server, Desc: s.desc}
		if err := f.stream.Initialize(); err != nil {
			t.Fatal(err)
		}
	}
	t.Cleanup(func() {
		if f.stream != nil {
			f.stream.Close()
		}
		f.server.Close()
	})
	return f
}

func (f *fakeServer) OnDescribe(ctx *gortsplib.ServerHandlerOnDescribeCtx) (*base.Response, *gortsplib.ServerStream, error) {
	f.mu.Lock()
	f.paths = append(f.paths, ctx.Path)
	f.mu.Unlock()
	if f.describe != base.StatusOK || f.stream == nil {
		return &base.Response{StatusCode: f.describe}, nil, nil
	}
	return &base.Response{StatusCode: base.StatusOK}, f.stream, nil
}

func (f *fakeServer) OnSetup(ctx *gortsplib.ServerHandlerOnSetupCtx) (*base.Response, *gortsplib.ServerStream, error) {
	return &base.Response{StatusCode: base.StatusOK}, f.stream, nil
}

func (f *fakeServer) OnPlay(ctx *gortsplib.ServerHandlerOnPlayCtx) (*base.Response, error) {
	go func() {
		// The session plays once its PLAY is answered.
		time.Sleep(50 * time.Millisecond)
		for _, p := range f.test.packets {
			pkt := *p.pkt
			f.stream.WritePacketRTP(p.media, &pkt)
		}
	}()
	return &base.Response{StatusCode: base.StatusOK}, nil
}

// TestTrackOf checks the codec of each format the analyzer reads, by type or,
// for a payload type gortsplib doesn't take as dynamic, by the rtpmap.
func TestTrackOf(t *testing.T) {
	m := &description.Media{Type: description.MediaTypeVideo}
	for _, tc := range []struct {
		f     format.Format
		codec string
		rate  uint32
	}{
		{&format.H264{PayloadTyp: 102}, "h264", 90000},
		{&format.H265{PayloadTyp: 96}, "h265", 90000},
		{&format.Generic{PayloadTyp: 49, RTPMa: "H265/90000", ClockRat: 90000}, "h265", 90000},
		{&format.MPEG4Audio{PayloadTyp: 111, Config: &mpeg4audio.AudioSpecificConfig{Type: 2, SampleRate: 44100, ChannelCount: 2}, SizeLength: 13, IndexLength: 3}, "aac", 44100},
	} {
		tr, err := TrackOf(m, tc.f)
		if err != nil {
			t.Errorf("%T: %v", tc.f, err)
			continue
		}
		if tr.Codec != tc.codec || tr.Rate != tc.rate {
			t.Errorf("%T: %v at %v, want %v at %v", tc.f, tr.Codec, tr.Rate, tc.codec, tc.rate)
		}
	}
	opus := &format.Opus{PayloadTyp: 111, ChannelCount: 2}
	if _, err := TrackOf(m, opus); err == nil {
		t.Error("Opus: no error, want a codec the analyzer doesn't read")
	}
}

// analyze adds the packets of a test stream to an analyzer.
func analyze(t *testing.T, s *testStream) *report.Report {
	t.Helper()
	var a Analyzer
	tracks := map[*description.Media]*Track{}
	for _, m := range s.desc.Medias {
		tr, err := TrackOf(m, m.Formats[0])
		if err != nil {
			t.Fatal(err)
		}
		tracks[m] = tr
	}
	for _, p := range s.packets {
		if err := a.Add(tracks[p.media], p.pkt); err != nil {
			t.Fatal(err)
		}
	}
	return a.Report()
}

// checkTestReport checks the report of the test stream: its codecs, the
// source's size and audio, one sequence header each, a keyframe a second,
// every AAC frame, and timestamps.
func checkTestReport(t *testing.T, r *report.Report, vcodec, profile string) {
	t.Helper()
	if r.Video == nil || r.Audio == nil {
		t.Fatalf("no video or audio: %v", r)
	}
	v, a := r.Video, r.Audio
	if v.Codec != vcodec || v.Profile != profile || v.Width != 768 || v.Height != 320 {
		t.Errorf("video %v %v %vx%v, want %v %v 768x320", v.Codec, v.Profile, v.Width, v.Height, vcodec, profile)
	}
	if v.SequenceHeaders != 1 || v.Keyframes != 2 || v.Frames != 50 || v.LeadingFrames != 0 {
		t.Errorf("video: %v sequence headers, %v keyframes, %v frames, %v before a keyframe; want 1, 2, 50, 0", v.SequenceHeaders, v.Keyframes, v.Frames, v.LeadingFrames)
	}
	if a.Codec != "aac" || a.SampleRate != 44100 || a.Channels != 2 || a.SequenceHeaders != 1 {
		t.Errorf("audio %v %vHz %vch with %v sequence headers, want aac 44100Hz 2ch with 1", a.Codec, a.SampleRate, a.Channels, a.SequenceHeaders)
	}
	if want := int(testDuration * 44100 / time.Second / 1024); a.Frames < want-1 || a.Frames > want+1 {
		t.Errorf("audio frames %v, want about %v", a.Frames, want)
	}
	if err := r.CheckTimestamps(50 * time.Millisecond); err != nil {
		t.Error(err)
	}
	if r.AVSync > 50*time.Millisecond {
		t.Errorf("A/V sync %v, want at most 50ms", r.AVSync)
	}
}

// TestAnalyzerH264AAC reads H.264 frames rebuilt from single NAL and FU-A
// packets with SPS and PPS in band, and AAC frames from RFC 3640 packets.
func TestAnalyzerH264AAC(t *testing.T) {
	checkTestReport(t, analyze(t, newTestStream(t, media.H264BaselineOpus)), "h264", "Baseline")
}

// TestAnalyzerHEVC reads HEVC frames rebuilt from their packets.
func TestAnalyzerHEVC(t *testing.T) {
	checkTestReport(t, analyze(t, newTestStream(t, media.HEVCOpus)), "hevc", "Main")
}

// TestAnalyzerAACTimes checks each AAC frame of a packet is 1024 samples
// after the one before.
func TestAnalyzerAACTimes(t *testing.T) {
	asc := &mpeg4audio.AudioSpecificConfig{Type: 2, SampleRate: 48000, ChannelCount: 1}
	f := &format.MPEG4Audio{PayloadTyp: 97, Config: asc, SizeLength: 13, IndexLength: 3, IndexDeltaLength: 3}
	m := &description.Media{Type: description.MediaTypeAudio, Formats: []format.Format{f}}
	tr, err := TrackOf(m, f)
	if err != nil {
		t.Fatal(err)
	}
	enc, err := f.CreateEncoder()
	if err != nil {
		t.Fatal(err)
	}
	pkts, err := enc.Encode([][]byte{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}})
	if err != nil {
		t.Fatal(err)
	}
	var a Analyzer
	for _, p := range pkts {
		p.Timestamp = 4800
		if err := a.Add(tr, p); err != nil {
			t.Fatal(err)
		}
	}
	au := a.Report().Audio
	if au == nil || au.Frames != 3 || au.LastDTS != 2*1024*time.Second/48000 {
		t.Errorf("audio %+v, want 3 frames, the last at %v", au, 2*1024*time.Second/48000)
	}
	if au != nil && (au.SampleRate != 48000 || au.Channels != 1 || au.SequenceHeaders != 1) {
		t.Errorf("audio %vHz %vch with %v sequence headers, want 48000Hz 1ch with 1", au.SampleRate, au.Channels, au.SequenceHeaders)
	}
}

// TestPlay plays a stream from a fake server over TCP, and analyzes it.
func TestPlay(t *testing.T) {
	f := startFakeServer(t, newTestStream(t, media.H264BaselineOpus))
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	p, err := Play(ctx, "rtsp://"+f.addr+"/live/s1", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	if !p.TCP() {
		t.Error("the player plays over UDP, want TCP")
	}
	if p.Describe == nil || p.Describe.StatusCode != base.StatusOK || len(p.Desc.Medias) != 2 || len(p.Tracks) != 2 {
		t.Fatalf("describe %v, %v medias, %v tracks; want 200, 2, 2", p.Describe, len(p.Desc.Medias), len(p.Tracks))
	}
	r, err := p.Until(ctx, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 2, VideoFrames: 50, Duration: testDuration - 100*time.Millisecond})
	if err != nil {
		t.Fatal(err)
	}
	checkTestReport(t, r, "h264", "Baseline")
	if p.Packets() == 0 {
		t.Error("no packets counted")
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.paths) != 1 || f.paths[0] != "/live/s1" {
		t.Errorf("described paths %v, want [/live/s1]", f.paths)
	}
}

// TestPlayTryUDP falls back to TCP when the server refuses UDP, as the fake
// server, which has no UDP listener, does.
func TestPlayTryUDP(t *testing.T) {
	f := startFakeServer(t, newTestStream(t, media.H264BaselineOpus))
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	p, err := Play(ctx, "rtsp://"+f.addr+"/live/s1", &Options{TryUDP: true})
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	if !p.TCP() {
		t.Error("the player plays over UDP, want TCP after the server refused UDP")
	}
	if _, err := p.Until(ctx, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1}); err != nil {
		t.Error(err)
	}
}

// TestPlayStatusError returns the status of a refused DESCRIBE.
func TestPlayStatusError(t *testing.T) {
	f := startFakeServer(t, nil)
	f.describe = base.StatusNotFound
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	p, err := Play(ctx, "rtsp://"+f.addr+"/live/none", nil)
	if p != nil {
		p.Close()
	}
	var se *StatusError
	if !errors.As(err, &se) || se.Method != "DESCRIBE" || se.Code != 404 {
		t.Fatalf("play: %v, want a DESCRIBE status 404", err)
	}
	if !strings.Contains(err.Error(), "404") {
		t.Errorf("error %q has no status", err)
	}
}

// TestPlayUntilTimeout returns the partial report when ctx ends first.
func TestPlayUntilTimeout(t *testing.T) {
	f := startFakeServer(t, newTestStream(t, media.H264BaselineOpus))
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	p, err := Play(ctx, "rtsp://"+f.addr+"/live/s1", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	short, cancel2 := context.WithTimeout(ctx, 500*time.Millisecond)
	defer cancel2()
	r, err := p.Until(short, report.Until{Keyframes: 100})
	if !errors.Is(err, context.DeadlineExceeded) || r == nil || r.Video == nil {
		t.Errorf("until: %v with %v, want a deadline with a partial report", err, r)
	}
}

// rawServer accepts one connection and runs serve on it.
func rawServer(t *testing.T, serve func(c net.Conn, r *bufio.Reader)) string {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	go func() {
		defer close(done)
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer c.Close()
		serve(c, bufio.NewReader(c))
	}()
	t.Cleanup(func() {
		l.Close()
		<-done
	})
	return l.Addr().String()
}

// readRequest reads a request's head, and returns its lines.
func readRequest(r *bufio.Reader) []string {
	var lines []string
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return lines
		}
		line = strings.TrimRight(line, "\r\n")
		if line == "" {
			return lines
		}
		lines = append(lines, line)
	}
}

func dialRaw(t *testing.T, addr string) *Conn {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	c, err := Dial(ctx, addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close() })
	return c
}

// TestConnDo sends requests with their CSeq and headers, and reads each
// answer past the interleaved frames before it, which it counts.
func TestConnDo(t *testing.T) {
	got := make(chan []string, 2)
	addr := rawServer(t, func(c net.Conn, r *bufio.Reader) {
		got <- readRequest(r)
		c.Write([]byte("RTSP/1.0 200 OK\r\nCSeq: 1\r\nPublic: DESCRIBE, PLAY\r\n\r\n"))
		got <- readRequest(r)
		c.Write([]byte("$\x00\x00\x02ab$\x01\x00\x01c"))
		c.Write([]byte("RTSP/1.0 461 Unsupported Transport\r\nCSeq: 2\r\nContent-Length: 4\r\n\r\nbody"))
		readRequest(r)
	})
	c := dialRaw(t, addr)
	res, err := c.Do(base.Options, "rtsp://"+addr+"/live/s1", nil, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != 200 || res.Header["Public"][0] != "DESCRIBE, PLAY" {
		t.Errorf("answer %v %v, want 200 with Public", res.StatusCode, res.Header)
	}
	if lines := <-got; len(lines) < 2 || lines[0] != "OPTIONS rtsp://"+addr+"/live/s1 RTSP/1.0" || !contains(lines, "CSeq: 1") {
		t.Errorf("request %q, want OPTIONS with CSeq 1", lines)
	}

	res, err = c.Do(base.Setup, "rtsp://"+addr+"/live/s1/trackID=0", base.Header{"Transport": base.HeaderValue{"RTP/AVP;unicast;client_port=5000-5001"}}, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != 461 || string(res.Body) != "body" {
		t.Errorf("answer %v %q, want 461 with its body", res.StatusCode, res.Body)
	}
	if lines := <-got; !contains(lines, "CSeq: 2") || !contains(lines, "Transport: RTP/AVP;unicast;client_port=5000-5001") {
		t.Errorf("request %q, want CSeq 2 and the Transport", lines)
	}
	if c.Frames() != 2 {
		t.Errorf("frames %v, want 2", c.Frames())
	}
}

func contains(lines []string, s string) bool {
	for _, l := range lines {
		if l == s {
			return true
		}
	}
	return false
}

// TestConnNoAnswer times out when the server doesn't answer.
func TestConnNoAnswer(t *testing.T) {
	addr := rawServer(t, func(c net.Conn, r *bufio.Reader) {
		readRequest(r)
		time.Sleep(500 * time.Millisecond)
	})
	c := dialRaw(t, addr)
	start := time.Now()
	_, err := c.Do(base.GetParameter, "rtsp://"+addr+"/live/s1", nil, 100*time.Millisecond)
	if !errors.Is(err, os.ErrDeadlineExceeded) {
		t.Errorf("do: %v, want a timeout", err)
	}
	if d := time.Since(start); d > 300*time.Millisecond {
		t.Errorf("waited %v, want about 100ms", d)
	}
}

// TestConnClosed tells a close by the server from a timeout.
func TestConnClosed(t *testing.T) {
	addr := rawServer(t, func(c net.Conn, r *bufio.Reader) {
		readRequest(r)
	})
	c := dialRaw(t, addr)
	if err := c.Write([]byte("hello\r\n\r\n")); err != nil {
		t.Fatal(err)
	}
	if err := c.WaitClosed(time.Second); err != nil {
		t.Errorf("wait closed: %v", err)
	}
	if _, err := c.Read(100 * time.Millisecond); !errors.Is(err, ErrClosed) {
		t.Errorf("read: %v, want ErrClosed", err)
	}
}

// TestConnWaitClosedTimeout fails when the server keeps the connection.
func TestConnWaitClosedTimeout(t *testing.T) {
	addr := rawServer(t, func(c net.Conn, r *bufio.Reader) {
		readRequest(r)
		time.Sleep(500 * time.Millisecond)
	})
	c := dialRaw(t, addr)
	c.Write([]byte("hello\r\n\r\n"))
	if err := c.WaitClosed(100 * time.Millisecond); err == nil {
		t.Error("wait closed: no error, want the connection still open")
	}
}

// TestConnReadFrames counts the interleaved frames for a while.
func TestConnReadFrames(t *testing.T) {
	addr := rawServer(t, func(c net.Conn, r *bufio.Reader) {
		for i := 0; i < 3; i++ {
			c.Write([]byte("$\x00\x00\x01x"))
		}
		time.Sleep(300 * time.Millisecond)
	})
	c := dialRaw(t, addr)
	n, err := c.ReadFrames(100 * time.Millisecond)
	if err != nil || n != 3 {
		t.Errorf("frames %v, %v; want 3", n, err)
	}
}

// TestConnWaitFrames returns as soon as enough frames came, and with how
// many came when the time runs out first.
func TestConnWaitFrames(t *testing.T) {
	addr := rawServer(t, func(c net.Conn, r *bufio.Reader) {
		c.Write([]byte("$\x00\x00\x01x$\x00\x00\x01y"))
		time.Sleep(300 * time.Millisecond)
	})
	c := dialRaw(t, addr)
	start := time.Now()
	n, err := c.WaitFrames(1, time.Second)
	if err != nil || n != 1 || time.Since(start) > 200*time.Millisecond {
		t.Errorf("frames %v, %v in %v; want 1 at once", n, err, time.Since(start))
	}
	n, err = c.WaitFrames(5, 100*time.Millisecond)
	if err != nil || n != 1 {
		t.Errorf("frames %v, %v; want the 1 left when the time ran out", n, err)
	}
	if c.Frames() != 2 {
		t.Errorf("frames %v, want 2", c.Frames())
	}
}
