// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package ts demuxes MPEG-TS (PAT, PMT and PES) and analyzes its H.264,
// HEVC, AAC, MP3 and Opus streams into the stream report, from an HTTP-TS
// player or a TS file.
package ts

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"time"

	"srs-integ/internal/audio"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
)

// PacketSize is the size of a TS packet.
const PacketSize = 188

// A PES is one PES packet of an elementary stream.
type PES struct {
	PID        uint16
	StreamType byte
	// Codec is "h264", "hevc", "aac", "mp3" or "opus", or "" for a stream
	// the analyzer doesn't read.
	Codec string
	// PTS and DTS are the presentation and decode times, unwrapped past the
	// 33-bit limit. DTS is PTS when the packet has no DTS.
	PTS, DTS time.Duration
	Data     []byte
}

// stream is an elementary stream of the PMT, and the PES it is collecting.
type stream struct {
	typ   byte
	codec string
	// buf is the PES so far; its length is the size the header gives, or 0.
	buf    []byte
	length int
	// last is the last 33-bit timestamp and wraps the times it wrapped.
	last  int64
	wraps int64
	seen  bool
}

// A Demuxer reads PES packets from a TS stream.
type Demuxer struct {
	r      io.Reader
	pmtPID int
	// streams are the elementary streams by PID, and order the PIDs in the
	// order of the PMT, to flush them in a fixed order at the end.
	streams map[uint16]*stream
	order   []uint16
	// last is the last packet of each elementary stream.
	last    map[uint16][]byte
	packets int64
	pending []*PES
	eof     bool
}

// NewDemuxer reads TS packets from r.
func NewDemuxer(r io.Reader) *Demuxer {
	return &Demuxer{r: r, pmtPID: -1, streams: map[uint16]*stream{}, last: map[uint16][]byte{}}
}

// Streams are the elementary streams of the last PMT, by PID.
func (d *Demuxer) Streams() map[uint16]string {
	m := map[uint16]string{}
	for pid, s := range d.streams {
		m[pid] = s.codec
	}
	return m
}

// ReadPES reads the next complete PES packet. A packet that gives its length
// is complete when that much has arrived; one without, as video usually is,
// when the next one of its PID starts, or at the end of the stream. It fails
// on a lost sync byte or a continuity counter that skips, which a TS over
// TCP never has, and returns io.EOF at the end.
func (d *Demuxer) ReadPES() (*PES, error) {
	p := make([]byte, PacketSize)
	for len(d.pending) == 0 {
		if d.eof {
			return nil, io.EOF
		}
		if _, err := io.ReadFull(d.r, p); err == io.EOF {
			// Flush the PES packets without a length.
			d.eof = true
			for _, pid := range d.order {
				if pes, err := d.finish(pid); err != nil {
					return nil, err
				} else if pes != nil {
					d.pending = append(d.pending, pes)
				}
			}
			continue
		} else if err != nil {
			return nil, err
		}
		if err := d.packet(p); err != nil {
			return nil, fmt.Errorf("TS packet %v: %w", d.packets, err)
		}
		d.packets++
	}
	pes := d.pending[0]
	d.pending = d.pending[1:]
	return pes, nil
}

// packet reads one TS packet.
func (d *Demuxer) packet(p []byte) error {
	if p[0] != 0x47 {
		return fmt.Errorf("lost sync, %#x instead of 0x47", p[0])
	}
	pusi := p[1]&0x40 != 0
	pid := uint16(p[1]&0x1f)<<8 | uint16(p[2])
	control, cc := (p[3]>>4)&0x03, p[3]&0x0f
	payload := p[4:]
	discontinuity := false
	if control&0x02 != 0 {
		n := int(payload[0])
		if n > 183 {
			return fmt.Errorf("adaptation field of %v bytes", n)
		}
		discontinuity = n > 0 && payload[1]&0x80 != 0
		payload = payload[1+n:]
	}
	if control&0x01 == 0 || pid == 0x1fff {
		return nil
	}

	switch {
	case pid == 0:
		return d.pat(pusi, payload)
	case int(pid) == d.pmtPID:
		return d.pmt(pusi, payload)
	}
	if _, ok := d.streams[pid]; !ok {
		return nil
	}

	// Only the elementary streams are checked: a lost packet breaks their
	// media, while muxers such as SRS write each PAT and PMT with the same
	// counter.
	if last, ok := d.last[pid]; ok && !discontinuity {
		if lastCC := last[3] & 0x0f; cc == lastCC && bytes.Equal(p, last) {
			// A duplicate packet, sent once more.
			return nil
		} else if cc != (lastCC+1)&0x0f {
			return fmt.Errorf("continuity counter of PID %#x skips from %v to %v", pid, lastCC, cc)
		}
	}
	d.last[pid] = append(d.last[pid][:0], p...)

	return d.pes(pid, pusi, payload)
}

