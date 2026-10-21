package main

import (
	"bytes"
	"errors"
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/codecs/h264"
	"github.com/bluenviron/mediacommon/v2/pkg/codecs/mpeg4audio"
	"github.com/bluenviron/mediacommon/v2/pkg/formats/mpegts"
	"github.com/bluenviron/mediacommon/v2/pkg/formats/mpegts/codecs"
)

// tsMuxer turns the FLV source into MPEG-TS for the SRT publisher, with
// mediacommon's writer: the elementary streams are the source's own, so the
// TS is as flat as the source.
type tsMuxer struct {
	out      bytes.Buffer
	w        *mpegts.Writer
	video    *mpegts.Track
	audio    *mpegts.Track
	sps, pps []byte
	// started is set by the first video frame. Audio before it is dropped, so
	// video is the writer's leading track: it carries the PCR, and PAT and PMT
	// go out before each IDR instead of before each audio frame.
	started bool
}

// isAVCFrame is an FLV AVC video frame, not a sequence header.
func isAVCFrame(m rtmpMessage) bool {
	return m.typeID == 9 && len(m.payload) > 5 && m.payload[0]&0x80 == 0 && m.payload[0]&0x0f == 7 && m.payload[1] == 1
}

func isAACTag(m rtmpMessage, packetType byte) bool {
	return m.typeID == 8 && len(m.payload) > 2 && m.payload[0]>>4 == 10 && m.payload[1] == packetType
}

// avccNALUs splits NALUs with 4-byte lengths.
func avccNALUs(b []byte) ([][]byte, error) {
	var nalus [][]byte
	for len(b) > 0 {
		if len(b) < 4 {
			return nil, errors.New("truncated AVC NALU length")
		}
		size := int(b[0])<<24 | int(b[1])<<16 | int(b[2])<<8 | int(b[3])
		if size <= 0 || size > len(b)-4 {
			return nil, fmt.Errorf("bad AVC NALU length %d", size)
		}
		nalus = append(nalus, b[4:4+size])
		b = b[4+size:]
	}
	if len(nalus) == 0 {
		return nil, errors.New("AVC frame has no NALU")
	}
	return nalus, nil
}

// parseAVCConfig reads the first SPS and PPS of an AVCDecoderConfigurationRecord.
func parseAVCConfig(b []byte) (sps, pps []byte, err error) {
	bad := errors.New("bad AVC decoder configuration record")
	if len(b) < 8 || b[5]&0x1f == 0 {
		return nil, nil, bad
	}
	i := 6
	for n := int(b[5] & 0x1f); n > 0; n-- {
		if len(b) < i+2 {
			return nil, nil, bad
		}
		size := int(b[i])<<8 | int(b[i+1])
		if len(b) < i+2+size {
			return nil, nil, bad
		}
		if sps == nil {
			sps = b[i+2 : i+2+size]
		}
		i += 2 + size
	}
	if len(b) < i+1 || b[i] == 0 {
		return nil, nil, bad
	}
	n := int(b[i])
	i++
	for ; n > 0; n-- {
		if len(b) < i+2 {
			return nil, nil, bad
		}
		size := int(b[i])<<8 | int(b[i+1])
		if len(b) < i+2+size {
			return nil, nil, bad
		}
		if pps == nil {
			pps = b[i+2 : i+2+size]
		}
		i += 2 + size
	}
	return sps, pps, nil
}

// newTSMuxer takes the tracks from the source's sequence headers: H.264,
// which it needs, and AAC if there is one.
func newTSMuxer(tags []rtmpMessage) (*tsMuxer, error) {
	m := &tsMuxer{}
	var config *mpeg4audio.AudioSpecificConfig
	for _, tag := range tags {
		if m.sps == nil && tag.typeID == 9 && len(tag.payload) > 5 && tag.payload[0] == 0x17 && tag.payload[1] == 0 {
			sps, pps, err := parseAVCConfig(tag.payload[5:])
			if err != nil {
				return nil, err
			}
			m.sps, m.pps = sps, pps
		}
		if config == nil && isAACTag(tag, 0) {
			var c mpeg4audio.AudioSpecificConfig
			if err := c.Unmarshal(tag.payload[2:]); err != nil {
				return nil, fmt.Errorf("AAC sequence header: %w", err)
			}
			config = &c
		}
	}
	if m.sps == nil {
		return nil, errors.New("source has no AVC sequence header to mux TS from")
	}
	m.video = &mpegts.Track{Codec: &codecs.H264{}}
	tracks := []*mpegts.Track{m.video}
	if config != nil {
		m.audio = &mpegts.Track{Codec: &codecs.MPEG4Audio{Config: *config}}
		tracks = append(tracks, m.audio)
	}
	m.w = &mpegts.Writer{W: &m.out, Tracks: tracks}
	if err := m.w.Initialize(); err != nil {
		return nil, err
	}
	return m, nil
}

