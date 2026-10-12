// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"bytes"
	"slices"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/ts"
)

// annexB joins NALs with 4-byte start codes.
func annexB(nals ...[]byte) []byte {
	var b []byte
	for _, n := range nals {
		b = append(append(b, 0, 0, 0, 1), n...)
	}
	return b
}

// TestWithoutSEI drops the SEI NALs of H.264 and HEVC samples, keeps every
// other NAL with its start code, leaves other codecs as they are, and
// doesn't change the samples it was given.
func TestWithoutSEI(t *testing.T) {
	h264 := annexB([]byte{0x67, 1}, []byte{0x68, 2}, []byte{0x06, 3}, []byte{0x65, 4})
	hevc := annexB([]byte{0x40, 1, 1}, []byte{0x42, 1, 2}, []byte{0x44, 1, 3}, []byte{0x4e, 1, 4}, []byte{0x28, 1, 5}, []byte{0x50, 1, 6})
	opus := []byte{0x06, 0, 0, 1, 0x06}
	in := []Sample{
		{Video: true, Codec: "h264", Key: true, Data: h264},
		{Video: true, Codec: "hevc", Key: true, Data: hevc},
		{Codec: "opus", Data: opus},
	}
	orig := [][]byte{slices.Clone(h264), slices.Clone(hevc), slices.Clone(opus)}
	out := WithoutSEI(in)
	want := [][]byte{
		annexB([]byte{0x67, 1}, []byte{0x68, 2}, []byte{0x65, 4}),
		annexB([]byte{0x40, 1, 1}, []byte{0x42, 1, 2}, []byte{0x44, 1, 3}, []byte{0x28, 1, 5}),
		opus,
	}
	if len(out) != len(in) {
		t.Fatalf("%v samples, want %v", len(out), len(in))
	}
	for i := range out {
		if !bytes.Equal(out[i].Data, want[i]) {
			t.Errorf("sample %v: % x, want % x", i, out[i].Data, want[i])
		}
		if out[i].Codec != in[i].Codec || out[i].Key != in[i].Key || out[i].Video != in[i].Video {
			t.Errorf("sample %v: %+v, want the fields of %+v", i, out[i], in[i])
		}
		if !bytes.Equal(in[i].Data, orig[i]) {
			t.Errorf("sample %v given changed: % x", i, in[i].Data)
		}
	}
}

// TestWithoutSEIMedia checks the HEVC test file's keyframes, which have an
// SEI before the IDR, lose only the SEI.
func TestWithoutSEIMedia(t *testing.T) {
	all, err := ReadMP4(media.Path(t, media.HEVCOpus))
	if err != nil {
		t.Fatal(err)
	}
	var in []Sample
	for _, s := range all {
		if s.DTS < 2*time.Second {
			in = append(in, s)
		}
	}
	types := func(s Sample) []int {
		var out []int
		for _, nal := range ts.SplitNALs(s.Data) {
			out = append(out, int(nal[0]>>1)&0x3f)
		}
		return out
	}
	out := WithoutSEI(in)
	keys := 0
	for i, s := range out {
		if !s.Video || !s.Key {
			continue
		}
		keys++
		if !slices.Contains(types(in[i]), 39) {
			t.Errorf("keyframe %v had no SEI: %v", i, types(in[i]))
		}
		if got := types(s); !slices.Equal(got, []int{32, 33, 34, 20}) {
			t.Errorf("keyframe %v: NAL types %v, want VPS, SPS, PPS and IDR", i, got)
		}
	}
	if keys != 2 {
		t.Errorf("%v keyframes, want 2", keys)
	}
}
