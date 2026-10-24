// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package gb

import (
	"encoding/binary"
	"fmt"
	"time"
)

// The stream types of the PS stream map.
const (
	StreamTypeH264 = 0x1b
	StreamTypeHEVC = 0x24
	StreamTypeAAC  = 0x0f
)

// The PES stream ids of the video and the audio.
const (
	VideoStreamID = 0xe0
	AudioStreamID = 0xc0
)

// maxPES is the most bytes after the length field of a PES packet.
const maxPES = 0xffff

// A Muxer makes a PS pack of each frame: the pack header, then for a
// keyframe the system header and the stream map, then the frame in PES
// packets of at most 65535 bytes each.
type Muxer struct {
	videoType byte
}

// NewMuxer makes packs of video codec vcodec, "h264" or "hevc", and AAC.
func NewMuxer(vcodec string) (*Muxer, error) {
	switch vcodec {
	case "h264":
		return &Muxer{videoType: StreamTypeH264}, nil
	case "hevc":
		return &Muxer{videoType: StreamTypeHEVC}, nil
	}
	return nil, fmt.Errorf("no PS stream type for video codec %q", vcodec)
}

// Pack returns the PS pack of a frame.
func (m *Muxer) Pack(f Frame) []byte {
	b := packHeader(make([]byte, 0, len(f.Data)+128), ticks(f.DTS))
	if f.Video && f.Key {
		b = systemHeader(b)
		b = m.streamMap(b)
	}
	id := byte(AudioStreamID)
	if f.Video {
		id = VideoStreamID
	}
	return appendPES(b, id, f)
}

// ticks is a time in the 90 kHz clock.
func ticks(d time.Duration) uint64 {
	return uint64(d * 90000 / time.Second)
}

// packHeader appends an MPEG-2 pack header with the clock reference scr and
// no stuffing.
func packHeader(b []byte, scr uint64) []byte {
	const muxRate = 6106 // in 50 bytes/s, about 2.4 Mbps
	return append(b, 0, 0, 1, 0xba,
		0x44|byte(scr>>27&0x38)|byte(scr>>28&0x03),
		byte(scr>>20),
		0x04|byte(scr>>12&0xf8)|byte(scr>>13&0x03),
		byte(scr>>5),
		0x04|byte(scr<<3&0xf8),
		0x01,
		byte(muxRate>>14), byte(muxRate>>6&0xff), byte(muxRate<<2&0xff)|0x03,
		0xf8)
}

// systemHeader appends a system header for one video and one audio stream.
func systemHeader(b []byte) []byte {
	const rateBound = 6106
	return append(b, 0, 0, 1, 0xbb, 0, 12,
		0x80|byte(rateBound>>15), byte(rateBound>>7&0xff), byte(rateBound<<1&0xff)|0x01,
		1<<2|0x02,     // audio_bound 1, fixed 0, CSPS 0
		0xe0|1,        // audio and video lock, marker, video_bound 1
		0x7f,          // no packet rate restriction
		VideoStreamID, // P-STD buffer: scale 1024 bytes, 400 units
		0xe0|400>>8&0x1f, 400&0xff,
		AudioStreamID, // P-STD buffer: scale 128 bytes, 32 units
		0xc0, 32)
}

// streamMap appends the program stream map of the video and the audio.
func (m *Muxer) streamMap(b []byte) []byte {
	start := len(b)
	b = append(b, 0, 0, 1, 0xbc, 0, 0,
		0x80|0x60, // current, version 0
		0xfe|0x01, // marker
		0, 0,      // no program descriptors
		0, 8, // two streams
		m.videoType, VideoStreamID, 0, 0,
		StreamTypeAAC, AudioStreamID, 0, 0)
	binary.BigEndian.PutUint16(b[start+4:], uint16(len(b)-start-6+4))
	return binary.BigEndian.AppendUint32(b, mpegCRC(b[start:]))
}

// mpegCRC is the CRC-32 of MPEG-2 systems, which feeds the bits MSB first.
func mpegCRC(b []byte) uint32 {
	crc := ^uint32(0)
	for _, c := range b {
		crc ^= uint32(c) << 24
		for range 8 {
			if crc&0x80000000 != 0 {
				crc = crc<<1 ^ 0x04c11db7
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

// appendPES appends a frame in PES packets of stream id, the first with the
// frame's PTS, and its DTS when they differ.
func appendPES(b []byte, id byte, f Frame) []byte {
	data, first := f.Data, true
	for first || len(data) > 0 {
		var opt []byte
		if first {
			pts, dts := ticks(f.PTS), ticks(f.DTS)
			if pts == dts {
				opt = appendTime(append(opt, 0x80, 0x80, 5), 0x2, pts)
			} else {
				opt = appendTime(appendTime(append(opt, 0x80, 0xc0, 10), 0x3, pts), 0x1, dts)
			}
		} else {
			opt = append(opt, 0x80, 0x00, 0)
		}
		n := min(len(data), maxPES-len(opt))
		b = append(b, 0, 0, 1, id)
		b = binary.BigEndian.AppendUint16(b, uint16(len(opt)+n))
		b = append(append(b, opt...), data[:n]...)
		data, first = data[n:], false
	}
	return b
}

// appendTime appends a 33-bit PES time with its 4-bit prefix and markers.
func appendTime(b []byte, prefix byte, t uint64) []byte {
	return append(b,
		prefix<<4|byte(t>>29&0x0e)|1,
		byte(t>>22),
		byte(t>>14)|1,
		byte(t>>7),
		byte(t<<1)|1)
}
