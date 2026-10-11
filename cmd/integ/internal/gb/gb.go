// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package gb is a GB28181 media publisher without SIP, as an external SIP
// server drives a device: it asks the server for a session by its publish API,
// then sends the media as MPEG-PS in RTP over TCP, framed as RFC 4571 says.
package gb

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
)

// An Answer is the server's answer to a publish request.
type Answer struct {
	Code  int    `json:"code"`
	Desc  string `json:"desc"`
	Port  int    `json:"port"`
	IsTCP bool   `json:"is_tcp"`
}

// Publish asks the server whose API is at api, such as
// http://127.0.0.1:1985, for a session of stream id whose media comes with
// ssrc. The SSRC goes as GB28181 writes it, 10 decimal digits.
func Publish(ctx context.Context, client *http.Client, api, id string, ssrc uint32) (*Answer, error) {
	body, err := json.Marshal(map[string]string{"id": id, "ssrc": fmt.Sprintf("%010d", ssrc)})
	if err != nil {
		return nil, err
	}
	return Post(ctx, client, api, body)
}

// Post sends body to the publish API as it is, and returns the answer.
func Post(ctx context.Context, client *http.Client, api string, body []byte) (*Answer, error) {
	req, err := http.NewRequestWithContext(ctx, "POST", strings.TrimSuffix(api, "/")+"/gb/v1/publish/", bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	res, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	b, err := io.ReadAll(res.Body)
	if err != nil {
		return nil, err
	}
	if res.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("publish API: status %v: %s", res.StatusCode, b)
	}
	var a Answer
	if err := json.Unmarshal(b, &a); err != nil {
		return nil, fmt.Errorf("publish API: %w: %s", err, b)
	}
	return &a, nil
}

// A Frame is one video frame, in Annex B with the parameter sets before each
// keyframe, or one AAC frame with its ADTS header.
type Frame struct {
	Video    bool
	Key      bool
	PTS, DTS time.Duration
	Data     []byte
}

var startCode = []byte{0, 0, 0, 1}

// ReadFLV reads the H.264 or HEVC video and the AAC audio of an FLV file as
// frames, in the file's order, and returns the video codec, "h264" or
// "hevc".
func ReadFLV(r io.Reader) (frames []Frame, vcodec string, err error) {
	fr, err := media.NewFLVReader(r)
	if err != nil {
		return nil, "", err
	}
	var sets, asc []byte
	lengthSize := 4
	for {
		tag, err := fr.ReadTag()
		if err == io.EOF {
			return frames, vcodec, nil
		} else if err != nil {
			return nil, "", err
		}
		at := time.Duration(tag.Timestamp) * time.Millisecond
		switch tag.Type {
		case media.TagVideo:
			v, err := media.ParseVideo(tag.Data)
			if err != nil {
				return nil, "", err
			}
			if v.Codec != "h264" && v.Codec != "hevc" {
				return nil, "", fmt.Errorf("video codec %v is not H.264 or HEVC", v.Codec)
			}
			vcodec = v.Codec
			if v.SequenceHeader {
				if v.Codec == "h264" {
					sets, lengthSize, err = parseAVCC(v.Payload)
				} else {
					sets, lengthSize, err = parseHVCC(v.Payload)
				}
				if err != nil {
					return nil, "", err
				}
				continue
			}
			if v.EndOfSequence || v.Metadata || len(v.Payload) == 0 {
				continue
			}
			if sets == nil {
				return nil, "", errors.New("a video frame before the sequence header")
			}
			var data []byte
			if v.Key {
				data = append(data, sets...)
			}
			if data, err = appendAnnexB(data, v.Payload, lengthSize); err != nil {
				return nil, "", err
			}
			frames = append(frames, Frame{Video: true, Key: v.Key, DTS: at, PTS: at + time.Duration(v.CTS)*time.Millisecond, Data: data})
		case media.TagAudio:
			a, err := media.ParseAudio(tag.Data)
			if err != nil {
				return nil, "", err
			}
			if a.Codec != "aac" {
				return nil, "", fmt.Errorf("audio codec %v is not AAC", a.Codec)
			}
			if a.SequenceHeader {
				asc = a.Payload
				continue
			}
			if len(asc) < 2 {
				return nil, "", errors.New("an AAC frame before the sequence header")
			}
			frames = append(frames, Frame{DTS: at, PTS: at, Data: append(adtsHeader(asc, len(a.Payload)), a.Payload...)})
		}
	}
}

// adtsHeader is the ADTS header of an AAC frame of n bytes, from its
// AudioSpecificConfig.
func adtsHeader(asc []byte, n int) []byte {
	object, freq, ch := asc[0]>>3, (asc[0]&7)<<1|asc[1]>>7, asc[1]>>3&0x0f
	size := n + 7
	return []byte{
		0xff, 0xf1,
		(object-1)<<6 | freq<<2 | ch>>2,
		ch<<6 | byte(size>>11&3),
		byte(size >> 3),
		byte(size<<5) | 0x1f,
		0xfc,
	}
}

