// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"bytes"
	"encoding/binary"
	"testing"
)

// box builds an MP4 box from its type and body parts.
func box(typ string, parts ...[]byte) []byte {
	body := bytes.Join(parts, nil)
	b := binary.BigEndian.AppendUint32(nil, uint32(8+len(body)))
	return append(append(b, typ...), body...)
}

// u32 encodes values as big-endian uint32s.
func u32(v ...uint32) []byte {
	var b []byte
	for _, x := range v {
		b = binary.BigEndian.AppendUint32(b, x)
	}
	return b
}

func u16(v ...uint16) []byte {
	var b []byte
	for _, x := range v {
		b = binary.BigEndian.AppendUint16(b, x)
	}
	return b
}

// fullBox is a box with version 0 and no flags.
func fullBox(typ string, parts ...[]byte) []byte {
	return box(typ, append([][]byte{u32(0)}, parts...)...)
}

func mdhd(timescale uint32) []byte {
	return fullBox("mdhd", u32(0, 0, timescale, 0), u16(0, 0))
}

func hdlr(handler string) []byte {
	return fullBox("hdlr", u32(0), []byte(handler), make([]byte, 12), []byte{0})
}

// avc1 is a visual sample entry with an avcC child.
func avc1(width, height uint16, avcC []byte) []byte {
	head := make([]byte, 78)
	binary.BigEndian.PutUint16(head[6:], 1) // data reference index
	binary.BigEndian.PutUint16(head[24:], width)
	binary.BigEndian.PutUint16(head[26:], height)
	return box("avc1", head, box("avcC", avcC))
}

// mp4a is an audio sample entry.
func mp4a(channels uint16, rate uint32) []byte {
	head := make([]byte, 28)
	binary.BigEndian.PutUint16(head[6:], 1)
	binary.BigEndian.PutUint16(head[16:], channels)
	binary.BigEndian.PutUint16(head[18:], 16)
	binary.BigEndian.PutUint32(head[24:], rate<<16)
	return box("mp4a", head)
}

func trak(id uint32, handler string, timescale uint32, entry []byte, tables ...[]byte) []byte {
	tkhd := fullBox("tkhd", u32(0, 0, id), make([]byte, 72))
	stbl := box("stbl", append([][]byte{fullBox("stsd", u32(1), entry)}, tables...)...)
	return box("trak", tkhd, box("mdia", mdhd(timescale), hdlr(handler), box("minf", box("dinf"), stbl)))
}

// testMP4 has a video track of 5 samples in two chunks, with B-frames and
// two sync samples, and an audio track of 3 samples in one chunk with a
// 64-bit chunk offset and no sync table.
func testMP4() (file []byte, data []byte) {
	data = []byte("v0v1v2v3v4a0a1a2")
	// The mdat body starts after ftyp (16) and the mdat header (8).
	const base = 16 + 8
	video := trak(1, "vide", 12800, avc1(768, 320, []byte{1, 100, 0, 30}),
		fullBox("stts", u32(1, 5, 512)),
		fullBox("ctts", u32(3, 3, 1024, 1, 0, 1, 512)),
		fullBox("stss", u32(2, 1, 4)),
		fullBox("stsz", u32(0, 5, 2, 2, 2, 2, 2)),
		fullBox("stsc", u32(2, 1, 3, 1, 2, 2, 1)),
		fullBox("stco", u32(2, base, base+6)),
	)
	audio := trak(2, "soun", 44100, mp4a(2, 44100),
		fullBox("stts", u32(2, 2, 1024, 1, 1000)),
		fullBox("stsz", u32(2, 3)),
		fullBox("stsc", u32(1, 1, 3, 1)),
		fullBox("co64", u32(1, 0, base+10)),
	)
	ftyp := box("ftyp", []byte("isom"), u32(512))
	return bytes.Join([][]byte{ftyp, box("mdat", data), box("moov", fullBox("mvhd", make([]byte, 96)), video, audio)}, nil), data
}

func TestReadMP4Tracks(t *testing.T) {
	file, _ := testMP4()
	f, err := ReadMP4(bytes.NewReader(file), int64(len(file)))
	if err != nil {
		t.Fatal(err)
	}
	if len(f.Tracks) != 2 {
		t.Fatalf("%v tracks", len(f.Tracks))
	}

	v := f.Track("vide")
	if v == nil || v.ID != 1 || v.Codec != "avc1" || v.Timescale != 12800 || v.Width != 768 || v.Height != 320 {
		t.Fatalf("video track %+v", v)
	}
	if !bytes.Equal(v.Config, []byte{1, 100, 0, 30}) {
		t.Errorf("avcC %x", v.Config)
	}
	want := []MP4Sample{
		{24, 2, 0, 1024, true},
		{26, 2, 512, 1024, false},
		{28, 2, 1024, 1024, false},
		{30, 2, 1536, 0, true},
		{32, 2, 2048, 512, false},
	}
	if len(v.Samples) != len(want) {
		t.Fatalf("%v video samples", len(v.Samples))
	}
	for i, s := range v.Samples {
		if s != want[i] {
			t.Errorf("video sample %v: %+v, want %+v", i, s, want[i])
		}
	}

	a := f.Track("soun")
	if a == nil || a.Codec != "mp4a" || a.Channels != 2 || a.SampleRate != 44100 || len(a.Samples) != 3 {
		t.Fatalf("audio track %+v", a)
	}
	wantA := []MP4Sample{{34, 2, 0, 0, true}, {36, 2, 1024, 0, true}, {38, 2, 2048, 0, true}}
	for i, s := range a.Samples {
		if s != wantA[i] {
			t.Errorf("audio sample %v: %+v, want %+v", i, s, wantA[i])
		}
	}

	if f.Track("text") != nil {
		t.Errorf("found a track that isn't there")
	}
}

func TestReadMP4Sample(t *testing.T) {
	file, _ := testMP4()
	f, err := ReadMP4(bytes.NewReader(file), int64(len(file)))
	if err != nil {
		t.Fatal(err)
	}
	for i, wantData := range []string{"v0", "v1", "v2", "v3", "v4"} {
		got, err := f.ReadSample(f.Track("vide").Samples[i])
		if err != nil || string(got) != wantData {
			t.Errorf("video sample %v: %q, %v", i, got, err)
		}
	}
	got, err := f.ReadSample(f.Track("soun").Samples[2])
	if err != nil || string(got) != "a2" {
		t.Errorf("audio sample 2: %q, %v", got, err)
	}
	if _, err := f.ReadSample(MP4Sample{Offset: int64(len(file)), Size: 4}); err == nil {
		t.Errorf("no error for a sample past the end")
	}
}

func TestReadMP4Errors(t *testing.T) {
	file, _ := testMP4()
	for _, bad := range [][]byte{
		nil,                         // no moov
		box("ftyp", []byte("isom")), // no moov
		file[:len(file)-20],         // a truncated moov
		append(u32(4), "moov"...),   // a box smaller than its header
	} {
		if _, err := ReadMP4(bytes.NewReader(bad), int64(len(bad))); err == nil {
			t.Errorf("no error for %x", bad)
		}
	}
}
