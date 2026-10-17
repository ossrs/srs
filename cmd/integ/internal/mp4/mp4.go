// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package mp4 reads fragmented MP4: init segments, and the moof and mdat
// boxes of fragments, as HLS fmp4 and DASH serve them. Its analyzer adds the
// samples to the stream report, and AnalyzeFile reads a whole file,
// progressive (with stts and the other sample tables) or fragmented.
package mp4

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"sort"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// Defaults are a track's sample defaults from its trex box.
type Defaults struct {
	Duration, Size, Flags uint32
}

// An Init is an init segment: its tracks, with empty sample tables, and
// their sample defaults by track id.
type Init struct {
	Tracks   []*media.MP4Track
	Defaults map[uint32]Defaults
}

// track returns the track of an id, or nil.
func (i *Init) track(id uint32) *media.MP4Track {
	for _, t := range i.Tracks {
		if t.ID == id {
			return t
		}
	}
	return nil
}

// A Sample is one sample of a fragment or a file.
type Sample struct {
	Track *media.MP4Track
	// DTS is the decode time and CTS the composition offset, in the track's
	// timescale; Duration too.
	DTS      uint64
	CTS      int32
	Duration uint32
	// Key is set when the sample flags say it is a sync sample.
	Key  bool
	Data []byte
	// Senc is set when the sample's traf has a senc box, and Subsamples are
	// the sample's subsamples from it, none when it is encrypted whole.
	Senc       bool
	Subsamples []Subsample
}

// A Subsample is a run of an encrypted sample: clear bytes, then protected
// ones.
type Subsample struct {
	Clear, Protected uint32
}

// Time is the decode time of the sample.
func (s Sample) Time() time.Duration {
	ts := uint64(s.Track.Timescale)
	if ts == 0 {
		return 0
	}
	return time.Duration(s.DTS/ts)*time.Second + time.Duration(s.DTS%ts)*time.Second/time.Duration(ts)
}

// A boxAt is one MP4 box: its type, its start in the buffer, and its body.
type boxAt struct {
	typ   string
	start int
	body  []byte
}

// boxes splits b into boxes, each at its offset from base. A size of 0 runs
// to the end; a size of 1 has a 64-bit size.
func boxes(b []byte, base int) ([]boxAt, error) {
	var out []boxAt
	for pos := 0; pos < len(b); {
		if len(b)-pos < 8 {
			return nil, fmt.Errorf("cut box header at %v", base+pos)
		}
		size := uint64(binary.BigEndian.Uint32(b[pos:]))
		typ := string(b[pos+4 : pos+8])
		head := 8
		switch size {
		case 0:
			size = uint64(len(b) - pos)
		case 1:
			if len(b)-pos < 16 {
				return nil, fmt.Errorf("cut %v box header at %v", typ, base+pos)
			}
			size, head = binary.BigEndian.Uint64(b[pos+8:]), 16
		}
		if size < uint64(head) || size > uint64(len(b)-pos) {
			return nil, fmt.Errorf("%v box of %v bytes at %v, past the end %v", typ, size, base+pos, base+len(b))
		}
		out = append(out, boxAt{typ, base + pos, b[pos+head : pos+int(size)]})
		pos += int(size)
	}
	return out, nil
}

// fields reads big-endian fields and records the first short read.
type fields struct {
	b   []byte
	err error
}

func (f *fields) take(n int) []byte {
	if f.err != nil || len(f.b) < n {
		if f.err == nil {
			f.err = errors.New("short box")
		}
		f.b = nil
		return make([]byte, n)
	}
	v := f.b[:n]
	f.b = f.b[n:]
	return v
}

func (f *fields) u16() uint16 { return binary.BigEndian.Uint16(f.take(2)) }
func (f *fields) u32() uint32 { return binary.BigEndian.Uint32(f.take(4)) }
func (f *fields) u64() uint64 { return binary.BigEndian.Uint64(f.take(8)) }

// fullBox reads the version and flags of a full box.
func fullBox(b []byte) (version byte, flags uint32, f *fields) {
	f = &fields{b: b}
	vf := f.take(4)
	return vf[0], uint32(vf[1])<<16 | uint32(vf[2])<<8 | uint32(vf[3]), f
}

