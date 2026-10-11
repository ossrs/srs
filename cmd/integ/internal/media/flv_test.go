// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"bytes"
	"encoding/binary"
	"errors"
	"io"
	"testing"
)

// flvBytes builds an FLV file with the header flags and tags.
func flvBytes(flags byte, tags ...FLVTag) []byte {
	var b bytes.Buffer
	b.Write([]byte{'F', 'L', 'V', 1, flags, 0, 0, 0, 9, 0, 0, 0, 0})
	for _, t := range tags {
		n := len(t.Data)
		b.Write([]byte{t.Type, byte(n >> 16), byte(n >> 8), byte(n),
			byte(t.Timestamp >> 16), byte(t.Timestamp >> 8), byte(t.Timestamp), byte(t.Timestamp >> 24),
			0, 0, 0})
		b.Write(t.Data)
		binary.Write(&b, binary.BigEndian, uint32(11+n))
	}
	return b.Bytes()
}

func TestFLVReaderReadsTags(t *testing.T) {
	tags := []FLVTag{
		{TagScript, 0, []byte{2, 0, 10}},
		{TagVideo, 40, []byte{0x17, 0, 0, 0, 0}},
		{TagAudio, 0x01020304, []byte{0xaf, 1, 9}}, // a timestamp past 24 bits
	}
	r, err := NewFLVReader(bytes.NewReader(flvBytes(5, tags...)))
	if err != nil {
		t.Fatal(err)
	}
	if !r.HasAudio || !r.HasVideo {
		t.Errorf("flags: audio %v video %v", r.HasAudio, r.HasVideo)
	}
	for i, want := range tags {
		got, err := r.ReadTag()
		if err != nil {
			t.Fatalf("tag %v: %v", i, err)
		}
		if got.Type != want.Type || got.Timestamp != want.Timestamp || !bytes.Equal(got.Data, want.Data) {
			t.Errorf("tag %v: got %+v, want %+v", i, got, want)
		}
	}
	if _, err := r.ReadTag(); err != io.EOF {
		t.Errorf("after the last tag: %v, want EOF", err)
	}
}

func TestFLVReaderErrors(t *testing.T) {
	if _, err := NewFLVReader(bytes.NewReader([]byte("FLX\x01\x05\x00\x00\x00\x09\x00\x00\x00\x00"))); err == nil {
		t.Errorf("no error for a bad signature")
	}
	if _, err := NewFLVReader(bytes.NewReader([]byte("FLV"))); err == nil {
		t.Errorf("no error for a short header")
	}

	b := flvBytes(1, FLVTag{TagVideo, 0, []byte{0x17, 0, 0, 0, 0, 1, 2, 3}})
	r, err := NewFLVReader(bytes.NewReader(b[:len(b)-7]))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := r.ReadTag(); !errors.Is(err, io.ErrUnexpectedEOF) {
		t.Errorf("truncated tag: %v, want unexpected EOF", err)
	}
}

func TestParseVideo(t *testing.T) {
	cases := []struct {
		name string
		data []byte
		want VideoTag
	}{
		{"AVCSequenceHeader", []byte{0x17, 0, 0, 0, 0, 1, 100}, VideoTag{"h264", true, true, 0, []byte{1, 100}, false, false, false}},
		{"AVCInterWithCTS", []byte{0x27, 1, 0, 0, 80, 9}, VideoTag{"h264", false, false, 80, []byte{9}, false, false, false}},
		{"AVCNegativeCTS", []byte{0x17, 1, 0xff, 0xff, 0xd8, 9}, VideoTag{"h264", true, false, -40, []byte{9}, false, false, false}},
		{"HEVCSequenceStart", []byte{0x90, 'h', 'v', 'c', '1', 1, 1}, VideoTag{"hevc", true, true, 0, []byte{1, 1}, false, false, false}},
		{"HEVCCodedFrames", []byte{0xa1, 'h', 'v', 'c', '1', 0, 0, 40, 7}, VideoTag{"hevc", false, false, 40, []byte{7}, false, false, false}},
		{"HEVCCodedFramesX", []byte{0x93, 'h', 'v', 'c', '1', 7}, VideoTag{"hevc", true, false, 0, []byte{7}, false, false, false}},
		{"AV1", []byte{0x91, 'a', 'v', '0', '1', 7}, VideoTag{"av1", true, false, 0, []byte{7}, false, false, false}},
		{"VP9", []byte{0xa1, 'v', 'p', '0', '9', 7}, VideoTag{"vp9", false, false, 0, []byte{7}, false, false, false}},
		{"HEVCLegacy", []byte{0x2c, 1, 0, 0, 40, 7}, VideoTag{"hevc", false, false, 40, []byte{7}, false, false, true}},
		{"HEVCLegacySequenceHeader", []byte{0x1c, 0, 0, 0, 0, 1, 1}, VideoTag{"hevc", true, true, 0, []byte{1, 1}, false, false, true}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ParseVideo(tc.data)
			if err != nil {
				t.Fatal(err)
			}
			if got.Codec != tc.want.Codec || got.Key != tc.want.Key || got.SequenceHeader != tc.want.SequenceHeader ||
				got.CTS != tc.want.CTS || !bytes.Equal(got.Payload, tc.want.Payload) || got.Legacy != tc.want.Legacy {
				t.Errorf("got %+v, want %+v", got, tc.want)
			}
		})
	}

	for _, bad := range [][]byte{nil, {0x17, 0}, {0x90, 'h', 'v'}, {0x90, 'x', 'x', 'x', 'x'}, {0x13, 0}} {
		if _, err := ParseVideo(bad); err == nil {
			t.Errorf("no error for %x", bad)
		}
	}
}

