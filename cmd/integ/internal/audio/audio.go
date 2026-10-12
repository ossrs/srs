// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package audio reads ADTS (AAC) and MPEG audio (MP3) frames, from a raw
// .aac or .mp3 stream or from the payload of a TS packet, and adds them to the
// stream report.
package audio

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"path"
	"strings"
	"time"

	"srs-integ/internal/report"
)

// ADTS is an ADTS header.
type ADTS struct {
	// Profile is the AAC audio object type, 2 for LC.
	Profile    int
	SampleRate int
	Channels   int
	// HeaderSize is 7, or 9 with a CRC. FrameSize includes the header.
	HeaderSize, FrameSize int
	// Samples is 1024 for each raw data block of the frame.
	Samples int
}

var aacRates = []int{96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350}

// ParseADTS parses the ADTS header at the start of b.
func ParseADTS(b []byte) (ADTS, error) {
	if len(b) < 7 {
		return ADTS{}, fmt.Errorf("short ADTS header of %v bytes", len(b))
	}
	if b[0] != 0xff || b[1]&0xf6 != 0xf0 {
		return ADTS{}, fmt.Errorf("no ADTS sync word in %x", b[:2])
	}
	h := ADTS{Profile: int(b[2]>>6) + 1, HeaderSize: 7}
	if b[1]&0x01 == 0 {
		h.HeaderSize = 9
	}
	index := int(b[2]>>2) & 0x0f
	if index >= len(aacRates) {
		return ADTS{}, fmt.Errorf("bad ADTS sample rate index %v", index)
	}
	h.SampleRate = aacRates[index]
	h.Channels = int(b[2]&0x01)<<2 | int(b[3]>>6)
	h.FrameSize = int(b[3]&0x03)<<11 | int(b[4])<<3 | int(b[5]>>5)
	if h.FrameSize <= h.HeaderSize {
		return ADTS{}, fmt.Errorf("ADTS frame size %v, not over its %v-byte header", h.FrameSize, h.HeaderSize)
	}
	h.Samples = 1024 * (int(b[6]&0x03) + 1)
	return h, nil
}

// MP3 is an MPEG audio frame header.
type MP3 struct {
	// Version is "1", "2" or "2.5"; Layer is 1, 2 or 3.
	Version string
	Layer   int
	// Bitrate is in bits per second.
	Bitrate    int
	SampleRate int
	Channels   int
	FrameSize  int
	Samples    int
}