// section returns the section of a PSI packet: from its table id to before
// its CRC. A section that spans packets isn't supported.
func section(pusi bool, b []byte, tableID byte) ([]byte, error) {
	if !pusi {
		return nil, errors.New("PSI continues from an earlier packet, which isn't supported")
	}
	if len(b) < 1 || int(b[0])+1 > len(b) {
		return nil, errors.New("bad PSI pointer field")
	}
	b = b[1+int(b[0]):]
	if len(b) < 3 || b[0] != tableID {
		return nil, fmt.Errorf("PSI table %#x, want %#x", b[0], tableID)
	}
	n := int(b[1]&0x0f)<<8 | int(b[2])
	if n < 9 || 3+n > len(b) {
		return nil, fmt.Errorf("PSI section of %v bytes doesn't fit the packet", n)
	}
	return b[:3+n-4], nil
}

func (d *Demuxer) pat(pusi bool, b []byte) error {
	s, err := section(pusi, b, 0)
	if err != nil {
		return fmt.Errorf("PAT: %w", err)
	}
	for p := s[8:]; len(p) >= 4; p = p[4:] {
		if program := uint16(p[0])<<8 | uint16(p[1]); program != 0 {
			d.pmtPID = int(p[2]&0x1f)<<8 | int(p[3])
			return nil
		}
	}
	return errors.New("PAT has no program")
}

func (d *Demuxer) pmt(pusi bool, b []byte) error {
	s, err := section(pusi, b, 2)
	if err != nil {
		return fmt.Errorf("PMT: %w", err)
	}
	if len(s) < 12 {
		return errors.New("short PMT")
	}
	info := int(s[10]&0x0f)<<8 | int(s[11])
	if 12+info > len(s) {
		return errors.New("PMT program info past its end")
	}
	for p := s[12+info:]; len(p) > 0; {
		if len(p) < 5 {
			return errors.New("short PMT stream entry")
		}
		typ, pid := p[0], uint16(p[1]&0x1f)<<8|uint16(p[2])
		n := int(p[3]&0x0f)<<8 | int(p[4])
		if 5+n > len(p) {
			return errors.New("PMT descriptors past its end")
		}
		codec := codecOf(typ, p[5:5+n])
		if s, ok := d.streams[pid]; !ok || s.typ != typ {
			if !ok {
				d.order = append(d.order, pid)
			}
			d.streams[pid] = &stream{typ: typ, codec: codec}
		}
		p = p[5+n:]
	}
	return nil
}

// codecOf is the codec of a stream type, with its descriptors for the
// private streams such as Opus.
func codecOf(typ byte, descriptors []byte) string {
	switch typ {
	case 0x1b:
		return "h264"
	case 0x24:
		return "hevc"
	case 0x0f:
		return "aac"
	case 0x03, 0x04:
		return "mp3"
	case 0x06:
		// A registration descriptor names it.
		for d := descriptors; len(d) >= 2 && 2+int(d[1]) <= len(d); d = d[2+int(d[1]):] {
			if d[0] == 0x05 && d[1] >= 4 && string(d[2:6]) == "Opus" {
				return "opus"
			}
		}
	}
	return ""
}

// pes adds a payload to the PES of a PID.
func (d *Demuxer) pes(pid uint16, pusi bool, b []byte) error {
	s := d.streams[pid]
	if pusi {
		if pes, err := d.finish(pid); err != nil {
			return err
		} else if pes != nil {
			d.pending = append(d.pending, pes)
		}
		if len(b) < 6 || b[0] != 0 || b[1] != 0 || b[2] != 1 {
			return fmt.Errorf("PID %#x: no PES start code", pid)
		}
		s.length = int(b[4])<<8 | int(b[5])
		if s.length > 0 {
			s.length += 6
		}
		s.buf = append([]byte(nil), b...)
	} else if s.buf != nil {
		s.buf = append(s.buf, b...)
	} else {
		// The stream started in the middle of a PES.
		return nil
	}

	if s.length > 0 && len(s.buf) >= s.length {
		pes, err := d.finish(pid)
		if err != nil {
			return err
		}
		d.pending = append(d.pending, pes)
	}
	return nil
}

