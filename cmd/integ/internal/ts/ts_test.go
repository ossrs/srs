// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package ts

import (
	"bytes"
	"context"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"reflect"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/audio"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// packet makes a TS packet, with stuffing in an adaptation field when the
// payload is shorter than 184 bytes.
func packet(pid uint16, pusi bool, cc byte, payload []byte) []byte {
	p := make([]byte, PacketSize)
	p[0] = 0x47
	p[1] = byte(pid>>8) & 0x1f
	if pusi {
		p[1] |= 0x40
	}
	p[2] = byte(pid)
	if len(payload) == 184 {
		p[3] = 0x10 | cc
		copy(p[4:], payload)
		return p
	}
	p[3] = 0x30 | cc
	af := 183 - len(payload)
	p[4] = byte(af)
	if af > 0 {
		for i := 6; i < 5+af; i++ {
			p[i] = 0xff
		}
	}
	copy(p[5+af:], payload)
	return p
}

// packets splits a PES into TS packets, the first with PUSI.
func packets(pid uint16, cc byte, pes []byte) []byte {
	var out []byte
	for first := true; len(pes) > 0; first = false {
		n := min(len(pes), 184)
		out = append(out, packet(pid, first, cc&0x0f, pes[:n])...)
		pes, cc = pes[n:], cc+1
	}
	return out
}

// psi makes a section with its pointer field and a zero CRC, which the
// demuxer doesn't check.
func psi(tableID byte, id uint16, body []byte) []byte {
	n := 5 + len(body) + 4
	b := []byte{0, tableID, 0xb0 | byte(n>>8), byte(n), byte(id >> 8), byte(id), 0xc1, 0, 0}
	b = append(b, body...)
	return append(b, 0, 0, 0, 0)
}

func pat(pmt uint16) []byte {
	return packet(0, true, 0, psi(0, 1, []byte{0, 1, 0xe0 | byte(pmt>>8), byte(pmt)}))
}

type es struct {
	typ         byte
	pid         uint16
	descriptors []byte
}

func pmt(pid uint16, streams ...es) []byte {
	body := []byte{0xe1, 0x00, 0xf0, 0x00}
	for _, s := range streams {
		body = append(body, s.typ, 0xe0|byte(s.pid>>8), byte(s.pid), 0xf0|byte(len(s.descriptors)>>8), byte(len(s.descriptors)))
		body = append(body, s.descriptors...)
	}
	return packet(pid, true, 0, psi(2, 1, body))
}

// timestamp encodes a 33-bit PTS or DTS with its 4-bit prefix.
func timestamp(prefix byte, ts int64) []byte {
	return []byte{
		prefix<<4 | byte(ts>>29)&0x0e | 1, byte(ts >> 22), byte(ts>>14)&0xfe | 1, byte(ts >> 7), byte(ts<<1) | 1,
	}
}

// pes makes a PES packet; dts < 0 means no DTS, and bounded gives its length.
func pes(streamID byte, pts, dts int64, data []byte, bounded bool) []byte {
	var opt []byte
	if dts < 0 {
		opt = append([]byte{0x80, 0x80, 5}, timestamp(2, pts)...)
	} else {
		opt = append(append([]byte{0x80, 0xc0, 10}, timestamp(3, pts)...), timestamp(1, dts)...)
	}
	n := 0
	if bounded {
		n = len(opt) + len(data)
	}
	b := append([]byte{0, 0, 1, streamID, byte(n >> 8), byte(n)}, opt...)
	return append(b, data...)
}

var opusDescriptor = []byte{0x05, 0x04, 'O', 'p', 'u', 's'}

// blocked fails a read, so a test sees whether the demuxer read too far.
type blocked struct{}

func (blocked) Read([]byte) (int, error) { return 0, errors.New("read past the packets") }

func cat(parts ...[]byte) []byte {
	return bytes.Join(parts, nil)
}

func TestDemuxStreams(t *testing.T) {
	d := NewDemuxer(bytes.NewReader(cat(pat(0x1000), pmt(0x1000,
		es{0x1b, 0x100, nil}, es{0x0f, 0x101, nil}, es{0x06, 0x102, opusDescriptor},
		es{0x03, 0x103, nil}, es{0x24, 0x104, nil}, es{0x15, 0x105, nil}, es{0x06, 0x106, nil}))))
	if p, err := d.ReadPES(); err != io.EOF {
		t.Fatalf("read %+v, %v, want EOF", p, err)
	}
	want := map[uint16]string{0x100: "h264", 0x101: "aac", 0x102: "opus", 0x103: "mp3", 0x104: "hevc", 0x105: "", 0x106: ""}
	if got := d.Streams(); !reflect.DeepEqual(got, want) {
		t.Errorf("streams %v, want %v", got, want)
	}
}

// TestDemuxBoundedPES returns an audio PES as soon as its length has
// arrived, without reading on.
func TestDemuxBoundedPES(t *testing.T) {
	data := bytes.Repeat([]byte{0xaa}, 300)
	src := cat(pat(0x1000), pmt(0x1000, es{0x0f, 0x101, nil}), packets(0x101, 0, pes(0xc0, 9000, -1, data, true)))
	d := NewDemuxer(io.MultiReader(bytes.NewReader(src), blocked{}))
	p, err := d.ReadPES()
	if err != nil {
		t.Fatal(err)
	}
	if p.PID != 0x101 || p.Codec != "aac" || p.StreamType != 0x0f || !bytes.Equal(p.Data, data) {
		t.Errorf("PES %v %v %#x with %v bytes, want aac on 0x101 with 300", p.PID, p.Codec, p.StreamType, len(p.Data))
	}
	if p.PTS != 100*time.Millisecond || p.DTS != p.PTS {
		t.Errorf("PTS %v DTS %v, want 100ms for both", p.PTS, p.DTS)
	}
}

// TestDemuxUnboundedPES returns a video PES without a length when the next
// one starts, and the last at the end of the stream.
func TestDemuxUnboundedPES(t *testing.T) {
	first := bytes.Repeat([]byte{1}, 400)
	second := bytes.Repeat([]byte{2}, 10)
	d := NewDemuxer(bytes.NewReader(cat(pat(0x1000), pmt(0x1000, es{0x1b, 0x100, nil}),
		packets(0x100, 0, pes(0xe0, 18000, 9000, first, false)),
		packets(0x100, 3, pes(0xe0, 21600, 12600, second, false)))))

	p, err := d.ReadPES()
	if err != nil {
		t.Fatal(err)
	}
	if p.Codec != "h264" || !bytes.Equal(p.Data, first) || p.PTS != 200*time.Millisecond || p.DTS != 100*time.Millisecond {
		t.Errorf("first PES %v with %v bytes, PTS %v DTS %v", p.Codec, len(p.Data), p.PTS, p.DTS)
	}
	if p, err = d.ReadPES(); err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(p.Data, second) || p.DTS != 140*time.Millisecond {
		t.Errorf("second PES with %v bytes, DTS %v", len(p.Data), p.DTS)
	}
	if _, err = d.ReadPES(); err != io.EOF {
		t.Errorf("after the last PES: %v, want EOF", err)
	}
}

func TestDemuxContinuity(t *testing.T) {
	video := packets(0x100, 0, pes(0xe0, 9000, -1, bytes.Repeat([]byte{1}, 400), false))
	head := cat(pat(0x1000), pmt(0x1000, es{0x1b, 0x100, nil}))

	// A duplicate packet is dropped.
	dup := cat(head, video[:2*PacketSize], video[PacketSize:])
	p, err := NewDemuxer(bytes.NewReader(dup)).ReadPES()
	if err != nil || len(p.Data) != 400 {
		t.Errorf("with a duplicate packet: %v, want the 400 bytes once", err)
	}

	// A skipped counter is an error.
	skip := cat(head, video[:PacketSize], video[2*PacketSize:])
	if _, err := NewDemuxer(bytes.NewReader(skip)).ReadPES(); err == nil || !strings.Contains(err.Error(), "continuity") {
		t.Errorf("with a lost packet: %v, want a continuity error", err)
	}
}

func TestDemuxErrors(t *testing.T) {
	bad := pat(0x1000)
	bad[0] = 0
	if _, err := NewDemuxer(bytes.NewReader(cat(pat(0x1000), bad))).ReadPES(); err == nil || !strings.Contains(err.Error(), "sync") {
		t.Errorf("lost sync: %v, want a sync error", err)
	}
	if _, err := NewDemuxer(bytes.NewReader(pat(0x1000)[:100])).ReadPES(); err != io.ErrUnexpectedEOF {
		t.Errorf("cut packet: %v, want unexpected EOF", err)
	}
	notPES := packets(0x100, 0, []byte{0, 0, 2, 0xe0, 0, 0})
	if _, err := NewDemuxer(bytes.NewReader(cat(pat(0x1000), pmt(0x1000, es{0x1b, 0x100, nil}), notPES))).ReadPES(); err == nil {
		t.Errorf("bad PES start code, want an error")
	}
}

// TestDemuxTimestampWrap unwraps a DTS that passes the 33-bit limit.
func TestDemuxTimestampWrap(t *testing.T) {
	const wrap = int64(1) << 33
	d := NewDemuxer(bytes.NewReader(cat(pat(0x1000), pmt(0x1000, es{0x0f, 0x101, nil}),
		packets(0x101, 0, pes(0xc0, wrap-9000, -1, []byte{1}, true)),
		packets(0x101, 1, pes(0xc0, 9000, -1, []byte{2}, true)))))
	a, err := d.ReadPES()
	if err != nil {
		t.Fatal(err)
	}
	b, err := d.ReadPES()
	if err != nil {
		t.Fatal(err)
	}
	if b.DTS-a.DTS != 200*time.Millisecond {
		t.Errorf("DTS %v then %v, want 200ms later", a.DTS, b.DTS)
	}
}

func TestSplitNALs(t *testing.T) {
	got := SplitNALs([]byte{0, 0, 0, 1, 0x09, 0xf0, 0, 0, 1, 0x67, 0xaa, 0, 0, 0, 1, 0x68, 0xbb})
	want := [][]byte{{0x09, 0xf0}, {0x67, 0xaa}, {0x68, 0xbb}}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("%x, want %x", got, want)
	}
	if got := SplitNALs([]byte{1, 2, 3}); len(got) != 0 {
		t.Errorf("no start code: %x, want none", got)
	}
}

