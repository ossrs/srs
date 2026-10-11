// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
)

// An MP4File is the tracks of a progressive MP4 file and their samples.
type MP4File struct {
	r      io.ReaderAt
	size   int64
	Tracks []*MP4Track
}

// An MP4Track is one track of an MP4 file.
type MP4Track struct {
	ID uint32
	// Handler is "vide" or "soun".
	Handler string
	// Codec is the sample entry type, such as "avc1", "hvc1", "vp09",
	// "av01", "mp4a" or "Opus".
	Codec     string
	Timescale uint32
	// Width and Height are set for video, Channels and SampleRate for audio.
	Width, Height uint16
	Channels      uint16
	SampleRate    uint32
	// Config is the codec configuration box body, such as avcC or dOps.
	Config  []byte
	Samples []MP4Sample
}

// An MP4Sample is one sample: where it is, its decode time and composition
// offset in the track's timescale, and whether it is a sync sample.
type MP4Sample struct {
	Offset int64
	Size   uint32
	DTS    uint64
	CTS    int64
	Key    bool
}

// A box4 is one MP4 box: its type and body.
type box4 struct {
	typ  string
	body []byte
}

// ReadMP4 reads the moov box of a progressive MP4 file.
func ReadMP4(r io.ReaderAt, size int64) (*MP4File, error) {
	moov, err := findTopBox(r, size, "moov")
	if err != nil {
		return nil, err
	}
	boxes, err := parseBoxes(moov)
	if err != nil {
		return nil, fmt.Errorf("moov: %w", err)
	}
	f := &MP4File{r: r, size: size}
	for _, b := range boxes {
		if b.typ != "trak" {
			continue
		}
		t, err := parseTrak(b.body)
		if err != nil {
			return nil, fmt.Errorf("trak: %w", err)
		}
		f.Tracks = append(f.Tracks, t)
	}
	if len(f.Tracks) == 0 {
		return nil, errors.New("moov has no track")
	}
	return f, nil
}

// findTopBox reads the body of the first top-level box of a type, reading
// only box headers before it.
func findTopBox(r io.ReaderAt, size int64, typ string) ([]byte, error) {
	for pos := int64(0); pos+8 <= size; {
		var h [16]byte
		if _, err := r.ReadAt(h[:8], pos); err != nil {
			return nil, fmt.Errorf("read box header at %v: %w", pos, err)
		}
		boxSize, head := int64(binary.BigEndian.Uint32(h[:4])), int64(8)
		switch boxSize {
		case 0:
			boxSize = size - pos
		case 1:
			if _, err := r.ReadAt(h[8:16], pos+8); err != nil {
				return nil, fmt.Errorf("read box size at %v: %w", pos, err)
			}
			boxSize, head = int64(binary.BigEndian.Uint64(h[8:16])), 16
		}
		if boxSize < head || pos+boxSize > size {
			return nil, fmt.Errorf("bad box %q of %v bytes at %v in %v", h[4:8], boxSize, pos, size)
		}
		if string(h[4:8]) == typ {
			body := make([]byte, boxSize-head)
			if _, err := r.ReadAt(body, pos+head); err != nil {
				return nil, fmt.Errorf("read %v: %w", typ, err)
			}
			return body, nil
		}
		pos += boxSize
	}
	return nil, fmt.Errorf("no %v box", typ)
}

// parseBoxes splits b into its boxes.
func parseBoxes(b []byte) ([]box4, error) {
	var boxes []box4
	for len(b) > 0 {
		if len(b) < 8 {
			return nil, fmt.Errorf("%v bytes left, too short for a box", len(b))
		}
		size, head := uint64(binary.BigEndian.Uint32(b)), uint64(8)
		typ := string(b[4:8])
		switch size {
		case 0:
			size = uint64(len(b))
		case 1:
			if len(b) < 16 {
				return nil, fmt.Errorf("box %q has no 64-bit size", typ)
			}
			size, head = binary.BigEndian.Uint64(b[8:]), 16
		}
		if size < head || size > uint64(len(b)) {
			return nil, fmt.Errorf("bad box %q of %v bytes in %v", typ, size, len(b))
		}
		boxes = append(boxes, box4{typ, b[head:size]})
		b = b[size:]
	}
	return boxes, nil
}

// child returns the body of the first child box at a path of types.
func child(b []byte, path ...string) ([]byte, error) {
	for _, typ := range path {
		boxes, err := parseBoxes(b)
		if err != nil {
			return nil, err
		}
		found := false
		for _, x := range boxes {
			if x.typ == typ {
				b, found = x.body, true
				break
			}
		}
		if !found {
			return nil, fmt.Errorf("no %v box", typ)
		}
	}
	return b, nil
}