// finish parses the PES collected for a PID, if any.
func (d *Demuxer) finish(pid uint16) (*PES, error) {
	s := d.streams[pid]
	b := s.buf
	if b == nil {
		return nil, nil
	}
	s.buf = nil
	if s.length > 0 {
		if len(b) < s.length {
			return nil, fmt.Errorf("PID %#x: PES of %v bytes cut at %v", pid, s.length, len(b))
		}
		b = b[:s.length]
	}
	if len(b) < 9 || len(b) < 9+int(b[8]) {
		return nil, fmt.Errorf("PID %#x: short PES header", pid)
	}
	flags, opt := b[7]>>6, b[9:9+int(b[8])]
	pes := &PES{PID: pid, StreamType: s.typ, Codec: s.codec, Data: b[9+int(b[8]):]}
	if flags&0x02 == 0 || len(opt) < 5 {
		return nil, fmt.Errorf("PID %#x: PES without a PTS", pid)
	}
	pts := s.unwrap(readTimestamp(opt))
	dts := pts
	if flags == 0x03 {
		if len(opt) < 10 {
			return nil, fmt.Errorf("PID %#x: PES DTS cut", pid)
		}
		dts = s.unwrap(readTimestamp(opt[5:]))
	}
	pes.PTS, pes.DTS = ticks(pts), ticks(dts)
	return pes, nil
}

// readTimestamp reads a 33-bit PTS or DTS.
func readTimestamp(b []byte) int64 {
	return int64(b[0]>>1&0x07)<<30 | int64(b[1])<<22 | int64(b[2]>>1)<<15 | int64(b[3])<<7 | int64(b[4]>>1)
}

// unwrap makes a 33-bit timestamp go on past the limit: a step back of more
// than half the range is a wrap.
func (s *stream) unwrap(ts int64) int64 {
	const wrap = int64(1) << 33
	if s.seen {
		if s.last-ts > wrap/2 {
			s.wraps++
		} else if ts-s.last > wrap/2 && s.wraps > 0 {
			s.wraps--
		}
	}
	s.last, s.seen = ts, true
	return ts + s.wraps*wrap
}

// ticks converts 90 kHz ticks to a duration.
func ticks(t int64) time.Duration {
	return time.Duration(t) * time.Second / 90000
}

// SplitNALs splits an Annex B byte stream into its NAL units, without their
// start codes.
func SplitNALs(b []byte) [][]byte {
	var nals [][]byte
	start := -1
	for i := 0; i+2 < len(b); {
		if b[i] == 0 && b[i+1] == 0 && b[i+2] == 1 {
			if start >= 0 {
				// A 4-byte start code leaves its zero on the NAL before.
				end := i
				for end > start && b[end-1] == 0 {
					end--
				}
				nals = append(nals, b[start:end])
			}
			i += 3
			start = i
			continue
		}
		i++
	}
	if start >= 0 && start < len(b) {
		nals = append(nals, b[start:])
	}
	return nals
}

// OpusPackets splits the payload of an Opus PES into its Opus packets, each
// after its control header.
func OpusPackets(b []byte) ([][]byte, error) {
	var packets [][]byte
	for len(b) > 0 {
		if len(b) < 2 || b[0] != 0x7f || b[1]&0xe0 != 0xe0 {
			return packets, errors.New("no Opus control header")
		}
		flags := b[1]
		b = b[2:]
		size := 0
		for {
			if len(b) == 0 {
				return packets, errors.New("Opus control header without a size")
			}
			v := b[0]
			b = b[1:]
			size += int(v)
			if v != 0xff {
				break
			}
		}
		if flags&0x10 != 0 {
			b = b[min(2, len(b)):]
		}
		if flags&0x08 != 0 {
			b = b[min(2, len(b)):]
		}
		if flags&0x04 != 0 {
			if len(b) == 0 || 1+int(b[0]) > len(b) {
				return packets, errors.New("Opus control extension past the end")
			}
			b = b[1+int(b[0]):]
		}
		if size > len(b) {
			return packets, fmt.Errorf("Opus packet of %v bytes cut at %v", size, len(b))
		}
		packets = append(packets, b[:size])
		b = b[size:]
	}
	return packets, nil
}

// OpusSamples is the number of samples at 48 kHz in an Opus packet, from its
// TOC byte.
func OpusSamples(packet []byte) (int, error) {
	if len(packet) == 0 {
		return 0, errors.New("empty Opus packet")
	}
	config := int(packet[0] >> 3)
	// The frame size in samples at 48 kHz: SILK, hybrid, then CELT.
	var frame int
	switch {
	case config < 12:
		frame = []int{480, 960, 1920, 2880}[config%4]
	case config < 16:
		frame = []int{480, 960}[config%2]
	default:
		frame = []int{120, 240, 480, 960}[config%4]
	}
	switch packet[0] & 0x03 {
	case 0:
		return frame, nil
	case 1, 2:
		return 2 * frame, nil
	}
	if len(packet) < 2 {
		return 0, errors.New("Opus packet without its frame count")
	}
	return int(packet[1]&0x3f) * frame, nil
}

// An Analyzer adds PES packets to its report.
type Analyzer struct {
	Report report.Report
	// config is the last parameter sets of the video, which TS repeats.
	config []byte
	audio  audio.Track
}

