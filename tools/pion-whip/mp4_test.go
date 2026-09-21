// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"os"
	"strings"
	"testing"

	"github.com/Eyevinn/mp4ff/avc"
	"github.com/Eyevinn/mp4ff/mp4"
)

var (
	nalSPS = []byte{0x67, 0x42, 0xc0, 0x1f, 0xda}
	nalPPS = []byte{0x68, 0xce, 0x38, 0x80}
	nalSEI = []byte{0x06, 0x05, 0x01, 0x80}
	nalIDR = []byte{0x65, 0x88, 0x84, 0x00}
	nalP   = []byte{0x41, 0x9a, 0x02}
)

// fixtureTrack describes one track of an in-memory MP4 built by buildMP4.
type fixtureTrack struct {
	video     bool
	aac       bool // audio only: mp4a instead of Opus
	timescale uint32
	samples   [][]byte // sample data as stored, AVCC for video
	durations []uint32
	syncs     []uint32 // 1-based sync sample numbers, video only
	ctts      []int32  // composition offsets, one per sample, video only
}

// avcc wraps NAL units with 4-byte lengths, the way an MP4 sample stores them.
func avcc(nals ...[]byte) []byte {
	var out []byte
	for _, nal := range nals {
		out = binary.BigEndian.AppendUint32(out, uint32(len(nal)))
		out = append(out, nal...)
	}
	return out
}

// annexB4 joins NAL units with 4-byte start codes, the form the reader returns.
func annexB4(nals ...[]byte) []byte {
	var out []byte
	for _, nal := range nals {
		out = append(out, 0x00, 0x00, 0x00, 0x01)
		out = append(out, nal...)
	}
	return out
}

// buildMP4 writes a progressive MP4 with the given tracks: ftyp, moov and one
// mdat holding every track's samples in one chunk per track.
func buildMP4(t *testing.T, tracks ...fixtureTrack) []byte {
	t.Helper()
	f := mp4.NewFile()
	f.AddChild(mp4.CreateFtyp(), 0)
	moov := mp4.NewMoovBox()
	mvhd := mp4.CreateMvhd()
	mvhd.Timescale = 1000
	moov.AddChild(mvhd)

	var data []byte
	var stcos []*mp4.StcoBox
	var chunkStarts []uint64
	for i, tr := range tracks {
		mediaType := "audio"
		if tr.video {
			mediaType = "video"
		}
		trak := mp4.CreateEmptyTrak(uint32(i+1), tr.timescale, mediaType, "und")
		stsd := trak.Mdia.Minf.Stbl.Stsd
		switch {
		case tr.video:
			avcC := &mp4.AvcCBox{DecConfRec: avc.DecConfRec{
				AVCProfileIndication: 66, ProfileCompatibility: 0xc0, AVCLevelIndication: 31,
				SPSnalus: [][]byte{nalSPS}, PPSnalus: [][]byte{nalPPS},
			}}
			stsd.AddChild(mp4.CreateVisualSampleEntryBox("avc1", 320, 240, avcC))
		case tr.aac:
			if err := trak.SetAACDescriptor(2, 48000); err != nil {
				t.Fatalf("SetAACDescriptor: %v", err)
			}
		default:
			dops := &mp4.DopsBox{OutputChannelCount: 2, PreSkip: 312, InputSampleRate: 48000}
			stsd.AddChild(mp4.CreateAudioSampleEntryBox("Opus", 2, 16, 48000, dops))
		}

		stbl := trak.Mdia.Minf.Stbl
		var total uint64
		for j, s := range tr.samples {
			stbl.Stts.SampleCount = append(stbl.Stts.SampleCount, 1)
			stbl.Stts.SampleTimeDelta = append(stbl.Stts.SampleTimeDelta, tr.durations[j])
			stbl.Stsz.SampleSize = append(stbl.Stsz.SampleSize, uint32(len(s)))
			total += uint64(tr.durations[j])
		}
		stbl.Stsz.SampleNumber = uint32(len(tr.samples))
		if err := stbl.Stsc.AddEntry(1, uint32(len(tr.samples)), 1); err != nil {
			t.Fatalf("stsc.AddEntry: %v", err)
		}
		stbl.Stco.ChunkOffset = []uint32{0}
		stcos = append(stcos, stbl.Stco)
		if len(tr.syncs) > 0 {
			stbl.AddChild(&mp4.StssBox{SampleNumber: tr.syncs})
		}
		if len(tr.ctts) > 0 {
			ctts := &mp4.CttsBox{}
			counts := make([]uint32, len(tr.ctts))
			for j := range counts {
				counts[j] = 1
			}
			if err := ctts.AddSampleCountsAndOffset(counts, tr.ctts); err != nil {
				t.Fatalf("ctts: %v", err)
			}
			stbl.AddChild(ctts)
		}
		trak.Mdia.Mdhd.Duration = total
		moov.AddChild(trak)

		chunkStarts = append(chunkStarts, uint64(len(data)))
		for _, s := range tr.samples {
			data = append(data, s...)
		}
	}
	f.AddChild(moov, 0)
	mdat := &mp4.MdatBox{}
	mdat.SetData(data)
	f.AddChild(mdat, 0)

	// The chunk offsets are absolute, so they need the sizes of the boxes before the mdat payload.
	base := f.Ftyp.Size() + moov.Size() + mdat.HeaderSize()
	for i, stco := range stcos {
		stco.ChunkOffset[0] = uint32(base + chunkStarts[i])
	}
	var buf bytes.Buffer
	if err := f.Encode(&buf); err != nil {
		t.Fatalf("encode mp4: %v", err)
	}
	return buf.Bytes()
}

