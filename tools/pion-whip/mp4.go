// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"

	"github.com/Eyevinn/mp4ff/mp4"
)

// mp4Sample is one video or audio sample of the input, ready for the WebRTC
// packetizer. Video is Annex B with 4-byte start codes and, on a sync sample,
// the SPS and PPS from avcC in front, so SRS finds them in-band. Audio is one
// Opus packet. DecodeTime and Duration are in the track's Timescale, and
// DecodeTime includes the offset of every Rewind so far, so a looped input keeps
// increasing timestamps.
type mp4Sample struct {
	Video      bool
	Data       []byte
	DecodeTime uint64
	Duration   uint32
	Timescale  uint32
	Sync       bool
}

// mp4Track is the read position in one track of the file.
type mp4Track struct {
	video     bool
	trak      *mp4.TrakBox
	timescale uint32
	nrSamples uint32
	duration  uint64 // decode time at the end of the last sample
	next      uint32 // 1-based number of the next sample to return
	offset    uint64 // added to every decode time, grows by duration on Rewind
	paramSets []byte // the SPS and PPS as Annex B, video only
}

// mp4Reader reads a progressive MP4 with one H.264 video track and one Opus
// audio track, the file FFmpeg writes for `-c:v libx264 -c:a libopus -f mp4`,
// and returns the samples of both tracks interleaved by decode time. It refuses
// B-frames, since WebRTC sends pictures in decode order with their presentation
// timestamps, and any other codec, since Go has no transcoder. The mdat is read
// lazily, one sample at a time, so the file is never loaded whole.
type mp4Reader struct {
	rs     io.ReadSeeker
	mdat   *mp4.MdatBox
	tracks []*mp4Track // video first, then audio
	// The file lasts as long as its longest track: loopDur ticks at loopScale Hz.
	loopDur   uint64
	loopScale uint32
}

func newMP4Reader(rs io.ReadSeeker) (*mp4Reader, error) {
	f, err := mp4.DecodeFile(rs, mp4.WithDecodeMode(mp4.DecModeLazyMdat))
	if err != nil {
		return nil, fmt.Errorf("decode mp4: %w", err)
	}
	if f.IsFragmented() {
		return nil, errors.New("fragmented MP4 is not supported; write a progressive file")
	}
	if f.Moov == nil || f.Mdat == nil {
		return nil, errors.New("mp4 has no moov or no mdat box")
	}

	var video, audio *mp4Track
	for _, trak := range f.Moov.Traks {
		if trak.Mdia == nil || trak.Mdia.Hdlr == nil || trak.Mdia.Mdhd == nil || trak.Mdia.Minf == nil || trak.Mdia.Minf.Stbl == nil {
			continue
		}
		switch trak.Mdia.Hdlr.HandlerType {
		case "vide":
			if video == nil {
				if video, err = newMP4VideoTrack(trak); err != nil {
					return nil, err
				}
			}
		case "soun":
			if audio == nil {
				if audio, err = newMP4AudioTrack(trak); err != nil {
					return nil, err
				}
			}
		}
	}
	if video == nil {
		return nil, errors.New("mp4 has no H.264 video track")
	}
	if audio == nil {
		return nil, errors.New("mp4 has no Opus audio track")
	}
	r := &mp4Reader{rs: rs, mdat: f.Mdat, tracks: []*mp4Track{video, audio}}
	for _, t := range r.tracks {
		if r.loopScale == 0 || t.duration*uint64(r.loopScale) > r.loopDur*uint64(t.timescale) {
			r.loopDur, r.loopScale = t.duration, t.timescale
		}
	}
	return r, nil
}

func newMP4Track(trak *mp4.TrakBox, video bool) (*mp4Track, error) {
	stbl := trak.Mdia.Minf.Stbl
	if stbl.Stsd == nil || stbl.Stts == nil || stbl.Stsz == nil || stbl.Stsc == nil || (stbl.Stco == nil && stbl.Co64 == nil) {
		return nil, fmt.Errorf("track %d lacks sample tables", trak.Tkhd.TrackID)
	}
	t := &mp4Track{video: video, trak: trak, timescale: trak.Mdia.Mdhd.Timescale, nrSamples: stbl.Stsz.GetNrSamples(), next: 1}
	if t.timescale == 0 {
		return nil, fmt.Errorf("track %d has timescale 0", trak.Tkhd.TrackID)
	}
	if t.nrSamples > 0 {
		dec, dur := stbl.Stts.GetDecodeTime(t.nrSamples)
		t.duration = dec + uint64(dur)
	}
	return t, nil
}

// sampleEntryType names the codec of a track for an error message.
func sampleEntryType(stsd *mp4.StsdBox) string {
	if len(stsd.Children) > 0 {
		return stsd.Children[0].Type()
	}
	return "unknown"
}

var startCode = []byte{0x00, 0x00, 0x00, 0x01}

