// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package mp4

import (
	"bytes"
	"encoding/binary"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// box makes an MP4 box of its type and the concatenated parts.
func box(typ string, parts ...[]byte) []byte {
	body := bytes.Join(parts, nil)
	b := make([]byte, 8, 8+len(body))
	binary.BigEndian.PutUint32(b, uint32(8+len(body)))
	copy(b[4:], typ)
	return append(b, body...)
}

// full makes a full box: its version, its flags and the parts.
func full(typ string, version byte, flags uint32, parts ...[]byte) []byte {
	vf := []byte{version, byte(flags >> 16), byte(flags >> 8), byte(flags)}
	return box(typ, append([][]byte{vf}, parts...)...)
}

func u32(vs ...uint32) []byte {
	b := make([]byte, 4*len(vs))
	for i, v := range vs {
		binary.BigEndian.PutUint32(b[4*i:], v)
	}
	return b
}

func u64(v uint64) []byte {
	b := make([]byte, 8)
	binary.BigEndian.PutUint64(b, v)
	return b
}

func zeros(n int) []byte { return make([]byte, n) }

// trak makes a track of an init segment, with empty sample tables.
func trak(id uint32, handler, codec string, timescale uint32, config []byte) []byte {
	var entry []byte
	if handler == "vide" {
		// Reserved, data reference index, predefined, then width 768 and
		// height 320, then the rest of the 78-byte visual sample entry.
		e := append(zeros(24), 0x03, 0x00, 0x01, 0x40)
		entry = box(codec, e, zeros(78-len(e)), box("avcC", config))
	} else {
		// Reserved, data reference index, version and more, then 2 channels,
		// sample size, predefined and reserved, and 44100 in 16.16.
		e := append(zeros(16), 0x00, 0x02)
		e = append(e, zeros(6)...)
		e = append(e, u32(44100<<16)...)
		entry = box(codec, e, full("esds", 0, 0, config))
	}
	stbl := box("stbl",
		full("stsd", 0, 0, u32(1), entry),
		full("stts", 0, 0, u32(0)),
		full("stsc", 0, 0, u32(0)),
		full("stsz", 0, 0, u32(0, 0)),
		full("stco", 0, 0, u32(0)),
	)
	return box("trak",
		full("tkhd", 0, 0, u32(0, 0, id)),
		box("mdia",
			full("mdhd", 0, 0, u32(0, 0, timescale)),
			full("hdlr", 0, 0, u32(0), []byte(handler)),
			box("minf", stbl),
		),
	)
}

// The avcC of a 768x320 High stream: one SPS and one PPS, 4-byte lengths.
var avcC = []byte{
	0x01, 0x64, 0x00, 0x1e, 0xff, 0xe1, 0x00, 0x19,
	0x67, 0x64, 0x00, 0x1e, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01,
	0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x16, 0x2d, 0x96,
	0x01, 0x00, 0x04, 0x68, 0xef, 0x8f, 0xcb,
}

// esdsAAC is the body of an esds, after its full box header, for AAC-LC at
// 44.1 kHz stereo, as ffmpeg writes it: ES, decoder config, decoder specific
// and SL descriptors, with 4-byte lengths.
var esdsAAC = []byte{
	0x03, 0x80, 0x80, 0x80, 0x25, 0x00, 0x02, 0x00,
	0x04, 0x80, 0x80, 0x80, 0x17, 0x40, 0x15, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf4, 0x00, 0x00, 0x01, 0xf4, 0x00,
	0x05, 0x80, 0x80, 0x80, 0x05, 0x12, 0x10, 0x56, 0xe5, 0x00,
	0x06, 0x80, 0x80, 0x80, 0x01, 0x02,
}

// initSegment is an init segment with video track 1 at 1/1000 and audio
// track 2 at 1/44100, and trex defaults for track 2: duration 1024, size 4.
func initSegment() []byte {
	return append(
		box("ftyp", []byte("iso6"), u32(0)),
		box("moov",
			full("mvhd", 0, 0, zeros(96)),
			trak(1, "vide", "avc1", 1000, avcC),
			trak(2, "soun", "mp4a", 44100, esdsAAC),
			box("mvex",
				full("trex", 0, 0, u32(1, 1, 0, 0, 0)),
				full("trex", 0, 0, u32(2, 1, 1024, 4, 0)),
			),
		)...,
	)
}

// nonSync is the sample flags of a sample that depends on others and isn't
// a sync sample.
const nonSync = 0x01010000

// idr and slice are length-prefixed H.264 access units.
var (
	idr   = []byte{0, 0, 0, 2, 0x65, 0x88}
	slice = []byte{0, 0, 0, 2, 0x41, 0x9a}
)

func TestParseInit(t *testing.T) {
	init, err := ParseInit(initSegment())
	if err != nil {
		t.Fatal(err)
	}
	if len(init.Tracks) != 2 {
		t.Fatalf("%v tracks, want 2", len(init.Tracks))
	}
	v, a := init.Tracks[0], init.Tracks[1]
	if v.ID != 1 || v.Handler != "vide" || v.Codec != "avc1" || v.Timescale != 1000 || v.Width != 768 || v.Height != 320 {
		t.Errorf("video track %+v", v)
	}
	if a.ID != 2 || a.Handler != "soun" || a.Codec != "mp4a" || a.Timescale != 44100 || a.SampleRate != 44100 || a.Channels != 2 {
		t.Errorf("audio track %+v", a)
	}
	if d := init.Defaults[2]; d.Duration != 1024 || d.Size != 4 {
		t.Errorf("track 2 defaults %+v, want duration 1024 and size 4", d)
	}
}

func TestParseInitErrors(t *testing.T) {
	if _, err := ParseInit(box("ftyp", []byte("iso6"))); err == nil {
		t.Error("an init without moov parsed")
	}
	if _, err := ParseInit([]byte{0, 0, 0, 99, 'm', 'o', 'o', 'v'}); err == nil {
		t.Error("a cut moov parsed")
	}
}

// TestParseFragmentsDefaults reads a fragment whose samples take their
// duration, size and flags from tfhd, the first sample's flags from trun,
// and their data from the moof start (default-base-is-moof).
func TestParseFragmentsDefaults(t *testing.T) {
	init, err := ParseInit(initSegment())
	if err != nil {
		t.Fatal(err)
	}
	data := bytes.Join([][]byte{idr, slice, slice}, nil)
	// tfhd: default-base-is-moof, duration, size and flags.
	tfhd := full("tfhd", 0, 0x020038, u32(1, 40, 6, nonSync))
	tfdt := full("tfdt", 1, 0, u64(5000))
	moof := func(off uint32) []byte {
		return box("moof", full("mfhd", 0, 0, u32(7)), box("traf", tfhd, tfdt,
			full("trun", 0, 0x000005, u32(3, off, 0x02000000))))
	}
	off := uint32(len(moof(0)) + 8)
	seg := append(box("styp", []byte("msdh")), append(moof(off), box("mdat", data)...)...)

	samples, err := ParseFragments(init, seg)
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 3 {
		t.Fatalf("%v samples, want 3", len(samples))
	}
	for i, s := range samples {
		if s.Track != init.Tracks[0] {
			t.Errorf("sample %v has track %+v", i, s.Track)
		}
		if want := uint64(5000 + 40*i); s.DTS != want {
			t.Errorf("sample %v DTS %v, want %v", i, s.DTS, want)
		}
		if want := time.Duration(5000+40*i) * time.Millisecond; s.Time() != want {
			t.Errorf("sample %v time %v, want %v", i, s.Time(), want)
		}
		if s.Duration != 40 || len(s.Data) != 6 {
			t.Errorf("sample %v duration %v and %v bytes, want 40 and 6", i, s.Duration, len(s.Data))
		}
		if s.Key != (i == 0) {
			t.Errorf("sample %v key %v", i, s.Key)
		}
	}
	if !bytes.Equal(samples[0].Data, idr) || !bytes.Equal(samples[1].Data, slice) {
		t.Errorf("sample data %x %x", samples[0].Data, samples[1].Data)
	}
}

// TestParseFragmentsPerSample reads a version 1 trun with each sample's
// duration, size, flags and a negative composition offset, two trafs with
// neither a base offset nor default-base-is-moof, so the second's data
// follows the first's, and audio sizes from the trex defaults.
func TestParseFragmentsPerSample(t *testing.T) {
	init, err := ParseInit(initSegment())
	if err != nil {
		t.Fatal(err)
	}
	video := full("trun", 1, 0x000f01, u32(2, 0),
		u32(40, 6, 0x02000000), u32(uint32(0xffffffb0)), // -80
		u32(50, 6, nonSync), u32(40))
	audio := full("trun", 0, 0x000000, u32(2))
	moof := box("moof", full("mfhd", 0, 0, u32(1)),
		box("traf", full("tfhd", 0, 0, u32(1)), full("tfdt", 0, 0, u32(100)), video),
		box("traf", full("tfhd", 0, 0, u32(2)), full("tfdt", 0, 0, u32(4410)), audio))
	// The video trun's data offset is from the moof start.
	binary.BigEndian.PutUint32(moof[bytes.Index(moof, []byte("trun"))+12:], uint32(len(moof)+8))
	seg := append(moof, box("mdat", idr, slice, []byte("aaaa"), []byte("bbbb"))...)

	samples, err := ParseFragments(init, seg)
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 4 {
		t.Fatalf("%v samples, want 4", len(samples))
	}
	// Sorted by time: video and audio at 100ms, audio at 123ms, video at 140ms.
	var got []string
	for _, s := range samples {
		got = append(got, s.Track.Handler+":"+string(s.Data[len(s.Data)-1:]))
	}
	if want := []string{"vide:\x88", "soun:a", "soun:b", "vide:\x9a"}; strings.Join(got, ",") != strings.Join(want, ",") {
		t.Errorf("order %q, want %q", got, want)
	}
	v := samples[0]
	if v.CTS != -80 || v.Duration != 40 || !v.Key {
		t.Errorf("first video sample %+v", v)
	}
	if v := samples[3]; v.DTS != 140 || v.CTS != 40 || v.Key {
		t.Errorf("second video sample %+v", v)
	}
	if a := samples[2]; a.DTS != 4410+1024 || a.Duration != 1024 || string(a.Data) != "bbbb" {
		t.Errorf("second audio sample %+v", a)
	}
}

// TestParseFragmentsBaseOffset reads a traf with an explicit base data
// offset, from the start of the segment.
func TestParseFragmentsBaseOffset(t *testing.T) {
	init, err := ParseInit(initSegment())
	if err != nil {
		t.Fatal(err)
	}
	mdat := box("mdat", []byte("xxxx"), []byte("aaaa"))
	moof := func(base uint64) []byte {
		return box("moof", box("traf", full("tfhd", 0, 0x000001, u32(2), u64(base)), full("tfdt", 0, 0, u32(0)),
			full("trun", 0, 0x000001, u32(1, 4))))
	}
	seg := append(moof(0), mdat...)
	seg = append(moof(uint64(len(seg)-len(mdat)+8)), mdat...)

	samples, err := ParseFragments(init, seg)
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 1 || string(samples[0].Data) != "aaaa" {
		t.Errorf("samples %+v, want one of aaaa", samples)
	}
}

func TestParseFragmentsErrors(t *testing.T) {
	init, err := ParseInit(initSegment())
	if err != nil {
		t.Fatal(err)
	}
	cases := []struct {
		name string
		seg  []byte
		want string
	}{
		{"UnknownTrack", box("moof", box("traf", full("tfhd", 0, 0, u32(9)), full("trun", 0, 0, u32(1)))), "track 9"},
		{"PastTheEnd", box("moof", box("traf", full("tfhd", 0, 0x020000, u32(2)), full("trun", 0, 0x000001, u32(4, 1000)))), "past the end"},
		{"CutTrun", box("moof", box("traf", full("tfhd", 0, 0, u32(1)), full("trun", 0, 0x000300, u32(3)))), "trun"},
		{"NoTfhd", box("moof", box("traf", full("trun", 0, 0, u32(1)))), "tfhd"},
		{"CutBox", []byte{0, 0, 1, 0, 'm', 'o', 'o', 'f'}, "box"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			_, err := ParseFragments(init, tc.seg)
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Errorf("error %v, want it to contain %q", err, tc.want)
			}
		})
	}
}