// videoTrack is three pictures at 25 fps in FFmpeg's 12800 Hz timescale: an
// IDR with an SEI in front, then two P pictures.
func videoTrack() fixtureTrack {
	return fixtureTrack{
		video: true, timescale: 12800,
		samples:   [][]byte{avcc(nalSEI, nalIDR), avcc(nalP), avcc(nalP)},
		durations: []uint32{512, 512, 512},
		syncs:     []uint32{1},
	}
}

// audioTrack is three 20 ms Opus packets at 48 kHz.
func audioTrack() fixtureTrack {
	return fixtureTrack{
		timescale: 48000,
		samples:   [][]byte{{0xfc, 0xff, 0xfe}, {0xfc, 0x01}, {0xfc, 0x02, 0x03}},
		durations: []uint32{960, 960, 960},
	}
}

func readAllSamples(t *testing.T, r *mp4Reader) []*mp4Sample {
	t.Helper()
	var out []*mp4Sample
	for {
		s, err := r.Next()
		if err == io.EOF {
			return out
		}
		if err != nil {
			t.Fatalf("sample %d: %v", len(out), err)
		}
		out = append(out, s)
	}
}

func expectSample(t *testing.T, got *mp4Sample, want mp4Sample) {
	t.Helper()
	if got.Video != want.Video || got.DecodeTime != want.DecodeTime || got.Duration != want.Duration ||
		got.Timescale != want.Timescale || got.Sync != want.Sync || !bytes.Equal(got.Data, want.Data) {
		t.Errorf("sample = {video:%v t:%d dur:%d ts:%d sync:%v data:%x}, want {video:%v t:%d dur:%d ts:%d sync:%v data:%x}",
			got.Video, got.DecodeTime, got.Duration, got.Timescale, got.Sync, got.Data,
			want.Video, want.DecodeTime, want.Duration, want.Timescale, want.Sync, want.Data)
	}
}

func TestMP4ReaderInterleavesTracksByDecodeTimeAndRestoresAnnexB(t *testing.T) {
	r, err := newMP4Reader(bytes.NewReader(buildMP4(t, videoTrack(), audioTrack())))
	if err != nil {
		t.Fatalf("newMP4Reader: %v", err)
	}
	got := readAllSamples(t, r)
	// Video at 0, 40 and 80 ms; audio at 0, 20 and 40 ms; video first on a tie.
	// The sync sample gets SPS and PPS in front, the others only start codes.
	want := []mp4Sample{
		{Video: true, Data: annexB4(nalSPS, nalPPS, nalSEI, nalIDR), DecodeTime: 0, Duration: 512, Timescale: 12800, Sync: true},
		{Data: []byte{0xfc, 0xff, 0xfe}, DecodeTime: 0, Duration: 960, Timescale: 48000},
		{Data: []byte{0xfc, 0x01}, DecodeTime: 960, Duration: 960, Timescale: 48000},
		{Video: true, Data: annexB4(nalP), DecodeTime: 512, Duration: 512, Timescale: 12800},
		{Data: []byte{0xfc, 0x02, 0x03}, DecodeTime: 1920, Duration: 960, Timescale: 48000},
		{Video: true, Data: annexB4(nalP), DecodeTime: 1024, Duration: 512, Timescale: 12800},
	}
	if len(got) != len(want) {
		t.Fatalf("got %d samples, want %d", len(got), len(want))
	}
	for i := range want {
		expectSample(t, got[i], want[i])
	}
}

func TestMP4ReaderRewindContinuesTimestampsAfterTheTrackDuration(t *testing.T) {
	r, err := newMP4Reader(bytes.NewReader(buildMP4(t, videoTrack(), audioTrack())))
	if err != nil {
		t.Fatalf("newMP4Reader: %v", err)
	}
	if n := len(readAllSamples(t, r)); n != 6 {
		t.Fatalf("first pass read %d samples, want 6", n)
	}
	if _, err := r.Next(); err != io.EOF {
		t.Fatalf("Next after the end = %v, want io.EOF", err)
	}

	r.Rewind()
	got := readAllSamples(t, r)
	if len(got) != 6 {
		t.Fatalf("second pass read %d samples, want 6", len(got))
	}
	// The video track lasts 120 ms and the audio track 60 ms, so the file lasts
	// 120 ms and both tracks restart there, as FFmpeg's -stream_loop does, so
	// audio stays aligned with video: video at 1536 ticks of 12800 Hz, audio at
	// 5760 ticks of 48000 Hz. The sync sample carries the parameter sets again.
	expectSample(t, got[0], mp4Sample{Video: true, Data: annexB4(nalSPS, nalPPS, nalSEI, nalIDR), DecodeTime: 1536, Duration: 512, Timescale: 12800, Sync: true})
	expectSample(t, got[1], mp4Sample{Data: []byte{0xfc, 0xff, 0xfe}, DecodeTime: 5760, Duration: 960, Timescale: 48000})
	expectSample(t, got[5], mp4Sample{Video: true, Data: annexB4(nalP), DecodeTime: 2560, Duration: 512, Timescale: 12800})
}