// A reader of big-endian fields that records the first short read.
type fields struct {
	b   []byte
	err error
}

func (f *fields) take(n int) []byte {
	if f.err != nil || len(f.b) < n {
		if f.err == nil {
			f.err = io.ErrUnexpectedEOF
		}
		return make([]byte, n)
	}
	v := f.b[:n]
	f.b = f.b[n:]
	return v
}

func (f *fields) u16() uint16 { return binary.BigEndian.Uint16(f.take(2)) }
func (f *fields) u32() uint32 { return binary.BigEndian.Uint32(f.take(4)) }
func (f *fields) u64() uint64 { return binary.BigEndian.Uint64(f.take(8)) }

// fullBoxFields reads the version of a full box body and returns the fields after
// its flags.
func fullBoxFields(b []byte) (version byte, f *fields) {
	f = &fields{b: b}
	vf := f.take(4)
	return vf[0], f
}

func parseTrak(trak []byte) (*MP4Track, error) {
	t := &MP4Track{}

	tkhd, err := child(trak, "tkhd")
	if err != nil {
		return nil, err
	}
	v, f := fullBoxFields(tkhd)
	if v == 1 {
		f.take(16)
	} else {
		f.take(8)
	}
	t.ID = f.u32()
	if f.err != nil {
		return nil, fmt.Errorf("tkhd: %w", f.err)
	}

	mdia, err := child(trak, "mdia")
	if err != nil {
		return nil, err
	}
	mdhd, err := child(mdia, "mdhd")
	if err != nil {
		return nil, err
	}
	v, f = fullBoxFields(mdhd)
	if v == 1 {
		f.take(16)
	} else {
		f.take(8)
	}
	t.Timescale = f.u32()
	if f.err != nil {
		return nil, fmt.Errorf("mdhd: %w", f.err)
	}

	hdlr, err := child(mdia, "hdlr")
	if err != nil {
		return nil, err
	}
	_, f = fullBoxFields(hdlr)
	f.take(4)
	t.Handler = string(f.take(4))
	if f.err != nil {
		return nil, fmt.Errorf("hdlr: %w", f.err)
	}

	stbl, err := child(mdia, "minf", "stbl")
	if err != nil {
		return nil, err
	}
	if err := t.parseStsd(stbl); err != nil {
		return nil, err
	}
	if err := t.parseSamples(stbl); err != nil {
		return nil, err
	}
	return t, nil
}

// parseStsd reads the first sample entry: its codec, its size or audio
// format, and its configuration box.
func (t *MP4Track) parseStsd(stbl []byte) error {
	stsd, err := child(stbl, "stsd")
	if err != nil {
		return err
	}
	_, f := fullBoxFields(stsd)
	f.u32() // entry count
	entries, err := parseBoxes(f.b)
	if err != nil || len(entries) == 0 {
		return fmt.Errorf("stsd has no sample entry: %v", err)
	}
	entry := entries[0]
	t.Codec = entry.typ

	var head int
	e := &fields{b: entry.body}
	switch t.Handler {
	case "vide":
		e.take(24)
		t.Width, t.Height = e.u16(), e.u16()
		head = 78
	case "soun":
		e.take(16)
		t.Channels = e.u16()
		e.take(6)
		t.SampleRate = e.u32() >> 16
		head = 28
	default:
		return nil
	}
	if e.err != nil || len(entry.body) < head {
		return fmt.Errorf("short %v sample entry", entry.typ)
	}

	children, err := parseBoxes(entry.body[head:])
	if err != nil {
		return fmt.Errorf("%v: %w", entry.typ, err)
	}
	for _, c := range children {
		switch c.typ {
		case "avcC", "hvcC", "vpcC", "av1C", "dOps", "esds":
			t.Config = c.body
			return nil
		}
	}
	return nil
}