func TestOpusPackets(t *testing.T) {
	b := cat([]byte{0x7f, 0xe0, 3, 0xfc, 1, 2}, []byte{0x7f, 0xf0, 2, 0, 0, 0xfc, 1})
	got, err := OpusPackets(b)
	if err != nil {
		t.Fatal(err)
	}
	if want := [][]byte{{0xfc, 1, 2}, {0xfc, 1}}; !reflect.DeepEqual(got, want) {
		t.Errorf("%x, want %x", got, want)
	}

	long := cat([]byte{0x7f, 0xe0, 0xff, 1}, bytes.Repeat([]byte{0xfc}, 256))
	if got, err := OpusPackets(long); err != nil || len(got) != 1 || len(got[0]) != 256 {
		t.Errorf("256-byte packet: %v packets, %v", len(got), err)
	}

	for name, b := range map[string][]byte{
		"NoControlHeader": {0x7e, 0xe0, 1, 0xfc},
		"Cut":             {0x7f, 0xe0, 5, 0xfc},
		"NoSize":          {0x7f, 0xe0},
	} {
		if _, err := OpusPackets(b); err == nil {
			t.Errorf("%v: want an error", name)
		}
	}
}

func TestOpusSamples(t *testing.T) {
	for _, tc := range []struct {
		packet  []byte
		samples int
	}{
		{[]byte{0xfc}, 960},        // CELT 20ms, one frame.
		{[]byte{0x78}, 960},        // Hybrid 20ms.
		{[]byte{0x80}, 120},        // CELT 2.5ms.
		{[]byte{0x09, 0}, 1920},    // SILK 20ms, two frames.
		{[]byte{0x0b, 0x03}, 2880}, // SILK 20ms, three frames by count.
		{[]byte{0x18}, 2880},       // SILK 60ms.
	} {
		if got, err := OpusSamples(tc.packet); err != nil || got != tc.samples {
			t.Errorf("TOC %#x: %v samples, %v, want %v", tc.packet[0], got, err, tc.samples)
		}
	}
	if _, err := OpusSamples(nil); err == nil {
		t.Errorf("empty packet, want an error")
	}
	if _, err := OpusSamples([]byte{0x0b}); err == nil {
		t.Errorf("no frame count, want an error")
	}
}

