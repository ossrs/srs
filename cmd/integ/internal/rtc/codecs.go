// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"sort"
	"strings"
	"time"

	"github.com/pion/rtp"
	"github.com/pion/rtp/codecs"
	"github.com/pion/rtp/codecs/av1/obu"
	"github.com/pion/rtp/codecs/vp9"
	"github.com/pion/webrtc/v4"

	"srs-integ/internal/media"
)

// A codec is one the client sends and plays.
type codec struct {
	kind       webrtc.RTPCodecType
	capability webrtc.RTPCodecCapability
	// pt is its payload type in a session's offer.
	pt webrtc.PayloadType
	// sample is the codec of its samples and reports, such as "hevc".
	sample string
}

var (
	videoFeedback = []webrtc.RTCPFeedback{
		{Type: "goog-remb"}, {Type: "ccm", Parameter: "fir"}, {Type: "nack"}, {Type: "nack", Parameter: "pli"}, {Type: "transport-cc"},
	}
	audioFeedback = []webrtc.RTCPFeedback{{Type: "transport-cc"}}
)

// codecTable holds the codecs the client knows, by their name in SDP in
// lower case: H.264 with packetization mode 1, HEVC, VP9 and AV1, Opus and
// G.711, with the feedback SRS takes.
var codecTable = map[string]codec{
	"h264": {webrtc.RTPCodecTypeVideo, webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeH264, ClockRate: 90000,
		SDPFmtpLine: "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f", RTCPFeedback: videoFeedback}, 106, "h264"},
	"h265": {webrtc.RTPCodecTypeVideo, webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeH265, ClockRate: 90000,
		SDPFmtpLine: "level-id=93;profile-id=1;tier-flag=0;tx-mode=SRST", RTCPFeedback: videoFeedback}, 116, "hevc"},
	"vp9": {webrtc.RTPCodecTypeVideo, webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeVP9, ClockRate: 90000,
		SDPFmtpLine: "profile-id=0", RTCPFeedback: videoFeedback}, 98, "vp9"},
	"av1": {webrtc.RTPCodecTypeVideo, webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeAV1, ClockRate: 90000,
		RTCPFeedback: videoFeedback}, 45, "av1"},
	"opus": {webrtc.RTPCodecTypeAudio, webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeOpus, ClockRate: 48000, Channels: 2,
		SDPFmtpLine: "minptime=10;useinbandfec=1", RTCPFeedback: audioFeedback}, 111, "opus"},
	"pcmu": {webrtc.RTPCodecTypeAudio, webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypePCMU, ClockRate: 8000, Channels: 1,
		RTCPFeedback: audioFeedback}, 0, "pcmu"},
	"pcma": {webrtc.RTPCodecTypeAudio, webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypePCMA, ClockRate: 8000, Channels: 1,
		RTCPFeedback: audioFeedback}, 8, "pcma"},
}

// sdpKey is the codecTable key of a sample codec, such as h265 for hevc.
func sdpKey(sample string) string {
	if sample == "hevc" {
		return "h265"
	}
	return sample
}

// mimeKey is the codecTable key of a MIME type, such as h265 for
// video/H265.
func mimeKey(mime string) string {
	_, name, _ := strings.Cut(mime, "/")
	return strings.ToLower(name)
}

// sessionCodecs are the codecTable keys of the codecs a session offers: the
// options' or H.264 and Opus.
func sessionCodecs(opts *Options) (video, audio []string, err error) {
	video, audio = []string{"h264"}, []string{"opus"}
	if opts == nil {
		return video, audio, nil
	}
	pick := func(names []string, kind webrtc.RTPCodecType, def []string) ([]string, error) {
		if names == nil {
			return def, nil
		}
		var keys []string
		for _, name := range names {
			c, ok := codecTable[strings.ToLower(name)]
			if !ok || c.kind != kind {
				return nil, fmt.Errorf("no %v codec %v", kind, name)
			}
			keys = append(keys, strings.ToLower(name))
		}
		return keys, nil
	}
	if video, err = pick(opts.Video, webrtc.RTPCodecTypeVideo, video); err != nil {
		return nil, nil, err
	}
	if audio, err = pick(opts.Audio, webrtc.RTPCodecTypeAudio, audio); err != nil {
		return nil, nil, err
	}
	return video, audio, nil
}