// parseSamples expands the sample tables into one entry per sample.
func (t *MP4Track) parseSamples(stbl []byte) error {
	// Sizes.
	stsz, err := child(stbl, "stsz")
	if err != nil {
		return err
	}
	_, f := fullBoxFields(stsz)
	fixed, count := f.u32(), int(f.u32())
	if f.err != nil {
		return fmt.Errorf("stsz: %w", f.err)
	}
	if fixed == 0 && len(f.b) < 4*count {
		return fmt.Errorf("stsz: %v sizes in %v bytes", count, len(f.b))
	}
	t.Samples = make([]MP4Sample, count)
	for i := range t.Samples {
		t.Samples[i].Key = true
		if t.Samples[i].Size = fixed; fixed == 0 {
			t.Samples[i].Size = f.u32()
		}
	}

	// Decode times.
	stts, err := child(stbl, "stts")
	if err != nil {
		return err
	}
	_, f = fullBoxFields(stts)
	var dts uint64
	i := 0
	for n := f.u32(); n > 0 && f.err == nil; n-- {
		run, delta := f.u32(), f.u32()
		for ; run > 0 && i < count; run-- {
			t.Samples[i].DTS = dts
			dts += uint64(delta)
			i++
		}
	}
	if f.err != nil || i != count {
		return fmt.Errorf("stts covers %v of %v samples: %v", i, count, f.err)
	}

	// Composition offsets, if any.
	if ctts, err := child(stbl, "ctts"); err == nil {
		v, f := fullBoxFields(ctts)
		i := 0
		for n := f.u32(); n > 0 && f.err == nil; n-- {
			run, off := f.u32(), f.u32()
			for ; run > 0 && i < count; run-- {
				if v == 1 {
					t.Samples[i].CTS = int64(int32(off))
				} else {
					t.Samples[i].CTS = int64(off)
				}
				i++
			}
		}
		if f.err != nil {
			return fmt.Errorf("ctts: %w", f.err)
		}
	}

	// Sync samples; without the table, every sample is one.
	if stss, err := child(stbl, "stss"); err == nil {
		_, f := fullBoxFields(stss)
		for i := range t.Samples {
			t.Samples[i].Key = false
		}
		for n := f.u32(); n > 0 && f.err == nil; n-- {
			if k := int(f.u32()); k >= 1 && k <= count {
				t.Samples[k-1].Key = true
			}
		}
		if f.err != nil {
			return fmt.Errorf("stss: %w", f.err)
		}
	}

	// Chunk offsets, 32 or 64 bits.
	var chunks []int64
	if stco, err := child(stbl, "stco"); err == nil {
		_, f := fullBoxFields(stco)
		for n := f.u32(); n > 0 && f.err == nil; n-- {
			chunks = append(chunks, int64(f.u32()))
		}
		if f.err != nil {
			return fmt.Errorf("stco: %w", f.err)
		}
	} else if co64, err := child(stbl, "co64"); err == nil {
		_, f := fullBoxFields(co64)
		for n := f.u32(); n > 0 && f.err == nil; n-- {
			chunks = append(chunks, int64(f.u64()))
		}
		if f.err != nil {
			return fmt.Errorf("co64: %w", f.err)
		}
	} else {
		return errors.New("no stco or co64 box")
	}

	// Samples per chunk: each entry holds from its first chunk to the next
	// entry's.
	stsc, err := child(stbl, "stsc")
	if err != nil {
		return err
	}
	_, f = fullBoxFields(stsc)
	type run struct{ first, perChunk uint32 }
	var runs []run
	for n := f.u32(); n > 0 && f.err == nil; n-- {
		first, per := f.u32(), f.u32()
		f.u32() // sample description index
		runs = append(runs, run{first, per})
	}
	if f.err != nil {
		return fmt.Errorf("stsc: %w", f.err)
	}
	i = 0
	for r, cur := range runs {
		last := uint32(len(chunks))
		if r+1 < len(runs) {
			last = runs[r+1].first - 1
		}
		for chunk := cur.first; chunk <= last && chunk >= 1 && int(chunk) <= len(chunks); chunk++ {
			off := chunks[chunk-1]
			for k := uint32(0); k < cur.perChunk && i < count; k++ {
				t.Samples[i].Offset = off
				off += int64(t.Samples[i].Size)
				i++
			}
		}
	}
	if i != count {
		return fmt.Errorf("the chunks hold %v of %v samples", i, count)
	}
	return nil
}

// ReadSample returns the bytes of a sample.
func (f *MP4File) ReadSample(s MP4Sample) ([]byte, error) {
	if s.Offset < 0 || s.Offset+int64(s.Size) > f.size {
		return nil, fmt.Errorf("sample at %v of %v bytes is past the end, %v", s.Offset, s.Size, f.size)
	}
	b := make([]byte, s.Size)
	if _, err := f.r.ReadAt(b, s.Offset); err != nil {
		return nil, err
	}
	return b, nil
}

// Track returns the first track of a handler, "vide" or "soun", or nil.
func (f *MP4File) Track(handler string) *MP4Track {
	for _, t := range f.Tracks {
		if t.Handler == handler {
			return t
		}
	}
	return nil
}