// TestAnalyzer adds samples: the config of each track counts a sequence
// header once, and again when it changes; an H.264 keyframe is an IDR,
// whatever the sample flags say.
func TestAnalyzer(t *testing.T) {
	init, err := ParseInit(initSegment())
	if err != nil {
		t.Fatal(err)
	}
	v, a := init.Tracks[0], init.Tracks[1]
	var an Analyzer
	add := func(samples ...Sample) {
		t.Helper()
		if err := an.Add(samples, report.Until{}); err != nil {
			t.Fatal(err)
		}
	}
	add(Sample{Track: v, DTS: 0, Data: idr}, Sample{Track: a, DTS: 0, Data: []byte("a")})
	add(Sample{Track: v, DTS: 40, Data: slice, Key: true}, Sample{Track: a, DTS: 1024, Data: []byte("a")})

	r := &an.Report
	if r.Video == nil || r.Audio == nil {
		t.Fatalf("report %v", r)
	}
	if r.Video.Codec != "h264" || r.Video.Profile != "High" || r.Video.Width != 768 || r.Video.Height != 320 {
		t.Errorf("video %+v", r.Video)
	}
	if r.Video.SequenceHeaders != 1 || r.Video.Frames != 2 || r.Video.Keyframes != 1 {
		t.Errorf("video %v sequence headers, %v frames, %v keyframes, want 1, 2, 1", r.Video.SequenceHeaders, r.Video.Frames, r.Video.Keyframes)
	}
	if r.Audio.Codec != "aac" || r.Audio.SampleRate != 44100 || r.Audio.Channels != 2 || r.Audio.SequenceHeaders != 1 || r.Audio.Frames != 2 {
		t.Errorf("audio %+v", r.Audio)
	}
	if r.Audio.LastDTS != 1024*time.Second/44100 {
		t.Errorf("audio last DTS %v", r.Audio.LastDTS)
	}

	// A new init with the same config doesn't count a header; another
	// config does.
	again, _ := ParseInit(initSegment())
	add(Sample{Track: again.Tracks[0], DTS: 80, Data: slice})
	if r.Video.SequenceHeaders != 1 {
		t.Errorf("%v video sequence headers after the same config, want 1", r.Video.SequenceHeaders)
	}
	changed := *again.Tracks[0]
	changed.Config = append(append([]byte{}, avcC[:len(avcC)-1]...), 0xcc)
	add(Sample{Track: &changed, DTS: 120, Data: slice})
	if r.Video.SequenceHeaders != 2 {
		t.Errorf("%v video sequence headers after a new config, want 2", r.Video.SequenceHeaders)
	}
}

