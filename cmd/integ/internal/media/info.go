// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"
)

// Info is what a test media file carries, read by the FLV or MP4 reader.
type Info struct {
	Video *VideoInfo
	Audio *AudioInfo
}

// VideoInfo describes the video stream.
type VideoInfo struct {
	// Codec is "h264", "hevc", "vp9" or "av1".
	Codec string
	// Profile is "Baseline", "Main" or "High" for H.264, "Main" for HEVC
	// and AV1, and "0" to "3" for VP9.
	Profile string
	Frames  int
	// Keyframes are the decode times of the keyframes.
	Keyframes []time.Duration
	// BFrames is set when the composition offsets of the frames differ,
	// which only reordered frames make.
	BFrames  bool
	Duration time.Duration
}

// FPS is the frame rate over the stream's duration.
func (v *VideoInfo) FPS() float64 {
	if v.Duration <= 0 {
		return 0
	}
	return float64(v.Frames) / v.Duration.Seconds()
}

// AudioInfo describes the audio stream.
type AudioInfo struct {
	// Codec is "aac", "mp3", "opus", "pcmu" or "pcma".
	Codec      string
	SampleRate int
	Channels   int
	Frames     int
	Duration   time.Duration
}

// ProbeFile reads a test media file, FLV, MP4 or raw G.711 by its extension.
func ProbeFile(path string) (*Info, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	switch filepath.Ext(path) {
	case ".flv":
		return probeFLV(f)
	case ".mp4":
		st, err := f.Stat()
		if err != nil {
			return nil, err
		}
		return probeMP4(f, st.Size())
	case ".ul", ".al":
		st, err := f.Stat()
		if err != nil {
			return nil, err
		}
		return probeG711(filepath.Ext(path), st.Size()), nil
	default:
		return nil, fmt.Errorf("unknown media type of %v", path)
	}
}

// G.711 is 8 kHz mono, one byte a sample; WebRTC sends it in frames of
// 20ms.
const (
	G711Rate       = 8000
	G711FrameBytes = G711Rate / 50
)

// probeG711 describes a raw G.711 file of size bytes: .ul is mu-law (PCMU),
// .al A-law (PCMA).
func probeG711(ext string, size int64) *Info {
	codec := "pcmu"
	if ext == ".al" {
		codec = "pcma"
	}
	return &Info{Audio: &AudioInfo{
		Codec: codec, SampleRate: G711Rate, Channels: 1,
		Frames:   int((size + G711FrameBytes - 1) / G711FrameBytes),
		Duration: time.Duration(size) * time.Second / G711Rate,
	}}
}

// span is the duration of n frames whose first and last decode times are
// given: the time between them plus one average frame.
func span(first, last time.Duration, n int) time.Duration {
	if n < 2 {
		return 0
	}
	return last - first + (last-first)/time.Duration(n-1)
}

func probeFLV(r io.Reader) (*Info, error) {
	fr, err := NewFLVReader(r)
	if err != nil {
		return nil, err
	}

	info := &Info{}
	var vFirst, vLast, aFirst, aLast time.Duration
	var ctsSeen bool
	var firstCTS int32
	for {
		tag, err := fr.ReadTag()
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, err
		}
		ts := time.Duration(tag.Timestamp) * time.Millisecond

		switch tag.Type {
		case TagVideo:
			v, err := ParseVideo(tag.Data)
			if err != nil {
				return nil, err
			}
			if info.Video == nil {
				info.Video = &VideoInfo{Codec: v.Codec}
			}
			if v.SequenceHeader {
				info.Video.Profile = Profile(v.Codec, v.Payload)
				continue
			}
			if v.EndOfSequence || v.Metadata {
				continue
			}
			if info.Video.Frames == 0 {
				vFirst = ts
			}
			vLast = ts
			info.Video.Frames++
			if v.Key {
				info.Video.Keyframes = append(info.Video.Keyframes, ts)
			}
			if !ctsSeen {
				ctsSeen, firstCTS = true, v.CTS
			} else if v.CTS != firstCTS {
				info.Video.BFrames = true
			}
		case TagAudio:
			a, err := ParseAudio(tag.Data)
			if err != nil {
				return nil, err
			}
			if info.Audio == nil {
				info.Audio = &AudioInfo{Codec: a.Codec}
			}
			if a.SequenceHeader {
				info.Audio.SampleRate, info.Audio.Channels = AACConfig(a.Payload)
				continue
			}
			if a.Codec == "mp3" && info.Audio.Frames == 0 {
				info.Audio.SampleRate, info.Audio.Channels = MP3Header(a.Payload)
			}
			if info.Audio.Frames == 0 {
				aFirst = ts
			}
			aLast = ts
			info.Audio.Frames++
		}
	}
	if info.Video != nil {
		info.Video.Duration = span(vFirst, vLast, info.Video.Frames)
	}
	if info.Audio != nil {
		info.Audio.Duration = span(aFirst, aLast, info.Audio.Frames)
	}
	return info, nil
}