// ParseInit parses an init segment: the tracks of its moov, and the sample
// defaults of its mvex.
func ParseInit(b []byte) (*Init, error) {
	f, err := media.ReadMP4(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		return nil, fmt.Errorf("init: %w", err)
	}
	init := &Init{Tracks: f.Tracks, Defaults: map[uint32]Defaults{}}

	top, err := boxes(b, 0)
	if err != nil {
		return nil, fmt.Errorf("init: %w", err)
	}
	for _, t := range top {
		if t.typ != "moov" {
			continue
		}
		moov, err := boxes(t.body, 0)
		if err != nil {
			return nil, fmt.Errorf("moov: %w", err)
		}
		for _, m := range moov {
			if m.typ != "mvex" {
				continue
			}
			mvex, err := boxes(m.body, 0)
			if err != nil {
				return nil, fmt.Errorf("mvex: %w", err)
			}
			for _, x := range mvex {
				if x.typ != "trex" {
					continue
				}
				_, _, f := fullBox(x.body)
				id := f.u32()
				f.u32() // sample description index
				d := Defaults{Duration: f.u32(), Size: f.u32(), Flags: f.u32()}
				if f.err != nil {
					return nil, fmt.Errorf("trex: %w", f.err)
				}
				init.Defaults[id] = d
			}
		}
	}
	return init, nil
}

// The tfhd and trun flags.
const (
	tfhdBaseDataOffset   = 0x000001
	tfhdDescriptionIndex = 0x000002
	tfhdDuration         = 0x000008
	tfhdSize             = 0x000010
	tfhdFlags            = 0x000020
	tfhdBaseIsMoof       = 0x020000

	trunDataOffset = 0x000001
	trunFirstFlags = 0x000004
	trunDuration   = 0x000100
	trunSize       = 0x000200
	trunFlags      = 0x000400
	trunCTS        = 0x000800

	// sampleNonSync is the sample_is_non_sync_sample bit of the sample flags.
	sampleNonSync = 0x00010000
)

// ParseFragments parses every moof of a segment, with the samples in the
// segment's mdat boxes, sorted by decode time across the tracks. The data
// offsets are from the start of b, so b is a whole segment or file.
func ParseFragments(init *Init, b []byte) ([]Sample, error) {
	top, err := boxes(b, 0)
	if err != nil {
		return nil, err
	}
	var samples []Sample
	for _, t := range top {
		if t.typ != "moof" {
			continue
		}
		s, err := parseMoof(init, b, t)
		if err != nil {
			return nil, fmt.Errorf("moof at %v: %w", t.start, err)
		}
		samples = append(samples, s...)
	}
	sortByTime(samples)
	return samples, nil
}

// sortByTime sorts samples by decode time, keeping the order of samples at
// the same time.
func sortByTime(samples []Sample) {
	sort.SliceStable(samples, func(i, j int) bool { return samples[i].Time() < samples[j].Time() })
}

func parseMoof(init *Init, file []byte, moof boxAt) ([]Sample, error) {
	children, err := boxes(moof.body, moof.start+8)
	if err != nil {
		return nil, err
	}
	var samples []Sample
	// Without a base offset or default-base-is-moof, the first traf's data
	// starts at the moof, and the next one's after the data before it.
	next := int64(moof.start)
	for _, c := range children {
		if c.typ != "traf" {
			continue
		}
		s, end, err := parseTraf(init, file, moof, c, next)
		if err != nil {
			return nil, err
		}
		samples = append(samples, s...)
		next = end
	}
	return samples, nil
}