func TestAnalyzerErrors(t *testing.T) {
	var a Analyzer
	if err := a.Add(&PES{Codec: "h264", Data: []byte{0, 0, 1, 0x67, 0x64}}); err == nil {
		t.Errorf("broken SPS, want an error")
	}
	if err := a.Add(&PES{Codec: "aac", Data: []byte{0xff, 0xf1, 0x50, 0x80, 0x0c, 0x9f, 0xfc, 1}}); err == nil {
		t.Errorf("cut ADTS frame, want an error")
	}
	// An unknown stream is not read.
	if err := a.Add(&PES{Codec: "", Data: []byte{1, 2, 3}}); err != nil {
		t.Errorf("unknown stream: %v", err)
	}
}

// TestAnalyzeGenerated reads the test media, remuxed by ffmpeg into TS, and
// finds every frame and keyframe of the source with its codecs.
func TestAnalyzeGenerated(t *testing.T) {
	cases := []struct {
		file, video, profile, audio string
		rate, audioHeaders          int
	}{
		{media.H264HighAAC, "h264", "High", "aac", 44100, 1},
		{media.HEVCAAC, "hevc", "Main", "aac", 44100, 1},
		{media.H264HighMP3, "h264", "High", "mp3", 44100, 0},
		{media.H264BaselineOpus, "h264", "Baseline", "opus", 48000, 0},
	}
	for _, tc := range cases {
		t.Run(tc.file, func(t *testing.T) {
			info, err := media.ProbeFile(media.Path(t, tc.file))
			if err != nil {
				t.Fatal(err)
			}
			f, err := os.Open(media.Remux(t, tc.file, "mpegts"))
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()

			r, err := Analyze(NewDemuxer(f), report.Until{})
			if err != nil {
				t.Fatalf("%v: %v", err, r)
			}
			v, a := r.Video, r.Audio
			if v == nil || a == nil {
				t.Fatalf("report %v, want video and audio", r)
			}
			if v.Codec != tc.video || v.Profile != tc.profile || v.Width != 768 || v.Height != 320 || v.SequenceHeaders != 1 {
				t.Errorf("video %v %v %vx%v with %v sequence headers, want %v %v 768x320 with 1",
					v.Codec, v.Profile, v.Width, v.Height, v.SequenceHeaders, tc.video, tc.profile)
			}
			if v.Frames != info.Video.Frames || v.Keyframes != len(info.Video.Keyframes) {
				t.Errorf("video %v frames %v keyframes, want %v and %v", v.Frames, v.Keyframes, info.Video.Frames, len(info.Video.Keyframes))
			}
			if a.Codec != tc.audio || a.SampleRate != tc.rate || a.Channels != 2 || a.SequenceHeaders != tc.audioHeaders {
				t.Errorf("audio %v %vHz %vch with %v sequence headers, want %v %vHz 2ch with %v",
					a.Codec, a.SampleRate, a.Channels, a.SequenceHeaders, tc.audio, tc.rate, tc.audioHeaders)
			}
			if a.Frames != info.Audio.Frames {
				t.Errorf("audio %v frames, want %v", a.Frames, info.Audio.Frames)
			}
			if err := r.CheckTimestamps(100 * time.Millisecond); err != nil {
				t.Error(err)
			}
		})
	}
}