// parseAVCC returns the SPS and PPS of an avcC in Annex B, and its NAL length
// size.
func parseAVCC(b []byte) (sets []byte, lengthSize int, err error) {
	if len(b) < 6 {
		return nil, 0, errors.New("short avcC")
	}
	lengthSize = int(b[4]&3) + 1
	p := 5
	for _, mask := range []byte{0x1f, 0xff} {
		if p >= len(b) {
			return nil, 0, errors.New("short avcC")
		}
		n := int(b[p] & mask)
		p++
		for range n {
			if sets, p, err = appendSet(sets, b, p); err != nil {
				return nil, 0, fmt.Errorf("avcC: %w", err)
			}
		}
	}
	return sets, lengthSize, nil
}

// parseHVCC returns the VPS, SPS and PPS of an hvcC in Annex B, and its NAL
// length size.
func parseHVCC(b []byte) (sets []byte, lengthSize int, err error) {
	if len(b) < 23 {
		return nil, 0, errors.New("short hvcC")
	}
	lengthSize = int(b[21]&3) + 1
	arrays, p := int(b[22]), 23
	for range arrays {
		if p+3 > len(b) {
			return nil, 0, errors.New("short hvcC array")
		}
		n := int(binary.BigEndian.Uint16(b[p+1:]))
		p += 3
		for range n {
			if sets, p, err = appendSet(sets, b, p); err != nil {
				return nil, 0, fmt.Errorf("hvcC: %w", err)
			}
		}
	}
	return sets, lengthSize, nil
}

// appendSet appends the parameter set at b[p:], after its 2-byte length, in
// Annex B, and returns where the next one starts.
func appendSet(sets, b []byte, p int) ([]byte, int, error) {
	if p+2 > len(b) {
		return nil, 0, errors.New("short NAL length")
	}
	size := int(binary.BigEndian.Uint16(b[p:]))
	p += 2
	if p+size > len(b) {
		return nil, 0, errors.New("short NAL")
	}
	return append(append(sets, startCode...), b[p:p+size]...), p + size, nil
}

// appendAnnexB appends the NALs of a frame with NAL length prefixes as Annex
// B.
func appendAnnexB(out, b []byte, lengthSize int) ([]byte, error) {
	for len(b) > 0 {
		if len(b) < lengthSize {
			return nil, errors.New("cut NAL length")
		}
		var n int
		for _, c := range b[:lengthSize] {
			n = n<<8 | int(c)
		}
		b = b[lengthSize:]
		if n > len(b) {
			return nil, fmt.Errorf("NAL of %v bytes, %v left", n, len(b))
		}
		out = append(append(out, startCode...), b[:n]...)
		b = b[n:]
	}
	return out, nil
}

// MaxPayload is the most PS bytes the publisher puts in one RTP packet.
const MaxPayload = 1400

// PayloadType is the RTP payload type of PS.
const PayloadType = 96

// A Publisher sends frames as PS in RTP packets over a stream, each packet
// after its length in 2 bytes. Each frame is one PS pack, and starts a new
// RTP packet.
type Publisher struct {
	w    io.Writer
	ssrc uint32
	mux  *Muxer
	seq  uint16
}

// NewPublisher sends frames of video codec vcodec, "h264" or "hevc", and AAC
// to w, with ssrc.
func NewPublisher(w io.Writer, ssrc uint32, vcodec string) (*Publisher, error) {
	mux, err := NewMuxer(vcodec)
	if err != nil {
		return nil, err
	}
	return &Publisher{w: w, ssrc: ssrc, mux: mux}, nil
}

// WriteFrame sends a frame.
func (p *Publisher) WriteFrame(f Frame) error {
	pack := p.mux.Pack(f)
	var out []byte
	for len(pack) > 0 {
		n := min(len(pack), MaxPayload)
		out = p.appendRTP(out, pack[:n], f.PTS, n == len(pack))
		pack = pack[n:]
	}
	_, err := p.w.Write(out)
	return err
}

// WriteRTP sends one RTP packet with the payload, at time ts, with the next
// sequence number.
func (p *Publisher) WriteRTP(payload []byte, ts time.Duration, marker bool) error {
	_, err := p.w.Write(p.appendRTP(nil, payload, ts, marker))
	return err
}

// WriteFramed sends b after its length in 2 bytes, as it is.
func (p *Publisher) WriteFramed(b []byte) error {
	_, err := p.w.Write(append(binary.BigEndian.AppendUint16(nil, uint16(len(b))), b...))
	return err
}

// appendRTP appends an RTP packet with the payload after its length.
func (p *Publisher) appendRTP(b, payload []byte, ts time.Duration, marker bool) []byte {
	b = binary.BigEndian.AppendUint16(b, uint16(12+len(payload)))
	pt := byte(PayloadType)
	if marker {
		pt |= 0x80
	}
	b = append(b, 0x80, pt)
	b = binary.BigEndian.AppendUint16(b, p.seq)
	b = binary.BigEndian.AppendUint32(b, uint32(ticks(ts)))
	b = binary.BigEndian.AppendUint32(b, p.ssrc)
	p.seq++
	return append(b, payload...)
}

// Send sends frames in order, paced by pacer like ffmpeg -re, or at once
// when pacer is nil, until they end or ctx ends.
func (p *Publisher) Send(ctx context.Context, frames []Frame, pacer *rtmp.Pacer) error {
	for _, f := range frames {
		var err error
		if pacer != nil {
			err = pacer.Wait(ctx, uint32(f.DTS/time.Millisecond))
		} else {
			err = ctx.Err()
		}
		if err != nil {
			return err
		}
		if err := p.WriteFrame(f); err != nil {
			return err
		}
	}
	return nil
}