// Add adds one PES packet to the report. It fails on a packet it can't
// parse, such as a broken SPS or a cut audio frame.
func (a *Analyzer) Add(p *PES) error {
	var err error
	switch p.Codec {
	case "h264", "hevc":
		err = a.video(p)
	case "aac", "mp3":
		err = a.audioFrames(p)
	case "opus":
		err = a.opus(p)
	}
	if err != nil {
		return fmt.Errorf("%v at %v: %w", p.Codec, p.DTS, err)
	}
	return nil
}

// video adds an access unit. TS repeats the parameter sets before each
// keyframe, so the report counts a sequence header when they first appear or
// change.
func (a *Analyzer) video(p *PES) error {
	var sps, config []byte
	key, frame := false, false
	for _, nal := range SplitNALs(p.Data) {
		if len(nal) == 0 {
			continue
		}
		if p.Codec == "h264" {
			switch t := nal[0] & 0x1f; {
			case t == 7:
				sps = nal
				config = append(config, nal...)
			case t == 8:
				config = append(config, nal...)
			case t == 5:
				key, frame = true, true
			case t >= 1 && t <= 4:
				frame = true
			}
			continue
		}
		switch t := (nal[0] >> 1) & 0x3f; {
		case t == 33:
			sps = nal
			config = append(config, nal...)
		case t == 32 || t == 34:
			config = append(config, nal...)
		case t >= 16 && t <= 21:
			key, frame = true, true
		case t < 32:
			frame = true
		}
	}

	if sps != nil && !bytes.Equal(config, a.config) {
		w, h, err := media.SPSSize(p.Codec, sps)
		if err != nil {
			return err
		}
		profile := media.Profile(p.Codec, sps)
		if p.Codec == "hevc" {
			// After the 2-byte NAL header, like the hvcC.
			profile = media.Profile(p.Codec, sps[2:])
		}
		a.config = config
		a.Report.VideoHeader(p.Codec, profile, w, h)
	}
	if frame {
		a.Report.AddVideo(p.Codec, p.DTS, key)
	}
	return nil
}

// audioFrames adds the ADTS or MP3 frames of a PES, each after the samples
// of the frames before it.
func (a *Analyzer) audioFrames(p *PES) error {
	frames, err := audio.SplitFrames(p.Codec, p.Data)
	at := p.DTS
	for _, f := range frames {
		a.audio.Add(&a.Report, f, at)
		at += time.Duration(f.Samples) * time.Second / time.Duration(f.SampleRate)
	}
	return err
}

// opus adds the Opus packets of a PES. TS has no Opus sequence header; every
// packet is 48 kHz, and its TOC tells mono or stereo.
func (a *Analyzer) opus(p *PES) error {
	packets, err := OpusPackets(p.Data)
	at := p.DTS
	for _, packet := range packets {
		n, perr := OpusSamples(packet)
		if perr != nil {
			return perr
		}
		r := &a.Report
		r.AddAudio("opus", at)
		if r.Audio.SampleRate == 0 {
			r.Audio.SampleRate, r.Audio.Channels = 48000, 1
			if packet[0]&0x04 != 0 {
				r.Audio.Channels = 2
			}
		}
		at += time.Duration(n) * time.Second / 48000
	}
	return err
}

// Analyze reads PES packets from d until the report meets u, or, for the
// zero Until, until the stream ends. It always returns the report, partial
// when it fails, and the error tells what it waited for and what it got.
func Analyze(d *Demuxer, u report.Until) (*report.Report, error) {
	var a Analyzer
	r := &a.Report
	for !u.Done(r) {
		p, err := d.ReadPES()
		if err == io.EOF {
			if u == (report.Until{}) {
				return r, nil
			}
			return r, fmt.Errorf("stream ended before %v: %v", u, r)
		} else if err != nil {
			return r, fmt.Errorf("%w, before %v: %v", err, u, r)
		}
		if err := a.Add(p); err != nil {
			return r, err
		}
	}
	return r, nil
}

// PlayHTTP plays an HTTP-TS URL and analyzes the stream until u, or until ctx
// ends. A nil client is http.DefaultClient.
func PlayHTTP(ctx context.Context, client *http.Client, url string, u report.Until) (*report.Report, error) {
	if client == nil {
		client = http.DefaultClient
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	res, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET %v: status %v", url, res.Status)
	}

	after := context.AfterFunc(ctx, func() { res.Body.Close() })
	defer after()
	r, err := Analyze(NewDemuxer(res.Body), u)
	if err != nil && ctx.Err() != nil {
		return r, fmt.Errorf("%w before %v: %v", ctx.Err(), u, r)
	}
	return r, err
}