func probeMP4(r io.ReaderAt, size int64) (*Info, error) {
	f, err := ReadMP4(r, size)
	if err != nil {
		return nil, err
	}
	info := &Info{}
	at := func(t *MP4Track, dts uint64) time.Duration {
		return time.Duration(dts * uint64(time.Second) / uint64(t.Timescale))
	}

	if t := f.Track("vide"); t != nil && len(t.Samples) > 0 {
		codec := map[string]string{"avc1": "h264", "avc3": "h264", "hvc1": "hevc", "hev1": "hevc", "vp09": "vp9", "av01": "av1"}[t.Codec]
		if codec == "" {
			return nil, fmt.Errorf("unknown video sample entry %q", t.Codec)
		}
		config := t.Config
		if codec == "vp9" && len(config) > 4 {
			config = config[4:] // vpcC is a full box.
		}
		v := &VideoInfo{Codec: codec, Profile: Profile(codec, config), Frames: len(t.Samples)}
		for _, s := range t.Samples {
			if s.Key {
				v.Keyframes = append(v.Keyframes, at(t, s.DTS))
			}
			if s.CTS != t.Samples[0].CTS {
				v.BFrames = true
			}
		}
		v.Duration = span(at(t, t.Samples[0].DTS), at(t, t.Samples[len(t.Samples)-1].DTS), len(t.Samples))
		info.Video = v
	}

	if t := f.Track("soun"); t != nil && len(t.Samples) > 0 {
		codec := map[string]string{"mp4a": "aac", "Opus": "opus"}[t.Codec]
		if codec == "" {
			return nil, fmt.Errorf("unknown audio sample entry %q", t.Codec)
		}
		info.Audio = &AudioInfo{
			Codec: codec, SampleRate: int(t.SampleRate), Channels: int(t.Channels), Frames: len(t.Samples),
			Duration: span(at(t, t.Samples[0].DTS), at(t, t.Samples[len(t.Samples)-1].DTS), len(t.Samples)),
		}
	}
	if info.Video == nil && info.Audio == nil {
		return nil, errors.New("no audio or video samples")
	}
	return info, nil
}

// Profile reads the profile from a codec configuration record: avcC,
// hvcC, av1C, or the VP9 codec configuration record without its full box
// header.
func Profile(codec string, config []byte) string {
	if len(config) < 2 {
		return ""
	}
	switch codec {
	case "h264":
		switch config[1] {
		case 66:
			return "Baseline"
		case 77:
			return "Main"
		case 100:
			return "High"
		}
		return fmt.Sprint(config[1])
	case "hevc":
		if p := config[1] & 0x1f; p == 1 {
			return "Main"
		} else {
			return fmt.Sprint(p)
		}
	case "av1":
		if p := config[1] >> 5; p == 0 {
			return "Main"
		} else {
			return fmt.Sprint(p)
		}
	case "vp9":
		return fmt.Sprint(config[0])
	}
	return ""
}

// AACConfig reads the sample rate and channels of an AudioSpecificConfig.
func AACConfig(asc []byte) (rate, channels int) {
	if len(asc) < 2 {
		return 0, 0
	}
	rates := []int{96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350}
	index := int(asc[0]&0x07)<<1 | int(asc[1]>>7)
	if index < len(rates) {
		rate = rates[index]
	}
	return rate, int(asc[1]>>3) & 0x0f
}

// MP3Header reads the sample rate and channels of an MP3 frame header.
func MP3Header(b []byte) (rate, channels int) {
	if len(b) < 4 || b[0] != 0xff || b[1]&0xe0 != 0xe0 {
		return 0, 0
	}
	base := []int{44100, 48000, 32000}
	index := int(b[2]>>2) & 0x03
	if index > 2 {
		return 0, 0
	}
	switch (b[1] >> 3) & 0x03 {
	case 3: // MPEG-1
		rate = base[index]
	case 2: // MPEG-2
		rate = base[index] / 2
	case 0: // MPEG-2.5
		rate = base[index] / 4
	}
	if b[3]>>6 == 3 {
		return rate, 1
	}
	return rate, 2
}