// TestAnalyzerUntil stops adding at the first sample that meets the
// condition.
func TestAnalyzerUntil(t *testing.T) {
	init, err := ParseInit(initSegment())
	if err != nil {
		t.Fatal(err)
	}
	var samples []Sample
	for i := range 10 {
		samples = append(samples, Sample{Track: init.Tracks[0], DTS: uint64(40 * i), Data: slice})
	}
	var an Analyzer
	if err := an.Add(samples, report.Until{VideoFrames: 4}); err != nil {
		t.Fatal(err)
	}
	if n := an.Report.Video.Frames; n != 4 {
		t.Errorf("%v frames, want 4", n)
	}
}

// TestAnalyzeFile reads the test media as MP4 files, progressive with
// sample tables and fragmented with moof boxes, and each gives the whole
// stream.
func TestAnalyzeFile(t *testing.T) {
	cases := []struct {
		name           string
		path           string
		vcodec, acodec string
		rate, channels int
	}{
		{"Progressive", media.Remux(t, media.H264HighAAC, "mp4"), "h264", "aac", 44100, 2},
		{"Fragmented", media.Remux(t, media.H264HighAAC, "mp4", "-movflags", "frag_keyframe+empty_moov+default_base_moof"), "h264", "aac", 44100, 2},
		{"Hevc", media.Remux(t, media.HEVCAAC, "mp4", "-tag:v", "hvc1"), "hevc", "aac", 44100, 2},
		{"Opus", media.Path(t, media.H264BaselineOpus), "h264", "opus", 48000, 2},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			r, err := AnalyzeFile(tc.path, report.Until{})
			if err != nil {
				t.Fatal(err)
			}
			if r.Video == nil || r.Audio == nil {
				t.Fatalf("report %v", r)
			}
			v, a := r.Video, r.Audio
			if v.Codec != tc.vcodec || v.Width != 768 || v.Height != 320 || v.SequenceHeaders != 1 {
				t.Errorf("video %v", r)
			}
			if v.Frames != 750 || v.Keyframes != 30 {
				t.Errorf("%v frames and %v keyframes, want 750 and 30", v.Frames, v.Keyframes)
			}
			if a.Codec != tc.acodec || a.SampleRate != tc.rate || a.Channels != tc.channels || a.SequenceHeaders != 1 {
				t.Errorf("audio %v", r)
			}
			if err := r.CheckTimestamps(100 * time.Millisecond); err != nil {
				t.Error(err)
			}
			if d := r.Duration(); d < 29*time.Second || d > 31*time.Second {
				t.Errorf("duration %v, want about 30s", d)
			}
		})
	}

	r, err := AnalyzeFile(media.Remux(t, media.H264HighAAC, "mp4"), report.Until{Keyframes: 2})
	if err != nil {
		t.Fatal(err)
	}
	if r.Video.Keyframes != 2 || r.Video.Frames != 26 {
		t.Errorf("%v keyframes and %v frames, want 2 and 26", r.Video.Keyframes, r.Video.Frames)
	}
	if _, err := AnalyzeFile("/nothing/a.mp4", report.Until{}); err == nil {
		t.Error("a missing file analyzed")
	}
}
