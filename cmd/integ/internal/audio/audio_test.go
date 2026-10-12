// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package audio

import (
	"bytes"
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// adts makes an ADTS frame of size bytes, header included, with no CRC: LC
// at the rate index and channel configuration.
func adts(rateIndex, channels, size int) []byte {
	b := make([]byte, size)
	b[0], b[1] = 0xff, 0xf1
	b[2] = 1<<6 | byte(rateIndex)<<2 | byte(channels>>2)
	b[3] = byte(channels&3)<<6 | byte(size>>11)
	b[4] = byte(size >> 3)
	b[5] = byte(size&7)<<5 | 0x1f
	b[6] = 0xfc
	return b
}

// mp3Frame makes an MPEG-1 Layer III frame of 128 kbps at 44.1 kHz, joint
// stereo, without padding: 417 bytes.
func mp3Frame() []byte {
	b := make([]byte, 417)
	copy(b, []byte{0xff, 0xfb, 0x90, 0x64})
	return b
}

func TestParseADTS(t *testing.T) {
	h, err := ParseADTS([]byte{0xff, 0xf1, 0x50, 0x80, 0x0c, 0x9f, 0xfc})
	if err != nil {
		t.Fatal(err)
	}
	want := ADTS{Profile: 2, SampleRate: 44100, Channels: 2, HeaderSize: 7, FrameSize: 100, Samples: 1024}
	if h != want {
		t.Errorf("%+v, want %+v", h, want)
	}

	// With a CRC, at 48 kHz mono, with two raw data blocks.
	b := adts(3, 1, 200)
	b[1] = 0xf0
	b[6] = 0xfc | 1
	if h, err = ParseADTS(b); err != nil {
		t.Fatal(err)
	}
	want = ADTS{Profile: 2, SampleRate: 48000, Channels: 1, HeaderSize: 9, FrameSize: 200, Samples: 2048}
	if h != want {
		t.Errorf("%+v, want %+v", h, want)
	}
}

func TestParseADTSErrors(t *testing.T) {
	cases := map[string][]byte{
		"Short":      {0xff, 0xf1, 0x50},
		"NoSync":     {0xff, 0x01, 0x50, 0x80, 0x0c, 0x9f, 0xfc},
		"BadRate":    {0xff, 0xf1, 0x7c, 0x80, 0x0c, 0x9f, 0xfc},
		"SizeTooLow": adts(4, 2, 7)[:7],
	}
	for name, b := range cases {
		t.Run(name, func(t *testing.T) {
			if h, err := ParseADTS(b); err == nil {
				t.Errorf("parsed %+v, want an error", h)
			}
		})
	}
}

func TestParseMP3(t *testing.T) {
	h, err := ParseMP3([]byte{0xff, 0xfb, 0x90, 0x64})
	if err != nil {
		t.Fatal(err)
	}
	want := MP3{Version: "1", Layer: 3, Bitrate: 128000, SampleRate: 44100, Channels: 2, FrameSize: 417, Samples: 1152}
	if h != want {
		t.Errorf("%+v, want %+v", h, want)
	}

	// MPEG-2 Layer III, 64 kbps at 24 kHz, padded, mono.
	if h, err = ParseMP3([]byte{0xff, 0xf3, 0x86, 0xc0}); err != nil {
		t.Fatal(err)
	}
	want = MP3{Version: "2", Layer: 3, Bitrate: 64000, SampleRate: 24000, Channels: 1, FrameSize: 193, Samples: 576}
	if h != want {
		t.Errorf("%+v, want %+v", h, want)
	}

	// MPEG-1 Layer II, 192 kbps at 48 kHz.
	if h, err = ParseMP3([]byte{0xff, 0xfd, 0xa4, 0x00}); err != nil {
		t.Fatal(err)
	}
	want = MP3{Version: "1", Layer: 2, Bitrate: 192000, SampleRate: 48000, Channels: 2, FrameSize: 576, Samples: 1152}
	if h != want {
		t.Errorf("%+v, want %+v", h, want)
	}
}

func TestParseMP3Errors(t *testing.T) {
	cases := map[string][]byte{
		"Short":         {0xff, 0xfb},
		"NoSync":        {0xff, 0x1b, 0x90, 0x64},
		"BadVersion":    {0xff, 0xeb, 0x90, 0x64},
		"BadLayer":      {0xff, 0xf9, 0x90, 0x64},
		"FreeBitrate":   {0xff, 0xfb, 0x00, 0x64},
		"BadBitrate":    {0xff, 0xfb, 0xf0, 0x64},
		"BadSampleRate": {0xff, 0xfb, 0x9c, 0x64},
	}
	for name, b := range cases {
		t.Run(name, func(t *testing.T) {
			if h, err := ParseMP3(b); err == nil {
				t.Errorf("parsed %+v, want an error", h)
			}
		})
	}
}

func TestSplitFrames(t *testing.T) {
	b := append(adts(4, 2, 100), adts(4, 2, 50)...)
	frames, err := SplitFrames("aac", b)
	if err != nil {
		t.Fatal(err)
	}
	if len(frames) != 2 || len(frames[0].Data) != 100 || len(frames[1].Data) != 50 {
		t.Fatalf("frames %+v, want 100 and 50 bytes", frames)
	}
	f := frames[0]
	if f.Codec != "aac" || f.SampleRate != 44100 || f.Channels != 2 || f.Samples != 1024 || f.Config == "" {
		t.Errorf("frame %+v", f)
	}

	frames, err = SplitFrames("mp3", append(mp3Frame(), mp3Frame()...))
	if err != nil {
		t.Fatal(err)
	}
	if len(frames) != 2 || frames[1].Codec != "mp3" || frames[1].Samples != 1152 || frames[1].Config != "" {
		t.Fatalf("frames %+v, want 2 MP3 frames", frames)
	}

	if _, err := SplitFrames("aac", adts(4, 2, 100)[:60]); err == nil {
		t.Errorf("split a cut frame, want an error")
	}
	if _, err := SplitFrames("opus", mp3Frame()); err == nil {
		t.Errorf("split an unknown codec, want an error")
	}
}

func TestCodecOf(t *testing.T) {
	for path, want := range map[string]string{
		"/live/a.aac": "aac", "/live/a.mp3": "mp3", "a.MP3": "mp3", "/live/a.flv": "", "/live/aac": "",
	} {
		if got := CodecOf(path); got != want {
			t.Errorf("CodecOf(%q) %q, want %q", path, got, want)
		}
	}
}

// TestReaderSkipsID3 reads an MP3 stream that starts with the 20-byte ID3v2
// tag SRS writes.
func TestReaderSkipsID3(t *testing.T) {
	id3 := []byte{'I', 'D', '3', 3, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
	r := NewReader(bytes.NewReader(append(append(id3, mp3Frame()...), mp3Frame()...)), "mp3")
	for i := 0; i < 2; i++ {
		f, err := r.ReadFrame()
		if err != nil {
			t.Fatalf("frame %v: %v", i, err)
		}
		if len(f.Data) != 417 {
			t.Errorf("frame %v has %v bytes, want 417", i, len(f.Data))
		}
	}
	if _, err := r.ReadFrame(); err != io.EOF {
		t.Errorf("after the last frame: %v, want EOF", err)
	}
}

func TestReaderErrors(t *testing.T) {
	r := NewReader(bytes.NewReader(append(adts(4, 2, 100), 0, 1, 2, 3, 4, 5, 6, 7)), "aac")
	if _, err := r.ReadFrame(); err != nil {
		t.Fatal(err)
	}
	if _, err := r.ReadFrame(); err == nil || !strings.Contains(err.Error(), "100") {
		t.Errorf("lost sync: %v, want an error at byte 100", err)
	}

	r = NewReader(bytes.NewReader(adts(4, 2, 100)[:50]), "aac")
	if _, err := r.ReadFrame(); err != io.ErrUnexpectedEOF {
		t.Errorf("cut frame: %v, want unexpected EOF", err)
	}
}

// TestTrack counts an AAC sequence header only when the configuration first
// appears or changes.
func TestTrack(t *testing.T) {
	var r report.Report
	var track Track
	for i, b := range [][]byte{adts(4, 2, 20), adts(4, 2, 20), adts(3, 2, 20), adts(3, 2, 20)} {
		f, err := ParseFrame("aac", b)
		if err != nil {
			t.Fatal(err)
		}
		track.Add(&r, f, time.Duration(i)*20*time.Millisecond)
	}
	a := r.Audio
	if a == nil || a.Codec != "aac" || a.SequenceHeaders != 2 || a.Frames != 4 || a.SampleRate != 48000 || a.Channels != 2 {
		t.Fatalf("report %v, want 2 sequence headers, 4 frames, 48000Hz 2ch", &r)
	}
	if a.LastDTS != 60*time.Millisecond {
		t.Errorf("last DTS %v, want 60ms", a.LastDTS)
	}

	// MP3 has no sequence header, but its rate is in each frame.
	var m report.Report
	f, err := ParseFrame("mp3", mp3Frame())
	if err != nil {
		t.Fatal(err)
	}
	track = Track{}
	track.Add(&m, f, 0)
	if a := m.Audio; a == nil || a.Codec != "mp3" || a.SequenceHeaders != 0 || a.Frames != 1 || a.SampleRate != 44100 || a.Channels != 2 {
		t.Errorf("report %v, want one mp3 frame at 44100Hz 2ch", &m)
	}
}

// TestAnalyzerTimesBySamples times the frames of a raw stream by the
// samples before them.
func TestAnalyzerTimesBySamples(t *testing.T) {
	var a Analyzer
	for i := 0; i < 3; i++ {
		f, err := ParseFrame("aac", adts(4, 2, 20))
		if err != nil {
			t.Fatal(err)
		}
		a.Add(f)
	}
	au := a.Report.Audio
	if au == nil || au.Frames != 3 || au.FirstDTS != 0 || au.LastDTS != 2048*time.Second/44100 {
		t.Errorf("report %v, want 3 frames from 0 to 2048 samples", &a.Report)
	}
}

// TestAnalyzeGenerated reads the audio of the test media, remuxed by ffmpeg
// into raw ADTS and MP3, and finds every frame of the source.
func TestAnalyzeGenerated(t *testing.T) {
	cases := []struct {
		name, file, format, codec string
		headers                   int
	}{
		{"Aac", media.AACOnly, "adts", "aac", 1},
		{"Mp3", media.H264HighMP3, "mp3", "mp3", 0},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			info, err := media.ProbeFile(media.Path(t, tc.file))
			if err != nil {
				t.Fatal(err)
			}
			f, err := os.Open(media.Remux(t, tc.file, tc.format, "-vn", "-write_xing", "0"))
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()

			r, err := Analyze(NewReader(f, tc.codec), report.Until{})
			if err != nil {
				t.Fatalf("%v: %v", err, r)
			}
			a := r.Audio
			if a == nil || a.Codec != tc.codec || a.Frames != info.Audio.Frames || a.SequenceHeaders != tc.headers {
				t.Fatalf("report %v, want %v %v frames and %v sequence headers", r, tc.codec, info.Audio.Frames, tc.headers)
			}
			if a.SampleRate != 44100 || a.Channels != 2 {
				t.Errorf("audio %vHz %vch, want 44100Hz 2ch", a.SampleRate, a.Channels)
			}
			if d := r.Duration() - info.Audio.Duration; d < -50*time.Millisecond || d > 50*time.Millisecond {
				t.Errorf("duration %v, want %v", r.Duration(), info.Audio.Duration)
			}
			if err := r.CheckTimestamps(30 * time.Millisecond); err != nil {
				t.Error(err)
			}
		})
	}
}

// TestPlayHTTP plays an .aac file from an HTTP server until 10 frames.
func TestPlayHTTP(t *testing.T) {
	path := media.Remux(t, media.AACOnly, "adts")
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/live/a.aac" {
			http.NotFound(w, r)
			return
		}
		http.ServeFile(w, r, path)
	}))
	defer srv.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	u := report.Until{AudioHeader: true, AudioFrames: 10}
	r, err := PlayHTTP(ctx, nil, srv.URL+"/live/a.aac", u)
	if err != nil {
		t.Fatal(err)
	}
	if a := r.Audio; a == nil || a.Frames != 10 || a.SequenceHeaders != 1 {
		t.Errorf("report %v, want 10 frames", r)
	}

	if _, err := PlayHTTP(ctx, nil, srv.URL+"/live/b.aac", u); err == nil || !strings.Contains(err.Error(), "404") {
		t.Errorf("missing stream: %v, want 404", err)
	}
	if _, err := PlayHTTP(ctx, nil, srv.URL+"/live/a.flv", u); err == nil || !strings.Contains(err.Error(), ".aac or .mp3") {
		t.Errorf("FLV URL: %v, want an error for the extension", err)
	}
}