func newMP4VideoTrack(trak *mp4.TrakBox) (*mp4Track, error) {
	t, err := newMP4Track(trak, true)
	if err != nil {
		return nil, err
	}
	stbl := trak.Mdia.Minf.Stbl
	if stbl.Stsd.AvcX == nil || stbl.Stsd.AvcX.AvcC == nil {
		return nil, fmt.Errorf("video codec %s is not supported, want H.264 (avc1)", sampleEntryType(stbl.Stsd))
	}
	if stbl.Ctts != nil {
		for _, offset := range stbl.Ctts.SampleOffset {
			if offset != 0 {
				return nil, errors.New("video has B-frames (a ctts box with composition offsets), which WebRTC cannot carry; encode with -profile:v baseline or -bf 0")
			}
		}
	}
	avcC := stbl.Stsd.AvcX.AvcC
	if len(avcC.SPSnalus) == 0 || len(avcC.PPSnalus) == 0 {
		return nil, errors.New("video avcC has no SPS or no PPS")
	}
	for _, nalus := range [][][]byte{avcC.SPSnalus, avcC.PPSnalus} {
		for _, nalu := range nalus {
			t.paramSets = append(t.paramSets, startCode...)
			t.paramSets = append(t.paramSets, nalu...)
		}
	}
	return t, nil
}

func newMP4AudioTrack(trak *mp4.TrakBox) (*mp4Track, error) {
	t, err := newMP4Track(trak, false)
	if err != nil {
		return nil, err
	}
	if stsd := trak.Mdia.Minf.Stbl.Stsd; stsd.Opus == nil {
		return nil, fmt.Errorf("audio codec %s is not supported, want Opus", sampleEntryType(stsd))
	}
	return t, nil
}

// nextTime is the decode time of the track's next sample, with the loop offset.
func (t *mp4Track) nextTime() uint64 {
	dec, _ := t.trak.Mdia.Minf.Stbl.Stts.GetDecodeTime(t.next)
	return dec + t.offset
}

// Next returns the next sample in decode order across both tracks, video first
// on a tie, or io.EOF when both tracks are exhausted.
func (r *mp4Reader) Next() (*mp4Sample, error) {
	var pick *mp4Track
	for _, t := range r.tracks {
		if t.next > t.nrSamples {
			continue
		}
		// Compare t.nextTime()/t.timescale with pick.nextTime()/pick.timescale without division.
		if pick == nil || t.nextTime()*uint64(pick.timescale) < pick.nextTime()*uint64(t.timescale) {
			pick = t
		}
	}
	if pick == nil {
		return nil, io.EOF
	}
	return r.read(pick)
}

func (r *mp4Reader) read(t *mp4Track) (*mp4Sample, error) {
	nr := t.next
	stbl := t.trak.Mdia.Minf.Stbl
	ranges, err := t.trak.GetRangesForSampleInterval(nr, nr)
	if err != nil {
		return nil, fmt.Errorf("locate sample %d: %w", nr, err)
	}
	if len(ranges) != 1 {
		return nil, fmt.Errorf("sample %d spans %d byte ranges", nr, len(ranges))
	}
	data, err := r.mdat.ReadData(int64(ranges[0].Offset), int64(ranges[0].Size), r.rs)
	if err != nil {
		return nil, fmt.Errorf("read sample %d: %w", nr, err)
	}

	dec, dur := stbl.Stts.GetDecodeTime(nr)
	s := &mp4Sample{Video: t.video, DecodeTime: dec + t.offset, Duration: dur, Timescale: t.timescale}
	if t.video {
		// Without an stss box every sample is a sync sample.
		s.Sync = stbl.Stss == nil || stbl.Stss.IsSyncSample(nr)
		annexB, err := avccToAnnexB(data)
		if err != nil {
			return nil, fmt.Errorf("video sample %d: %w", nr, err)
		}
		if s.Sync {
			s.Data = append(append(make([]byte, 0, len(t.paramSets)+len(annexB)), t.paramSets...), annexB...)
		} else {
			s.Data = annexB
		}
	} else {
		s.Data = data
	}
	t.next++
	return s, nil
}

// Rewind restarts both tracks from their first sample and adds the file's
// duration, that of its longest track, to the timestamps of both, so the next
// pass continues where this one ended and audio stays aligned with video. It
// is what -stream_loop uses, and it is how FFmpeg loops an input.
func (r *mp4Reader) Rewind() {
	for _, t := range r.tracks {
		t.offset += r.loopDur * uint64(t.timescale) / uint64(r.loopScale)
		t.next = 1
	}
}

// avccToAnnexB rewrites, in place, the 4-byte NAL lengths of an MP4 sample as
// start codes. The length is always 4 bytes here: mp4ff refuses an avcC with
// any other length size, and FFmpeg writes no other.
func avccToAnnexB(sample []byte) ([]byte, error) {
	for pos := 0; pos < len(sample); {
		left := len(sample) - pos
		if left < 4 {
			return nil, fmt.Errorf("%d trailing bytes are too short for a NAL length", left)
		}
		n := int(binary.BigEndian.Uint32(sample[pos:]))
		if n == 0 || n > left-4 {
			return nil, fmt.Errorf("NAL length %d exceeds the %d bytes left in the sample", n, left-4)
		}
		copy(sample[pos:pos+4], startCode)
		pos += 4 + n
	}
	return sample, nil
}