// TestPlayHTTP plays a TS file from an HTTP server until a keyframe and 5
// frames.
func TestPlayHTTP(t *testing.T) {
	path := media.Remux(t, media.H264HighAAC, "mpegts")
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/live/a.ts" {
			http.NotFound(w, r)
			return
		}
		http.ServeFile(w, r, path)
	}))
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	u := report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}
	r, err := PlayHTTP(ctx, nil, srv.URL+"/live/a.ts", u)
	if err != nil {
		t.Fatal(err)
	}
	// ffmpeg holds audio back into large PES packets, so the audio header
	// comes after more than 5 video frames; the analyzer stops right then.
	if v := r.Video; !u.Done(r) || v.Frames > 25 || r.Audio.SequenceHeaders != 1 || r.Audio.Frames > 25 {
		t.Errorf("report %v, want it to stop once both headers, a keyframe and 5 frames came", r)
	}

	if _, err := PlayHTTP(ctx, nil, srv.URL+"/live/b.ts", u); err == nil || !strings.Contains(err.Error(), "404") {
		t.Errorf("missing stream: %v, want 404", err)
	}
}

// TestDemuxPMTUpdate reads a new PMT that adds a stream, sent like SRS does
// when audio first comes: the PAT and PMT again, with the same continuity
// counters as before.
func TestDemuxPMTUpdate(t *testing.T) {
	d := NewDemuxer(bytes.NewReader(cat(pat(0x1000), pmt(0x1000, es{0x1b, 0x100, nil}),
		pat(0x1000), pmt(0x1000, es{0x0f, 0x101, nil}, es{0x1b, 0x100, nil}),
		packets(0x101, 0, pes(0xc0, 9000, -1, []byte{1}, true)))))
	p, err := d.ReadPES()
	if err != nil {
		t.Fatal(err)
	}
	if p.PID != 0x101 || p.Codec != "aac" {
		t.Errorf("PES of %#x %v, want aac on 0x101", p.PID, p.Codec)
	}
	if want := map[uint16]string{0x100: "h264", 0x101: "aac"}; !reflect.DeepEqual(d.Streams(), want) {
		t.Errorf("streams %v, want %v", d.Streams(), want)
	}
}