func TestParseAudio(t *testing.T) {
	got, err := ParseAudio([]byte{0xaf, 0, 0x12, 0x10})
	if err != nil || got.Codec != "aac" || !got.SequenceHeader || !bytes.Equal(got.Payload, []byte{0x12, 0x10}) {
		t.Errorf("AAC sequence header: %+v, %v", got, err)
	}
	got, err = ParseAudio([]byte{0xaf, 1, 9})
	if err != nil || got.Codec != "aac" || got.SequenceHeader || !bytes.Equal(got.Payload, []byte{9}) {
		t.Errorf("AAC raw: %+v, %v", got, err)
	}
	got, err = ParseAudio([]byte{0x2f, 0xff, 0xfb})
	if err != nil || got.Codec != "mp3" || got.SequenceHeader || !bytes.Equal(got.Payload, []byte{0xff, 0xfb}) {
		t.Errorf("MP3: %+v, %v", got, err)
	}
	for _, bad := range [][]byte{nil, {0xaf}, {0x0f, 1}} {
		if _, err := ParseAudio(bad); err == nil {
			t.Errorf("no error for %x", bad)
		}
	}
}

func TestParseVideoEndOfSequence(t *testing.T) {
	for _, data := range [][]byte{{0x17, 2, 0, 0, 0}, {0x92, 'h', 'v', 'c', '1'}} {
		v, err := ParseVideo(data)
		if err != nil || !v.EndOfSequence || v.SequenceHeader {
			t.Errorf("%x: %+v, %v", data, v, err)
		}
	}
	if v, _ := ParseVideo([]byte{0x27, 1, 0, 0, 0, 9}); v.EndOfSequence {
		t.Errorf("a frame is an end of sequence")
	}
}

func TestProbeFLVSkipsTheEndOfSequence(t *testing.T) {
	file := flvBytes(1,
		FLVTag{TagVideo, 0, []byte{0x17, 0, 0, 0, 0, 1, 66, 0, 30}},
		FLVTag{TagVideo, 0, []byte{0x17, 1, 0, 0, 0, 9}},
		FLVTag{TagVideo, 40, []byte{0x27, 1, 0, 0, 0, 9}},
		FLVTag{TagVideo, 40, []byte{0x17, 2, 0, 0, 0}},
	)
	info, err := probeFLV(bytes.NewReader(file))
	if err != nil {
		t.Fatal(err)
	}
	if v := info.Video; v.Frames != 2 || len(v.Keyframes) != 1 || v.Profile != "Baseline" || v.BFrames {
		t.Errorf("video %+v", v)
	}
}

func TestParseVideoMetadata(t *testing.T) {
	v, err := ParseVideo([]byte{0x94, 'h', 'v', 'c', '1', 2, 0, 9})
	if err != nil || !v.Metadata || v.SequenceHeader || v.EndOfSequence {
		t.Errorf("%+v, %v", v, err)
	}

	file := flvBytes(1,
		FLVTag{TagVideo, 0, []byte{0x90, 'h', 'v', 'c', '1', 1, 1}},
		FLVTag{TagVideo, 0, []byte{0x94, 'h', 'v', 'c', '1', 2, 0, 9}},
		FLVTag{TagVideo, 0, []byte{0x93, 'h', 'v', 'c', '1', 7}},
	)
	info, err := probeFLV(bytes.NewReader(file))
	if err != nil || info.Video.Frames != 1 {
		t.Errorf("metadata counted as a frame: %+v, %v", info.Video, err)
	}
}