// newCodecSender makes the sender of a codecTable codec, with its payloader
// and clock.
func newCodecSender(key string) *sender {
	switch key {
	case "h265":
		return newSender(&codecs.H265Payloader{}, 90000, true)
	case "vp9":
		return newSender(&codecs.VP9Payloader{}, 90000, true)
	case "av1":
		return newSender(&codecs.AV1Payloader{}, 90000, true)
	case "opus":
		return newSender(&codecs.OpusPayloader{}, 48000, false)
	case "pcmu", "pcma":
		return newSender(&codecs.G711Payloader{}, media.G711Rate, false)
	}
	return newSender(&codecs.H264Payloader{}, 90000, true)
}

func newSender(p rtp.Payloader, clock uint32, video bool) *sender {
	var b [6]byte
	rand.Read(b[:])
	return &sender{payloader: p, clock: clock, video: video,
		seq: binary.BigEndian.Uint16(b[:2]), base: binary.BigEndian.Uint32(b[2:])}
}

func newVideoSender() *sender { return newCodecSender("h264") }

func newAudioSender() *sender { return newCodecSender("opus") }

// ReadG711 reads a raw G.711 file, .ul for mu-law (PCMU) or .al for A-law
// (PCMA), as audio samples of 20ms; a cut frame at the end is left out.
func ReadG711(path string) ([]Sample, error) {
	codec := map[string]string{".ul": "pcmu", ".al": "pcma"}[filepath.Ext(path)]
	if codec == "" {
		return nil, fmt.Errorf("%v is not a .ul or .al file", path)
	}
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var out []Sample
	for i := 0; (i+1)*media.G711FrameBytes <= len(b); i++ {
		out = append(out, Sample{Codec: codec, DTS: time.Duration(i) * 20 * time.Millisecond,
			Data: b[i*media.G711FrameBytes : (i+1)*media.G711FrameBytes]})
	}
	return out, nil
}

// sortSamples sorts samples by decode time, keeping the order of samples at
// the same time.
func sortSamples(s []Sample) {
	sort.SliceStable(s, func(i, j int) bool { return s[i].DTS < s[j].DTS })
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
			if p+2 > len(b) {
				return nil, 0, errors.New("short hvcC NAL")
			}
			size := int(binary.BigEndian.Uint16(b[p:]))
			p += 2
			if p+size > len(b) {
				return nil, 0, errors.New("short hvcC NAL")
			}
			sets = append(append(sets, startCode...), b[p:p+size]...)
			p += size
		}
	}
	return sets, lengthSize, nil
}

// av1OBUs splits OBUs that have their size fields, as MP4 and the RTP
// depacketizer give them, into their headers and payloads.
func av1OBUs(b []byte) ([]obu.OBU, error) {
	var out []obu.OBU
	for len(b) > 0 {
		h, err := obu.ParseOBUHeader(b)
		if err != nil {
			return nil, err
		}
		if !h.HasSizeField {
			return nil, errors.New("AV1 OBU without a size field")
		}
		size, n, err := obu.ReadLeb128(b[h.Size():])
		if err != nil {
			return nil, err
		}
		start := h.Size() + int(n)
		if start+int(size) > len(b) {
			return nil, fmt.Errorf("AV1 OBU of %v bytes, %v left", size, len(b)-start)
		}
		out = append(out, obu.OBU{Header: *h, Payload: b[start : start+int(size)]})
		b = b[start+int(size):]
	}
	return out, nil
}

// vp9Frame reads a VP9 frame's uncompressed header: whether it is a
// keyframe, and its profile and size, which only a keyframe has.
func vp9Frame(b []byte) (key bool, profile string, width, height int, err error) {
	var h vp9.Header
	if err := h.Unmarshal(b); err != nil {
		return false, "", 0, 0, err
	}
	if h.ShowExistingFrame || h.NonKeyFrame {
		return false, "", 0, 0, nil
	}
	return true, fmt.Sprint(h.Profile), int(h.Width()), int(h.Height()), nil
}

// OptionsOf offers the codecs of samples; a kind without samples offers its
// default.
func OptionsOf(samples []Sample) *Options {
	o := &Options{}
	for _, s := range samples {
		names := &o.Audio
		if s.Video {
			names = &o.Video
		}
		name := codecTable[sdpKey(s.Codec)].capability.MimeType
		if _, name, _ = strings.Cut(name, "/"); name != "" && !slices.Contains(*names, name) {
			*names = append(*names, name)
		}
	}
	return o
}

// Every offers every codec the client knows, for a player of any stream.
func Every() *Options {
	return &Options{Video: []string{"H264", "H265", "VP9", "AV1"}, Audio: []string{"opus", "PCMU", "PCMA"}}
}