// TestDemuxProgram checks that each PES has the codecs of the PMT in force
// when it started, sorted: video first under a PMT with video only, then
// audio and video under the PMT SRS sends when audio comes.
func TestDemuxProgram(t *testing.T) {
	d := NewDemuxer(bytes.NewReader(cat(pat(0x1000), pmt(0x1000, es{0x1b, 0x100, nil}),
		packets(0x100, 0, pes(0xe0, 9000, -1, []byte{1}, false)),
		pat(0x1000), pmt(0x1000, es{0x1b, 0x100, nil}, es{0x0f, 0x101, nil}),
		packets(0x101, 0, pes(0xc0, 9000, -1, []byte{2}, true)),
		packets(0x100, 1, pes(0xe0, 12600, -1, []byte{3}, false)))))
	// The audio PES has its length, so it comes before the first video PES,
	// which ends when the next one starts; each is keyed by its one byte.
	want := map[byte][]string{1: {"h264"}, 2: {"aac", "h264"}, 3: {"aac", "h264"}}
	got := map[byte][]string{}
	for {
		p, err := d.ReadPES()
		if err == io.EOF {
			break
		} else if err != nil {
			t.Fatal(err)
		}
		got[p.Data[0]] = p.Program
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("programs %v, want %v", got, want)
	}
}

// TestDialHTTP plays an HTTP-TS file PES by PES, with the codecs of its
// program, and fails a missing stream with a status error.
func TestDialHTTP(t *testing.T) {
	path := media.Remux(t, media.H264HighAAC, "mpegts")
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/live/a.ts" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "video/MP2T")
		http.ServeFile(w, r, path)
	}))
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	p, err := DialHTTP(ctx, srv.URL+"/live/a.ts", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	p.SetDeadline(time.Now().Add(2 * time.Second))
	var a Analyzer
	u := report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}
	for !u.Done(&a.Report) {
		pes, err := p.Read()
		if err != nil {
			t.Fatalf("read: %v, after %v", err, &a.Report)
		}
		if want := []string{"aac", "h264"}; !reflect.DeepEqual(pes.Program, want) {
			t.Fatalf("program %v, want %v", pes.Program, want)
		}
		if err := a.Add(pes); err != nil {
			t.Fatal(err)
		}
	}

	missing, err := DialHTTP(ctx, srv.URL+"/live/b.ts", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer missing.Close()
	missing.SetDeadline(time.Now().Add(time.Second))
	if _, err := missing.Read(); err == nil || !strings.Contains(err.Error(), "404") {
		t.Errorf("missing stream: %v, want 404", err)
	}
}