func TestMP4ReaderRejectsWhatWebRTCCannotCarry(t *testing.T) {
	bframes := videoTrack()
	bframes.ctts = []int32{1024, 0, -512}
	cases := []struct {
		name   string
		tracks []fixtureTrack
		want   string
	}{
		{"b-frames", []fixtureTrack{bframes, audioTrack()}, "B-frames"},
		{"aac audio", []fixtureTrack{videoTrack(), {aac: true, timescale: 48000, samples: [][]byte{{0x21}}, durations: []uint32{1024}}}, "audio codec mp4a"},
		{"no video", []fixtureTrack{audioTrack()}, "no H.264 video track"},
		{"no audio", []fixtureTrack{videoTrack()}, "no Opus audio track"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := newMP4Reader(bytes.NewReader(buildMP4(t, c.tracks...)))
			if err == nil {
				t.Fatalf("newMP4Reader succeeded, want an error mentioning %q", c.want)
			}
			if !strings.Contains(err.Error(), c.want) {
				t.Errorf("error = %q, want it to mention %q", err.Error(), c.want)
			}
		})
	}
}

func TestMP4ReaderRejectsATruncatedNalLength(t *testing.T) {
	video := videoTrack()
	// The length says 9 bytes but only 4 follow.
	video.samples[1] = append(binary.BigEndian.AppendUint32(nil, 9), nalP...)
	r, err := newMP4Reader(bytes.NewReader(buildMP4(t, video, audioTrack())))
	if err != nil {
		t.Fatalf("newMP4Reader: %v", err)
	}
	var lastErr error
	for i := 0; i < 6 && lastErr == nil; i++ {
		_, lastErr = r.Next()
	}
	if lastErr == nil || lastErr == io.EOF {
		t.Fatalf("Next returned %v, want an error for the truncated NAL length", lastErr)
	}
	if !strings.Contains(lastErr.Error(), "NAL") {
		t.Errorf("error = %q, want it to name the NAL length", lastErr.Error())
	}
}

// TestMP4ReaderReadsAnFFmpegFile checks a real file when PION_WHIP_MP4_FILE
// names one, made with `ffmpeg -i in -c:v libx264 -profile:v baseline -c:a
// libopus -f mp4 out.mp4`; it is skipped otherwise, so the suite needs no fixture.
func TestMP4ReaderReadsAnFFmpegFile(t *testing.T) {
	path := os.Getenv("PION_WHIP_MP4_FILE")
	if path == "" {
		t.Skip("set PION_WHIP_MP4_FILE to an H.264 baseline plus Opus MP4 to run this test")
	}
	f, err := os.Open(path)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	defer f.Close()
	r, err := newMP4Reader(f)
	if err != nil {
		t.Fatalf("newMP4Reader: %v", err)
	}
	samples := readAllSamples(t, r)
	var video, audio, syncs int
	var lastVideo, lastAudio uint64
	for i, s := range samples {
		if len(s.Data) == 0 {
			t.Fatalf("sample %d is empty", i)
		}
		if s.Video {
			if s.Timescale == 0 || s.DecodeTime < lastVideo {
				t.Fatalf("video sample %d: time %d/%d went backwards from %d", i, s.DecodeTime, s.Timescale, lastVideo)
			}
			lastVideo = s.DecodeTime
			if !bytes.HasPrefix(s.Data, []byte{0, 0, 0, 1}) {
				t.Fatalf("video sample %d does not start with a start code: %x", i, s.Data[:8])
			}
			if s.Sync {
				syncs++
				if s.Data[4]&0x1f != 7 {
					t.Fatalf("sync sample %d does not start with an SPS: %x", i, s.Data[:8])
				}
			}
			video++
		} else {
			if s.DecodeTime < lastAudio {
				t.Fatalf("audio sample %d: time went backwards", i)
			}
			lastAudio = s.DecodeTime
			audio++
		}
	}
	if video == 0 || audio == 0 || syncs == 0 || !samples[0].Video || !samples[0].Sync {
		t.Fatalf("video=%d audio=%d syncs=%d first video sync=%v", video, audio, syncs, samples[0].Video && samples[0].Sync)
	}
	t.Logf("%s: %d video samples (%d sync), %d audio samples", path, video, syncs, audio)
}
