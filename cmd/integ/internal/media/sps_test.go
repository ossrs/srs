// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"encoding/hex"
	"io"
	"os"
	"testing"
)

// The SPS NAL units of one frame made by libx264 and libx265 from the lavfi
// testsrc, so they crop: 1920x1080 is 1088 coded lines, and 1918x1078 is
// coded as 1920x1080 with a conformance window.
const (
	spsH264Yuv444 = "67f40028919b280f0044fc4e0220000003002000000641e30632c0"
	spsH264Yuv420 = "67640028acd940780227e5c044000003000400000300c83c60c658"
	spsHEVCYuv444 = "4201010408000003009e0800000300007890007810021ceef2cacd24995e02dc080800100000030010000003019080"
	spsHEVCYuv420 = "420101016000000300900000030000030078a003c08010e75596566924caf0168080000003008000000c84"
)

func unhex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestSPSSize(t *testing.T) {
	cases := []struct {
		name, codec, sps string
		w, h             int
	}{
		{"H264High444", "h264", spsH264Yuv444, 1920, 1080},
		{"H264High420", "h264", spsH264Yuv420, 1920, 1080},
		{"HEVC444", "hevc", spsHEVCYuv444, 1918, 1078},
		{"HEVC420", "hevc", spsHEVCYuv420, 1918, 1078},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			w, h, err := SPSSize(tc.codec, unhex(t, tc.sps))
			if err != nil {
				t.Fatal(err)
			}
			if w != tc.w || h != tc.h {
				t.Errorf("size %vx%v, want %vx%v", w, h, tc.w, tc.h)
			}
		})
	}
}

func TestSPSSizeErrors(t *testing.T) {
	cases := []struct {
		name, codec, sps string
	}{
		{"Empty", "h264", ""},
		{"NotAnSPS", "h264", "68ebe3cb22c0"},
		{"Truncated", "h264", spsH264Yuv420[:10]},
		{"HEVCNotAnSPS", "hevc", "4401c172b4624"[:12]},
		{"HEVCTruncated", "hevc", spsHEVCYuv420[:20]},
		{"UnknownCodec", "vp9", spsH264Yuv420},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if w, h, err := SPSSize(tc.codec, unhex(t, tc.sps)); err == nil {
				t.Errorf("size %vx%v, want an error", w, h)
			}
		})
	}
}

// avcC wraps one SPS in a decoder configuration record.
func avcC(sps []byte) []byte {
	b := []byte{1, sps[1], sps[2], sps[3], 0xff, 0xe1, byte(len(sps) >> 8), byte(len(sps))}
	b = append(b, sps...)
	return append(b, 1, 0, 0) // One empty PPS, not read.
}

// hvcC wraps one SPS in a decoder configuration record, after a VPS array.
func hvcC(sps []byte) []byte {
	b := make([]byte, 22)
	b[0] = 1
	b = append(b, 2)
	b = append(b, 0x20, 0, 1, 0, 2, 0x40, 0x01) // A VPS of 2 bytes, not read.
	b = append(b, 0x21, 0, 1, byte(len(sps)>>8), byte(len(sps)))
	return append(b, sps...)
}

func TestVideoSize(t *testing.T) {
	w, h, err := VideoSize("h264", avcC(unhex(t, spsH264Yuv420)))
	if err != nil || w != 1920 || h != 1080 {
		t.Errorf("avcC size %vx%v %v, want 1920x1080", w, h, err)
	}
	w, h, err = VideoSize("hevc", hvcC(unhex(t, spsHEVCYuv420)))
	if err != nil || w != 1918 || h != 1078 {
		t.Errorf("hvcC size %vx%v %v, want 1918x1078", w, h, err)
	}
	for _, bad := range [][]byte{nil, {1, 100, 0, 40, 0xff, 0xe0}, {1, 100, 0, 40, 0xff, 0xe1, 0, 9, 0x67}} {
		if _, _, err := VideoSize("h264", bad); err == nil {
			t.Errorf("avcC %x has a size, want an error", bad)
		}
	}
	if _, _, err := VideoSize("hevc", make([]byte, 23)); err == nil {
		t.Errorf("hvcC without an SPS has a size, want an error")
	}
	if _, _, err := VideoSize("av1", []byte{0x81, 0, 0, 0}); err == nil {
		t.Errorf("av1C has a size, want an error")
	}
}

// TestVideoSizeOfTheMedia reads the size of each FLV test file's sequence
// header: the source's 768x320.
func TestVideoSizeOfTheMedia(t *testing.T) {
	for _, name := range []string{H264HighAAC, H264BaselineAAC, HEVCAAC, H264Only, H264HighMP3} {
		t.Run(name, func(t *testing.T) {
			f, err := os.Open(Path(t, name))
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()
			r, err := NewFLVReader(f)
			if err != nil {
				t.Fatal(err)
			}
			for {
				tag, err := r.ReadTag()
				if err == io.EOF {
					t.Fatal("no video sequence header")
				} else if err != nil {
					t.Fatal(err)
				}
				if tag.Type != TagVideo {
					continue
				}
				v, err := ParseVideo(tag.Data)
				if err != nil {
					t.Fatal(err)
				}
				if !v.SequenceHeader {
					continue
				}
				w, h, err := VideoSize(v.Codec, v.Payload)
				if err != nil || w != 768 || h != 320 {
					t.Errorf("size %vx%v %v, want 768x320", w, h, err)
				}
				return
			}
		})
	}
}

// TestAV1SequenceSize reads the sequence header OBU in the av1C of the AV1
// test file: Main, 768x320.
func TestAV1SequenceSize(t *testing.T) {
	f, err := os.Open(Path(t, AV1Opus))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		t.Fatal(err)
	}
	mf, err := ReadMP4(f, st.Size())
	if err != nil {
		t.Fatal(err)
	}
	// av1C: 4 bytes, then the sequence header OBU with its size field.
	obu := mf.Track("vide").Config[4:]
	if len(obu) < 2 || (obu[0]>>3)&0xf != 1 || obu[0]&2 == 0 {
		t.Fatalf("av1C config OBU %x, want a sequence header with a size", obu)
	}
	head := 1
	if obu[0]&4 != 0 {
		head++
	}
	var size, shift int
	for {
		b := obu[head]
		head++
		size |= int(b&0x7f) << shift
		shift += 7
		if b&0x80 == 0 {
			break
		}
	}
	profile, w, h, err := AV1SequenceSize(obu[head : head+size])
	if err != nil || profile != "Main" || w != 768 || h != 320 {
		t.Errorf("AV1 sequence header: %v %vx%v %v, want Main 768x320", profile, w, h, err)
	}
	if _, _, _, err := AV1SequenceSize(obu[head : head+2]); err == nil {
		t.Errorf("a cut sequence header has a size, want an error")
	}
}