// TestGeneratedTS checks the MPEG-TS files of the test media, which the media
// package can't read: the codecs of their FLV and MP4 twins, a keyframe every
// second for 30s, and 25 fps. TS carries no Opus sequence header. A file of
// one track has no other. A file of both has its audio beside the video of
// the same time, one frame per PES, as SRS's SRT docs publish TS.
func TestGeneratedTS(t *testing.T) {
	cases := []struct {
		file, video, profile, audio string
		rate, headers               int
	}{
		{media.H264HighAACTS, "h264", "High", "aac", 44100, 1},
		{media.HEVCAACTS, "hevc", "Main", "aac", 44100, 1},
		{media.H264OpusTS, "h264", "High", "opus", 48000, 0},
		{media.AACOnlyTS, "", "", "aac", 44100, 1},
		{media.H264OnlyTS, "h264", "High", "", 0, 0},
		{media.AAC48kPackedTS, "", "", "aac", 48000, 1},
	}
	for _, tc := range cases {
		t.Run(tc.file, func(t *testing.T) {
			f, err := os.Open(media.Path(t, tc.file))
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()

			r, err := Analyze(NewDemuxer(f), report.Until{})
			if err != nil {
				t.Fatalf("%v: %v", err, r)
			}
			v, a := r.Video, r.Audio
			if (v == nil) != (tc.video == "") || (a == nil) != (tc.audio == "") {
				t.Fatalf("report %v, want video %q and audio %q", r, tc.video, tc.audio)
			}
			if v != nil {
				if v.Codec != tc.video || v.Profile != tc.profile || v.Width != 768 || v.Height != 320 || v.SequenceHeaders != 1 {
					t.Errorf("video %v %v %vx%v with %v sequence headers, want %v %v 768x320 with 1",
						v.Codec, v.Profile, v.Width, v.Height, v.SequenceHeaders, tc.video, tc.profile)
				}
				if v.Frames != media.Duration*media.FPS || v.Keyframes != media.Duration || v.LeadingFrames != 0 {
					t.Errorf("video %v frames, %v keyframes, %v before the first; want %v, %v, 0",
						v.Frames, v.Keyframes, v.LeadingFrames, media.Duration*media.FPS, media.Duration)
				}
				if d := v.Duration(); d < 29900*time.Millisecond || d > 30*time.Second {
					t.Errorf("video lasts %v, want 29.96s", d)
				}
			}
			if v != nil && a != nil && r.AVSync > 200*time.Millisecond {
				t.Errorf("A/V sync %v in file order, want at most 200ms", r.AVSync)
			}
			if a != nil && (a.Codec != tc.audio || a.SampleRate != tc.rate || a.Channels != 2 || a.SequenceHeaders != tc.headers || a.Frames == 0) {
				t.Errorf("audio %v %vHz %vch with %v sequence headers and %v frames, want %v %vHz 2ch with %v",
					a.Codec, a.SampleRate, a.Channels, a.SequenceHeaders, a.Frames, tc.audio, tc.rate, tc.headers)
			}
			if err := r.CheckTimestamps(100 * time.Millisecond); err != nil {
				t.Error(err)
			}
		})
	}
}

// TestGeneratedPackedTS checks the file of ffmpeg's default packing has
// several AAC frames in each PES but its last, all with the PES's one PTS.
func TestGeneratedPackedTS(t *testing.T) {
	f, err := os.Open(media.Path(t, media.AAC48kPackedTS))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	d := NewDemuxer(f)
	var counts []int
	for {
		p, err := d.ReadPES()
		if errors.Is(err, io.EOF) {
			break
		} else if err != nil {
			t.Fatal(err)
		}
		frames, err := audio.SplitFrames(p.Codec, p.Data)
		if err != nil {
			t.Fatal(err)
		}
		counts = append(counts, len(frames))
	}
	if len(counts) < 2 {
		t.Fatalf("%v PES, want many", len(counts))
	}
	for i, n := range counts[:len(counts)-1] {
		if n < 2 {
			t.Fatalf("PES %v has %v frames, want several: %v", i, n, counts)
		}
	}
}