// The bitrates in kbps by index: MPEG-1 layers 1, 2 and 3, then MPEG-2 and
// 2.5 layer 1, and layers 2 and 3.
var mp3Bitrates = [5][15]int{
	{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
	{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
	{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
	{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
	{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
}

// ParseMP3 parses the MPEG audio header at the start of b. Free-format
// frames, which have no bitrate and so no size, are an error.
func ParseMP3(b []byte) (MP3, error) {
	if len(b) < 4 {
		return MP3{}, fmt.Errorf("short MPEG audio header of %v bytes", len(b))
	}
	if b[0] != 0xff || b[1]&0xe0 != 0xe0 {
		return MP3{}, fmt.Errorf("no MPEG audio sync in %x", b[:2])
	}
	var h MP3
	divisor := 1
	switch (b[1] >> 3) & 0x03 {
	case 3:
		h.Version = "1"
	case 2:
		h.Version, divisor = "2", 2
	case 0:
		h.Version, divisor = "2.5", 4
	default:
		return MP3{}, errors.New("reserved MPEG audio version")
	}
	h.Layer = 4 - int(b[1]>>1)&0x03
	if h.Layer == 4 {
		return MP3{}, errors.New("reserved MPEG audio layer")
	}
	index := int(b[2] >> 4)
	if index == 0 || index == 15 {
		return MP3{}, fmt.Errorf("unsupported MPEG audio bitrate index %v", index)
	}
	table := h.Layer - 1
	if h.Version != "1" {
		table = min(3+h.Layer-1, 4)
	}
	h.Bitrate = mp3Bitrates[table][index] * 1000
	rate := int(b[2]>>2) & 0x03
	if rate == 3 {
		return MP3{}, errors.New("reserved MPEG audio sample rate")
	}
	h.SampleRate = []int{44100, 48000, 32000}[rate] / divisor
	h.Channels = 2
	if b[3]>>6 == 3 {
		h.Channels = 1
	}
	padding := int(b[2]>>1) & 0x01
	switch {
	case h.Layer == 1:
		h.FrameSize, h.Samples = (12*h.Bitrate/h.SampleRate+padding)*4, 384
	case h.Layer == 3 && h.Version != "1":
		h.FrameSize, h.Samples = 72*h.Bitrate/h.SampleRate+padding, 576
	default:
		h.FrameSize, h.Samples = 144*h.Bitrate/h.SampleRate+padding, 1152
	}
	return h, nil
}

// A Frame is one audio frame.
type Frame struct {
	// Codec is "aac" or "mp3".
	Codec                         string
	SampleRate, Channels, Samples int
	// Config identifies the decoder configuration of an AAC frame: its
	// profile, sample rate and channels. It is empty for MP3, which has none.
	Config string
	// Data is the frame with its header.
	Data []byte
}

// headerSize is the bytes to read to know a frame's size.
func headerSize(codec string) (int, error) {
	switch codec {
	case "aac":
		return 7, nil
	case "mp3":
		return 4, nil
	}
	return 0, fmt.Errorf("unsupported audio codec %q", codec)
}

// parseHeader parses the header of a frame of codec at the start of b; the
// frame's Data is not set.
func parseHeader(codec string, b []byte) (*Frame, int, error) {
	switch codec {
	case "aac":
		h, err := ParseADTS(b)
		if err != nil {
			return nil, 0, err
		}
		return &Frame{
			Codec: codec, SampleRate: h.SampleRate, Channels: h.Channels, Samples: h.Samples,
			Config: fmt.Sprintf("profile %v, %vHz, %vch", h.Profile, h.SampleRate, h.Channels),
		}, h.FrameSize, nil
	case "mp3":
		h, err := ParseMP3(b)
		if err != nil {
			return nil, 0, err
		}
		return &Frame{Codec: codec, SampleRate: h.SampleRate, Channels: h.Channels, Samples: h.Samples}, h.FrameSize, nil
	}
	return nil, 0, fmt.Errorf("unsupported audio codec %q", codec)
}

// ParseFrame parses the whole frame at the start of b; its Data is the
// frame, without the bytes after it.
func ParseFrame(codec string, b []byte) (*Frame, error) {
	f, size, err := parseHeader(codec, b)
	if err != nil {
		return nil, err
	}
	if len(b) < size {
		return nil, fmt.Errorf("%v frame of %v bytes cut at %v", codec, size, len(b))
	}
	f.Data = b[:size]
	return f, nil
}

// SplitFrames splits b, whole ADTS or MP3 frames, into frames.
func SplitFrames(codec string, b []byte) ([]*Frame, error) {
	var frames []*Frame
	for pos := 0; pos < len(b); {
		f, err := ParseFrame(codec, b[pos:])
		if err != nil {
			return frames, fmt.Errorf("at byte %v: %w", pos, err)
		}
		frames = append(frames, f)
		pos += len(f.Data)
	}
	return frames, nil
}

// CodecOf is the codec of a raw audio path by its extension: "aac" for .aac,
// "mp3" for .mp3, or "".
func CodecOf(p string) string {
	switch strings.ToLower(path.Ext(p)) {
	case ".aac":
		return "aac"
	case ".mp3":
		return "mp3"
	}
	return ""
}

// A Reader reads the frames of a raw .aac or .mp3 stream.
type Reader struct {
	r     *bufio.Reader
	codec string
	// pos is the offset of the next frame.
	pos     int64
	started bool
}

// NewReader reads frames of codec, "aac" or "mp3", from r. An MP3 stream
// may start with an ID3v2 tag, which it skips.
func NewReader(r io.Reader, codec string) *Reader {
	return &Reader{r: bufio.NewReader(r), codec: codec}
}

// skipID3 skips the ID3v2 tag at the start of an MP3 stream.
func (r *Reader) skipID3() error {
	b, err := r.r.Peek(10)
	if err != nil || string(b[:3]) != "ID3" {
		return nil
	}
	size := int(b[6]&0x7f)<<21 | int(b[7]&0x7f)<<14 | int(b[8]&0x7f)<<7 | int(b[9]&0x7f) + 10
	if b[5]&0x10 != 0 {
		size += 10
	}
	n, err := r.r.Discard(size)
	r.pos += int64(n)
	if err == io.EOF {
		return io.ErrUnexpectedEOF
	}
	return err
}

// ReadFrame reads the next frame. It fails when the next bytes are not a
// frame header, and returns io.EOF at the end.
func (r *Reader) ReadFrame() (*Frame, error) {
	if !r.started {
		r.started = true
		if r.codec == "mp3" {
			if err := r.skipID3(); err != nil {
				return nil, err
			}
		}
	}
	n, err := headerSize(r.codec)
	if err != nil {
		return nil, err
	}
	b, err := r.r.Peek(n)
	if err == io.EOF && len(b) == 0 {
		return nil, io.EOF
	} else if err == io.EOF {
		return nil, io.ErrUnexpectedEOF
	} else if err != nil {
		return nil, err
	}
	f, size, err := parseHeader(r.codec, b)
	if err != nil {
		return nil, fmt.Errorf("at byte %v: %w", r.pos, err)
	}
	f.Data = make([]byte, size)
	if _, err := io.ReadFull(r.r, f.Data); err == io.EOF {
		return nil, io.ErrUnexpectedEOF
	} else if err != nil {
		return nil, err
	}
	r.pos += int64(size)
	return f, nil
}

// A Track adds the audio frames of one stream to a report.
type Track struct {
	config string
}

// Add adds a frame at its decode time. It counts a sequence header when the
// AAC configuration first appears or changes, because raw AAC and TS carry it
// in every frame, not once like FLV.
func (t *Track) Add(r *report.Report, f *Frame, dts time.Duration) {
	if f.Config != "" && f.Config != t.config {
		t.config = f.Config
		r.AudioHeader(f.Codec, f.SampleRate, f.Channels)
	}
	r.AddAudio(f.Codec, dts)
	// MP3 has no sequence header; each frame tells its rate.
	if a := r.Audio; a.SampleRate == 0 {
		a.SampleRate, a.Channels = f.SampleRate, f.Channels
	}
}

// An Analyzer adds the frames of a raw stream to its report, timed by the
// samples before each frame.
type Analyzer struct {
	Report report.Report
	track  Track
	// The next frame is samples after base, at rate; a new rate starts a
	// new base, so the time doesn't drift by rounding each frame.
	base          time.Duration
	samples, rate int
}

// Add adds the next frame of the stream.
func (a *Analyzer) Add(f *Frame) {
	if f.SampleRate != a.rate {
		a.base, a.samples, a.rate = a.at(), 0, f.SampleRate
	}
	a.track.Add(&a.Report, f, a.at())
	a.samples += f.Samples
}

// at is the decode time of the next frame.
func (a *Analyzer) at() time.Duration {
	if a.rate == 0 {
		return a.base
	}
	return a.base + time.Duration(a.samples)*time.Second/time.Duration(a.rate)
}

// Analyze reads frames from src until the report meets u, or, for the zero
// Until, until the stream ends. It always returns the report, partial when it
// fails.
func Analyze(src *Reader, u report.Until) (*report.Report, error) {
	var a Analyzer
	r := &a.Report
	for !u.Done(r) {
		f, err := src.ReadFrame()
		if err == io.EOF {
			if u == (report.Until{}) {
				return r, nil
			}
			return r, fmt.Errorf("stream ended before %v: %v", u, r)
		} else if err != nil {
			return r, fmt.Errorf("%w, before %v: %v", err, u, r)
		}
		a.Add(f)
	}
	return r, nil
}

// PlayHTTP plays an HTTP .aac or .mp3 URL and analyzes the stream until u, or
// until ctx ends. A nil client is http.DefaultClient.
func PlayHTTP(ctx context.Context, client *http.Client, url string, u report.Until) (*report.Report, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	codec := CodecOf(req.URL.Path)
	if codec == "" {
		return nil, fmt.Errorf("GET %v: want an .aac or .mp3 path", url)
	}
	if client == nil {
		client = http.DefaultClient
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
	r, err := Analyze(NewReader(res.Body, codec), u)
	if err != nil && ctx.Err() != nil {
		return r, fmt.Errorf("%w before %v: %v", ctx.Err(), u, r)
	}
	return r, err
}
