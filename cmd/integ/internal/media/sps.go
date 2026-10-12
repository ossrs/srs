// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"errors"
	"fmt"
)

// SPSSize reads the picture size from an H.264 or HEVC SPS NAL unit, with its
// NAL header, after the cropping or conformance window.
func SPSSize(codec string, nal []byte) (width, height int, err error) {
	switch codec {
	case "h264":
		if len(nal) < 4 || nal[0]&0x1f != 7 {
			return 0, 0, errors.New("not an H.264 SPS")
		}
		return h264Size(&bitReader{b: unescape(nal[1:])})
	case "hevc":
		if len(nal) < 3 || (nal[0]>>1)&0x3f != 33 {
			return 0, 0, errors.New("not an HEVC SPS")
		}
		return hevcSize(&bitReader{b: unescape(nal[2:])})
	default:
		return 0, 0, fmt.Errorf("no SPS for codec %q", codec)
	}
}

// VideoSize reads the picture size from a decoder configuration record: avcC
// for H.264 or hvcC for HEVC.
func VideoSize(codec string, config []byte) (width, height int, err error) {
	switch codec {
	case "h264":
		// Version, profile, compatibility, level, NAL length size, then the
		// number of SPS and the first one with its 16-bit size.
		if len(config) < 8 || config[5]&0x1f == 0 {
			return 0, 0, errors.New("avcC has no SPS")
		}
		n := int(config[6])<<8 | int(config[7])
		if len(config) < 8+n {
			return 0, 0, errors.New("short SPS in avcC")
		}
		return SPSSize(codec, config[8:8+n])
	case "hevc":
		// 22 bytes of header, then the arrays of NAL units by type.
		if len(config) < 23 {
			return 0, 0, errors.New("short hvcC")
		}
		b, arrays := config[23:], int(config[22])
		for i := 0; i < arrays; i++ {
			if len(b) < 3 {
				return 0, 0, errors.New("short hvcC array")
			}
			typ, count := b[0]&0x3f, int(b[1])<<8|int(b[2])
			b = b[3:]
			for j := 0; j < count; j++ {
				if len(b) < 2 {
					return 0, 0, errors.New("short hvcC NAL unit")
				}
				n := int(b[0])<<8 | int(b[1])
				if len(b) < 2+n {
					return 0, 0, errors.New("short hvcC NAL unit")
				}
				if typ == 33 {
					return SPSSize(codec, b[2:2+n])
				}
				b = b[2+n:]
			}
		}
		return 0, 0, errors.New("hvcC has no SPS")
	default:
		return 0, 0, fmt.Errorf("no picture size in the %v configuration", codec)
	}
}

// unescape removes the emulation prevention bytes, the 3 of each 00 00 03.
func unescape(b []byte) []byte {
	out := make([]byte, 0, len(b))
	zeros := 0
	for _, c := range b {
		if zeros >= 2 && c == 3 {
			zeros = 0
			continue
		}
		if c == 0 {
			zeros++
		} else {
			zeros = 0
		}
		out = append(out, c)
	}
	return out
}

var errShortSPS = errors.New("short SPS")

// bitReader reads bits and Exp-Golomb codes; it remembers the first error.
type bitReader struct {
	b   []byte
	pos int
	err error
}

func (r *bitReader) bits(n int) uint32 {
	var v uint32
	for i := 0; i < n; i++ {
		if r.pos >= len(r.b)*8 {
			r.err = errShortSPS
			return 0
		}
		v = v<<1 | uint32(r.b[r.pos/8]>>(7-r.pos%8))&1
		r.pos++
	}
	return v
}

func (r *bitReader) flag() bool { return r.bits(1) == 1 }

func (r *bitReader) ue() int {
	zeros := 0
	for r.bits(1) == 0 {
		if r.err != nil || zeros > 31 {
			r.err = errShortSPS
			return 0
		}
		zeros++
	}
	return int(1<<zeros - 1 + r.bits(zeros))
}

func (r *bitReader) se() int {
	v := r.ue()
	if v%2 == 1 {
		return (v + 1) / 2
	}
	return -v / 2
}