// parseTraf reads the samples of a traf, and returns where its data ends.
func parseTraf(init *Init, file []byte, moof, traf boxAt, implicit int64) ([]Sample, int64, error) {
	children, err := boxes(traf.body, traf.start+8)
	if err != nil {
		return nil, 0, err
	}
	var tfhd []byte
	var baseTime uint64
	var truns [][]byte
	var senc []byte
	for _, c := range children {
		switch c.typ {
		case "tfhd":
			tfhd = c.body
		case "tfdt":
			v, _, f := fullBox(c.body)
			if v == 1 {
				baseTime = f.u64()
			} else {
				baseTime = uint64(f.u32())
			}
			if f.err != nil {
				return nil, 0, fmt.Errorf("tfdt: %w", f.err)
			}
		case "trun":
			truns = append(truns, c.body)
		case "senc":
			senc = c.body
		}
	}
	if tfhd == nil {
		return nil, 0, errors.New("traf without tfhd")
	}

	_, flags, f := fullBox(tfhd)
	id := f.u32()
	track := init.track(id)
	if track == nil {
		return nil, 0, fmt.Errorf("tfhd of track %v, not in the init", id)
	}
	d := init.Defaults[id]
	base := implicit
	if flags&tfhdBaseDataOffset != 0 {
		base = int64(f.u64())
	} else if flags&tfhdBaseIsMoof != 0 {
		base = int64(moof.start)
	}
	if flags&tfhdDescriptionIndex != 0 {
		f.u32()
	}
	if flags&tfhdDuration != 0 {
		d.Duration = f.u32()
	}
	if flags&tfhdSize != 0 {
		d.Size = f.u32()
	}
	if flags&tfhdFlags != 0 {
		d.Flags = f.u32()
	}
	if f.err != nil {
		return nil, 0, fmt.Errorf("tfhd: %w", f.err)
	}

	var samples []Sample
	dts := baseTime
	pos := base
	for i, trun := range truns {
		version, flags, f := fullBox(trun)
		count := f.u32()
		if flags&trunDataOffset != 0 {
			pos = base + int64(int32(f.u32()))
		} else if i == 0 {
			pos = base
		}
		firstFlags, hasFirst := uint32(0), flags&trunFirstFlags != 0
		if hasFirst {
			firstFlags = f.u32()
		}
		for k := uint32(0); k < count; k++ {
			s := Sample{Track: track, DTS: dts, Duration: d.Duration}
			size, sflags := d.Size, d.Flags
			if flags&trunDuration != 0 {
				s.Duration = f.u32()
			}
			if flags&trunSize != 0 {
				size = f.u32()
			}
			if flags&trunFlags != 0 {
				sflags = f.u32()
			} else if k == 0 && hasFirst {
				sflags = firstFlags
			}
			if flags&trunCTS != 0 {
				if version == 0 {
					s.CTS = int32(min(f.u32(), 1<<31-1))
				} else {
					s.CTS = int32(f.u32())
				}
			}
			if f.err != nil {
				return nil, 0, fmt.Errorf("trun of track %v: sample %v of %v: %w", id, k, count, f.err)
			}
			if pos < 0 || pos+int64(size) > int64(len(file)) {
				return nil, 0, fmt.Errorf("track %v sample %v at %v of %v bytes is past the end %v", id, k, pos, size, len(file))
			}
			s.Key = sflags&sampleNonSync == 0
			s.Data = file[pos : pos+int64(size)]
			samples = append(samples, s)
			pos += int64(size)
			dts += uint64(s.Duration)
		}
	}
	if senc != nil {
		if err := parseSenc(senc, samples); err != nil {
			return nil, 0, fmt.Errorf("senc of track %v: %w", id, err)
		}
	}
	return samples, pos, nil
}

// parseSenc reads the senc box of a traf into its samples: the IV of each,
// which must be the track's constant one, and its subsamples.
func parseSenc(senc []byte, samples []Sample) error {
	_, flags, f := fullBox(senc)
	if count := f.u32(); f.err == nil && int(count) != len(samples) {
		return fmt.Errorf("%v samples, the trun has %v", count, len(samples))
	}
	for i := range samples {
		s := &samples[i]
		if p := s.Track.Protection; p == nil || p.PerSampleIVSize != 0 {
			return errors.New("a sample IV, or a track not protected")
		}
		s.Senc = true
		if flags&0x2 == 0 {
			continue
		}
		for n := f.u16(); n > 0 && f.err == nil; n-- {
			s.Subsamples = append(s.Subsamples, Subsample{Clear: uint32(f.u16()), Protected: f.u32()})
		}
	}
	return f.err
}