// mux gives a tag's TS packets, in 90kHz time from its timestamp, or nothing
// for a sequence header or another tag. An IDR gets the SPS and PPS before
// it, which a TS stream carries in band. The bytes are valid until the next
// call.
func (m *tsMuxer) mux(tag rtmpMessage) ([]byte, error) {
	m.out.Reset()
	dts := int64(tag.timestamp) * 90
	var err error
	switch {
	case isAVCFrame(tag):
		// The composition time is a signed 24-bit value.
		cts := int64(int32(get24(tag.payload[2:5])<<8) >> 8)
		au, perr := avccNALUs(tag.payload[5:])
		if perr != nil {
			return nil, perr
		}
		if h264.IsRandomAccess(au) {
			hasSPS := false
			for _, nalu := range au {
				hasSPS = hasSPS || h264.NALUType(nalu[0]&0x1f) == h264.NALUTypeSPS
			}
			if !hasSPS {
				au = append([][]byte{m.sps, m.pps}, au...)
			}
		}
		m.started = true
		err = m.w.WriteH264(m.video, dts+cts*90, dts, au)
	case m.audio != nil && m.started && isAACTag(tag, 1):
		err = m.w.WriteMPEG4Audio(m.audio, dts, [][]byte{tag.payload[2:]})
	default:
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return m.out.Bytes(), nil
}

// tsDemuxer is the SRT players' lean TS demuxer: it learns the H.264 and AAC
// PIDs from PAT and PMT, rebuilds each PES, and reports a video access unit's
// NALUs or an audio PES's ADTS data with its 90kHz PTS. A player that joins
// mid-stream ignores everything before the first PAT and PMT, which go out
// before each IDR.
type tsDemuxer struct {
	onVideo  func(pts int64, au [][]byte)
	onAudio  func(pts int64, data []byte)
	rest     []byte
	pmtPID   int
	videoPID int
	audioPID int
	pes      map[int]*tsPES
}

// tsPES is a PES being rebuilt: its PTS and data, and its length when the
// header gives one, 0 when it ends at the next PES of its PID.
type tsPES struct {
	pts    int64
	data   []byte
	length int
	active bool
}

func newTSDemuxer(onVideo func(int64, [][]byte), onAudio func(int64, []byte)) *tsDemuxer {
	return &tsDemuxer{onVideo: onVideo, onAudio: onAudio, pmtPID: -1, videoPID: -1, audioPID: -1, pes: map[int]*tsPES{}}
}

// write takes TS in any split; a lost sync byte is an error.
func (d *tsDemuxer) write(b []byte) error {
	if len(d.rest) > 0 {
		need := 188 - len(d.rest)
		if len(b) < need {
			d.rest = append(d.rest, b...)
			return nil
		}
		d.rest = append(d.rest, b[:need]...)
		b = b[need:]
		if err := d.packet(d.rest); err != nil {
			return err
		}
		d.rest = d.rest[:0]
	}
	for ; len(b) >= 188; b = b[188:] {
		if err := d.packet(b[:188]); err != nil {
			return err
		}
	}
	d.rest = append(d.rest, b...)
	return nil
}

func (d *tsDemuxer) packet(p []byte) error {
	if p[0] != 0x47 {
		return fmt.Errorf("TS packet sync byte 0x%02x, want 0x47", p[0])
	}
	pid := int(p[1]&0x1f)<<8 | int(p[2])
	start := p[1]&0x40 != 0
	control := p[3] >> 4 & 3
	payload := p[4:]
	if control&2 != 0 {
		if len(payload) < 1 || int(payload[0]) >= len(payload) {
			return nil
		}
		payload = payload[1+int(payload[0]):]
	}
	if control&1 == 0 || len(payload) == 0 {
		return nil
	}
	switch {
	case pid == 0 && start:
		d.pat(payload)
	case pid == d.pmtPID && start:
		d.pmt(payload)
	case pid >= 0 && (pid == d.videoPID || pid == d.audioPID):
		d.pesPacket(pid, start, payload)
	}
	return nil
}

// section is the PSI section after the pointer field, cut to its length
// without the CRC, or nil.
func section(payload []byte, tableID byte) []byte {
	at := 1 + int(payload[0])
	if len(payload) < at+3 || payload[at] != tableID {
		return nil
	}
	s := payload[at:]
	length := int(s[1]&0x0f)<<8 | int(s[2])
	if length < 9 || len(s) < 3+length {
		return nil
	}
	return s[:3+length-4]
}

func (d *tsDemuxer) pat(payload []byte) {
	s := section(payload, 0)
	if s == nil {
		return
	}
	for i := 8; i+4 <= len(s); i += 4 {
		if program := int(s[i])<<8 | int(s[i+1]); program != 0 {
			d.pmtPID = int(s[i+2]&0x1f)<<8 | int(s[i+3])
			return
		}
	}
}

func (d *tsDemuxer) pmt(payload []byte) {
	s := section(payload, 2)
	if s == nil || len(s) < 12 {
		return
	}
	i := 12 + (int(s[10]&0x0f)<<8 | int(s[11]))
	for ; i+5 <= len(s); i += 5 + (int(s[i+3]&0x0f)<<8 | int(s[i+4])) {
		pid := int(s[i+1]&0x1f)<<8 | int(s[i+2])
		switch s[i] {
		case 0x1b:
			d.videoPID = pid
		case 0x0f:
			d.audioPID = pid
		}
	}
}

func (d *tsDemuxer) pesPacket(pid int, start bool, payload []byte) {
	pes := d.pes[pid]
	if pes == nil {
		pes = &tsPES{}
		d.pes[pid] = pes
	}
	if start {
		if pes.active && pes.length == 0 {
			d.emit(pid, pes)
		}
		pes.active = false
		// The PES header: start code, stream id, length, two flag bytes, the
		// header data length, then the PTS when the flags have one.
		if len(payload) < 9 || payload[0] != 0 || payload[1] != 0 || payload[2] != 1 || payload[7]&0x80 == 0 || len(payload) < 9+int(payload[8]) || payload[8] < 5 {
			return
		}
		q := payload[9:]
		pes.pts = int64(q[0]>>1&7)<<30 | int64(q[1])<<22 | int64(q[2]>>1)<<15 | int64(q[3])<<7 | int64(q[4]>>1)
		pes.length = 0
		if n := int(payload[4])<<8 | int(payload[5]); n != 0 {
			pes.length = n - 3 - int(payload[8])
		}
		pes.data = append(pes.data[:0], payload[9+int(payload[8]):]...)
		pes.active = true
	} else if pes.active {
		pes.data = append(pes.data, payload...)
	}
	if pes.active && pes.length > 0 && len(pes.data) >= pes.length {
		pes.data = pes.data[:pes.length]
		d.emit(pid, pes)
		pes.active = false
	}
}

// flush ends the PES that end at the next one of their PID, as a video PES
// does, at the end of a TS file such as an HLS segment.
func (d *tsDemuxer) flush() {
	for _, pid := range []int{d.videoPID, d.audioPID} {
		if pes := d.pes[pid]; pes != nil && pes.active && pes.length == 0 {
			d.emit(pid, pes)
			pes.active = false
		}
	}
}

func (d *tsDemuxer) emit(pid int, pes *tsPES) {
	if pid == d.audioPID {
		d.onAudio(pes.pts, pes.data)
		return
	}
	var au h264.AnnexB
	if err := au.Unmarshal(pes.data); err != nil {
		return
	}
	if len(au) > 0 && h264.NALUType(au[0][0]&0x1f) == h264.NALUTypeAccessUnitDelimiter {
		au = au[1:]
	}
	if len(au) > 0 {
		d.onVideo(pes.pts, au)
	}
}

// newStreamTSDemuxer is the demuxer of a player of one continuous TS stream,
// such as SRT or HTTP-TS: each H.264 frame is sent as an FLV AVC payload in
// milliseconds from its first PTS, and an audio PES at the newest video time.
// send runs in the demuxer's write.
func newStreamTSDemuxer(send func(rtmpMessage) bool) *tsDemuxer {
	var clock ptsClock
	var newest uint32
	return newTSDemuxer(func(pts int64, au [][]byte) {
		newest = clock.millis(pts)
		send(rtmpMessage{typeID: 9, timestamp: newest, payload: avcPayload(au)})
	}, func(pts int64, data []byte) {
		send(rtmpMessage{typeID: 8, timestamp: newest, payload: data})
	})
}

// ptsClock turns 90kHz PTS into milliseconds from the first one, across the
// 33-bit wrap.
type ptsClock struct {
	started bool
	last    int64
	ticks   int64
}

func (c *ptsClock) millis(pts int64) uint32 {
	const wrap = int64(1) << 33
	if !c.started {
		c.started, c.last = true, pts
	}
	delta := (pts - c.last) & (wrap - 1)
	if delta >= wrap/2 {
		delta -= wrap
	}
	c.ticks += delta
	c.last = pts
	if c.ticks < 0 {
		return 0
	}
	return uint32(c.ticks / 90)
}