// cropUnits are the units of the crop window: SubWidthC and SubHeightC, or 1
// for monochrome and separate colour planes.
func cropUnits(chroma int, separate bool) (x, y int) {
	if separate {
		return 1, 1
	}
	switch chroma {
	case 1:
		return 2, 2
	case 2:
		return 2, 1
	}
	return 1, 1
}

// h264Size reads an H.264 SPS after its NAL header, ITU-T H.264 7.3.2.1.1.
func h264Size(r *bitReader) (int, int, error) {
	profile := r.bits(8)
	r.bits(16) // Constraint flags and level.
	r.ue()     // seq_parameter_set_id
	chroma, separate := 1, false
	switch profile {
	case 100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135:
		if chroma = r.ue(); chroma == 3 {
			separate = r.flag()
		}
		r.ue()   // bit_depth_luma_minus8
		r.ue()   // bit_depth_chroma_minus8
		r.flag() // qpprime_y_zero_transform_bypass_flag
		if r.flag() {
			lists := 8
			if chroma == 3 {
				lists = 12
			}
			for i := 0; i < lists; i++ {
				if !r.flag() {
					continue
				}
				size, last, next := 16, 8, 8
				if i >= 6 {
					size = 64
				}
				for j := 0; j < size; j++ {
					if next != 0 {
						next = (last + r.se() + 256) % 256
					}
					if next != 0 {
						last = next
					}
				}
			}
		}
	}
	r.ue() // log2_max_frame_num_minus4
	switch r.ue() {
	case 0:
		r.ue() // log2_max_pic_order_cnt_lsb_minus4
	case 1:
		r.flag() // delta_pic_order_always_zero_flag
		r.se()   // offset_for_non_ref_pic
		r.se()   // offset_for_top_to_bottom_field
		for n := r.ue(); n > 0 && r.err == nil; n-- {
			r.se()
		}
	}
	r.ue()   // max_num_ref_frames
	r.flag() // gaps_in_frame_num_value_allowed_flag
	mbWidth, mapHeight := r.ue()+1, r.ue()+1
	frameMbsOnly := r.flag()
	if !frameMbsOnly {
		r.flag() // mb_adaptive_frame_field_flag
	}
	r.flag() // direct_8x8_inference_flag
	var left, right, top, bottom int
	if r.flag() {
		left, right, top, bottom = r.ue(), r.ue(), r.ue(), r.ue()
	}
	if r.err != nil {
		return 0, 0, r.err
	}

	fields := 2
	if frameMbsOnly {
		fields = 1
	}
	ux, uy := cropUnits(chroma, separate)
	uy *= fields
	return mbWidth*16 - ux*(left+right), fields*mapHeight*16 - uy*(top+bottom), nil
}

// hevcSize reads an HEVC SPS after its NAL header, ITU-T H.265 7.3.2.2.
func hevcSize(r *bitReader) (int, int, error) {
	r.bits(4) // sps_video_parameter_set_id
	subLayers := int(r.bits(3))
	r.flag() // sps_temporal_id_nesting_flag

	// profile_tier_level: the general profile and level are 96 bits.
	r.bits(32)
	r.bits(32)
	r.bits(32)
	profile, level := make([]bool, subLayers), make([]bool, subLayers)
	for i := 0; i < subLayers; i++ {
		profile[i], level[i] = r.flag(), r.flag()
	}
	if subLayers > 0 {
		for i := subLayers; i < 8; i++ {
			r.bits(2)
		}
	}
	for i := 0; i < subLayers; i++ {
		if profile[i] {
			r.bits(32)
			r.bits(32)
			r.bits(24)
		}
		if level[i] {
			r.bits(8)
		}
	}

	r.ue() // sps_seq_parameter_set_id
	chroma, separate := r.ue(), false
	if chroma == 3 {
		separate = r.flag()
	}
	width, height := r.ue(), r.ue()
	if r.flag() {
		left, right, top, bottom := r.ue(), r.ue(), r.ue(), r.ue()
		ux, uy := cropUnits(chroma, separate)
		width -= ux * (left + right)
		height -= uy * (top + bottom)
	}
	if r.err != nil {
		return 0, 0, r.err
	}
	return width, height, nil
}