// DecryptCBCS decrypts a sample of a cbcs track with key: the protected bytes
// of each subsample, or the whole sample when it has none, take the track's
// pattern, and the CBC chain starts at the constant IV for each. A partial
// block at the end is clear.
func DecryptCBCS(s Sample, key []byte) ([]byte, error) {
	p := s.Track.Protection
	if p == nil || p.Scheme != "cbcs" || len(p.ConstantIV) != aes.BlockSize {
		return nil, fmt.Errorf("the %v track is not cbcs with a constant IV", s.Track.Handler)
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	crypt, skip := int(p.CryptBlocks), int(p.SkipBlocks)
	if crypt == 0 {
		crypt, skip = 1, 0
	}
	out := bytes.Clone(s.Data)
	decrypt := func(b []byte) {
		cbc := cipher.NewCBCDecrypter(block, p.ConstantIV)
		for i := 0; i+aes.BlockSize <= len(b); i += (crypt + skip) * aes.BlockSize {
			n := min(crypt*aes.BlockSize, (len(b)-i)/aes.BlockSize*aes.BlockSize)
			cbc.CryptBlocks(b[i:i+n], b[i:i+n])
		}
	}
	if len(s.Subsamples) == 0 {
		decrypt(out)
		return out, nil
	}
	pos := 0
	for _, ss := range s.Subsamples {
		pos += int(ss.Clear)
		if pos+int(ss.Protected) > len(out) {
			return nil, fmt.Errorf("subsamples past the %v bytes of the sample", len(out))
		}
		decrypt(out[pos : pos+int(ss.Protected)])
		pos += int(ss.Protected)
	}
	if pos != len(out) {
		return nil, fmt.Errorf("subsamples of %v bytes, the sample has %v", pos, len(out))
	}
	return out, nil
}

// An Analyzer adds samples to its report. A track's sequence header is its
// codec configuration, counted when it first appears or changes.
type Analyzer struct {
	Report report.Report
	// configs are the last configurations, by handler.
	configs map[string]string
}

// Add adds samples in their order until the report meets u; the zero Until
// adds them all. It fails on a sample or a configuration it can't parse.
func (a *Analyzer) Add(samples []Sample, u report.Until) error {
	if a.configs == nil {
		a.configs = map[string]string{}
	}
	for _, s := range samples {
		if u != (report.Until{}) && u.Done(&a.Report) {
			return nil
		}
		var err error
		switch s.Track.Handler {
		case "vide":
			err = a.video(s)
		case "soun":
			err = a.audio(s)
		}
		if err != nil {
			return fmt.Errorf("track %v %v at %v: %w", s.Track.ID, s.Track.Codec, s.Time(), err)
		}
	}
	return nil
}

var videoCodecs = map[string]string{"avc1": "h264", "avc3": "h264", "hvc1": "hevc", "hev1": "hevc", "vp09": "vp9", "av01": "av1"}

func (a *Analyzer) video(s Sample) error {
	t := s.Track
	codec := videoCodecs[t.Codec]
	if codec == "" {
		return fmt.Errorf("unknown video sample entry %q", t.Codec)
	}
	if string(t.Config) != a.configs["vide"] {
		config := t.Config
		if codec == "vp9" && len(config) > 4 {
			config = config[4:] // vpcC is a full box.
		}
		w, h := int(t.Width), int(t.Height)
		if codec == "h264" || codec == "hevc" {
			var err error
			if w, h, err = media.VideoSize(codec, config); err != nil {
				return err
			}
		}
		a.configs["vide"] = string(t.Config)
		a.Report.VideoHeader(codec, media.Profile(codec, config), w, h)
	}

	key := s.Key
	if codec == "h264" || codec == "hevc" {
		var err error
		if key, err = irap(codec, t.Config, s.Data); err != nil {
			return err
		}
	}
	a.Report.AddVideo(codec, s.Time(), key)
	return nil
}

// irap reports whether a length-prefixed H.264 or HEVC access unit has an
// IDR or IRAP picture, with the NAL length size from its avcC or hvcC.
func irap(codec string, config, data []byte) (bool, error) {
	n := 4
	if codec == "h264" && len(config) > 4 {
		n = int(config[4]&0x03) + 1
	} else if codec == "hevc" && len(config) > 21 {
		n = int(config[21]&0x03) + 1
	}
	for len(data) > 0 {
		if len(data) < n {
			return false, errors.New("cut NAL length")
		}
		var size int
		for _, c := range data[:n] {
			size = size<<8 | int(c)
		}
		data = data[n:]
		if size > len(data) {
			return false, fmt.Errorf("NAL of %v bytes in %v", size, len(data))
		}
		if size > 0 {
			if codec == "h264" && data[0]&0x1f == 5 {
				return true, nil
			}
			if t := (data[0] >> 1) & 0x3f; codec == "hevc" && t >= 16 && t <= 21 {
				return true, nil
			}
		}
		data = data[size:]
	}
	return false, nil
}

func (a *Analyzer) audio(s Sample) error {
	t := s.Track
	codec, rate, channels := "", int(t.SampleRate), int(t.Channels)
	var hasHeader bool
	switch t.Codec {
	case "mp4a":
		oti, asc, err := esds(t.Config)
		if err != nil {
			return err
		}
		switch oti {
		case 0x40, 0x66, 0x67, 0x68:
			codec = "aac"
			if asc != nil {
				rate, channels = media.AACConfig(asc)
				hasHeader = true
			}
		case 0x69, 0x6b:
			codec = "mp3"
		default:
			return fmt.Errorf("unknown mp4a object type 0x%x", oti)
		}
	case "Opus":
		codec, hasHeader = "opus", true
	default:
		return fmt.Errorf("unknown audio sample entry %q", t.Codec)
	}

	if hasHeader && string(t.Config) != a.configs["soun"] {
		a.configs["soun"] = string(t.Config)
		a.Report.AudioHeader(codec, rate, channels)
	}
	a.Report.AddAudio(codec, s.Time())
	if r := a.Report.Audio; r.SampleRate == 0 {
		r.SampleRate, r.Channels = rate, channels
	}
	return nil
}

// esds reads the object type and the AudioSpecificConfig, if any, of an
// esds box body with its full box header.
func esds(b []byte) (oti byte, asc []byte, err error) {
	if len(b) < 4 {
		return 0, nil, errors.New("no esds")
	}
	b = b[4:]
	// descriptor reads a tag and its length of 1 to 4 bytes of 7 bits.
	descriptor := func(b []byte) (tag byte, body, rest []byte, err error) {
		if len(b) < 2 {
			return 0, nil, nil, errors.New("cut esds descriptor")
		}
		tag, b = b[0], b[1:]
		size := 0
		for i := 0; ; i++ {
			if i == 4 || len(b) == 0 {
				return 0, nil, nil, errors.New("bad esds descriptor length")
			}
			c := b[0]
			b = b[1:]
			size = size<<7 | int(c&0x7f)
			if c&0x80 == 0 {
				break
			}
		}
		if size > len(b) {
			return 0, nil, nil, fmt.Errorf("esds descriptor 0x%x of %v bytes in %v", tag, size, len(b))
		}
		return tag, b[:size], b[size:], nil
	}

	tag, es, _, err := descriptor(b)
	if err != nil {
		return 0, nil, err
	}
	if tag != 0x03 || len(es) < 3 {
		return 0, nil, fmt.Errorf("esds without an ES descriptor")
	}
	flags := es[2]
	es = es[3:]
	if flags&0x80 != 0 {
		es = es[min(2, len(es)):]
	}
	if flags&0x40 != 0 && len(es) > 0 {
		es = es[min(1+int(es[0]), len(es)):]
	}
	if flags&0x20 != 0 {
		es = es[min(2, len(es)):]
	}
	tag, dc, _, err := descriptor(es)
	if err != nil {
		return 0, nil, err
	}
	if tag != 0x04 || len(dc) < 13 {
		return 0, nil, fmt.Errorf("esds without a decoder config descriptor")
	}
	oti = dc[0]
	if len(dc) > 13 {
		if tag, info, _, err := descriptor(dc[13:]); err == nil && tag == 0x05 {
			asc = info
		}
	}
	return oti, asc, nil
}

// AnalyzeFile reads an MP4 file until the report meets u, or, for the zero
// Until, to its end: a fragmented file by its moof boxes, a progressive one
// by its sample tables.
func AnalyzeFile(path string, u report.Until) (*report.Report, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	init, err := ParseInit(b)
	if err != nil {
		return nil, fmt.Errorf("%v: %w", path, err)
	}

	var samples []Sample
	for _, t := range init.Tracks {
		for _, s := range t.Samples {
			if s.Offset < 0 || s.Offset+int64(s.Size) > int64(len(b)) {
				return nil, fmt.Errorf("%v: track %v sample at %v is past the end", path, t.ID, s.Offset)
			}
			samples = append(samples, Sample{
				Track: t, DTS: s.DTS, CTS: int32(s.CTS), Key: s.Key, Data: b[s.Offset : s.Offset+int64(s.Size)],
			})
		}
	}
	sortByTime(samples)
	fragments, err := ParseFragments(init, b)
	if err != nil {
		return nil, fmt.Errorf("%v: %w", path, err)
	}
	samples = append(samples, fragments...)

	var a Analyzer
	r := &a.Report
	if err := a.Add(samples, u); err != nil {
		return r, err
	}
	if u != (report.Until{}) && !u.Done(r) {
		return r, fmt.Errorf("file ended before %v: %v", u, r)
	}
	return r, nil
}
