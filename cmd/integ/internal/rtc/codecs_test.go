// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/pion/rtp/codecs/av1/obu"
	"github.com/pion/rtp/codecs/vp9"
	"github.com/pion/webrtc/v4"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/ts"
)

// TestReadMP4Codecs reads the HEVC, VP9 and AV1 test files: every sample of
// each track, in decode order, a keyframe each second from the first, with
// what a payloader needs before a keyframe: HEVC in Annex B with the VPS,
// SPS and PPS, AV1 with its sequence header.
func TestReadMP4Codecs(t *testing.T) {
	for _, w := range []struct{ file, codec string }{
		{media.HEVCOpus, "hevc"}, {media.VP9Opus, "vp9"}, {media.AV1Opus, "av1"},
	} {
		t.Run(w.codec, func(t *testing.T) {
			path := media.Path(t, w.file)
			samples, err := ReadMP4(path)
			if err != nil {
				t.Fatal(err)
			}
			var videos, audios, keys int
			var last time.Duration
			for i, s := range samples {
				if s.DTS < last {
					t.Fatalf("sample %v at %v is before %v", i, s.DTS, last)
				}
				last = s.DTS
				if !s.Video {
					audios++
					if s.Codec != "opus" {
						t.Fatalf("audio sample %v codec %q, want opus", i, s.Codec)
					}
					continue
				}
				videos++
				if s.Codec != w.codec {
					t.Fatalf("video sample %v codec %q, want %v", i, s.Codec, w.codec)
				}
				if s.Key {
					keys++
				}
				checkKeySample(t, w.codec, s)
			}
			f, err := openMP4(t, path)
			if err != nil {
				t.Fatal(err)
			}
			if want := len(f.Track("vide").Samples); videos != want {
				t.Errorf("%v video samples, want %v", videos, want)
			}
			if want := len(f.Track("soun").Samples); audios != want {
				t.Errorf("%v audio samples, want %v", audios, want)
			}
			if keys != 30 {
				t.Errorf("%v keyframes, want 30 in 30s", keys)
			}
			if len(samples) == 0 || !samples[0].Video || !samples[0].Key || samples[0].DTS != 0 {
				t.Errorf("first sample %+v, want a keyframe at 0", first(samples))
			}
		})
	}
}

// checkKeySample checks a video sample is a keyframe exactly when it says
// so, and carries its codec's configuration then.
func checkKeySample(t *testing.T, codec string, s Sample) {
	t.Helper()
	switch codec {
	case "hevc":
		var types []byte
		for _, nal := range ts.SplitNALs(s.Data) {
			if len(nal) > 0 {
				types = append(types, (nal[0]>>1)&0x3f)
			}
		}
		irap := false
		for _, n := range types {
			irap = irap || (n >= 16 && n <= 21)
		}
		if s.Key && (len(types) < 4 || types[0] != 32 || types[1] != 33 || types[2] != 34 || !irap) {
			t.Fatalf("keyframe at %v has NALs %v, want VPS, SPS, PPS, then an IRAP", s.DTS, types)
		}
		if !s.Key && (irap || contains(types, 33)) {
			t.Fatalf("frame at %v has NALs %v, want no SPS or IRAP", s.DTS, types)
		}
	case "vp9":
		var h vp9.Header
		if err := h.Unmarshal(s.Data); err != nil {
			t.Fatalf("frame at %v: %v", s.DTS, err)
		}
		if s.Key != !h.NonKeyFrame {
			t.Fatalf("frame at %v: key %v, header says %v", s.DTS, s.Key, !h.NonKeyFrame)
		}
		if s.Key && (h.Width() != 768 || h.Height() != 320) {
			t.Fatalf("keyframe at %v is %vx%v, want 768x320", s.DTS, h.Width(), h.Height())
		}
	case "av1":
		h, err := obu.ParseOBUHeader(s.Data)
		if err != nil {
			t.Fatalf("frame at %v: %v", s.DTS, err)
		}
		if s.Key != (h.Type == obu.OBUSequenceHeader) {
			t.Fatalf("frame at %v: key %v, first OBU %v", s.DTS, s.Key, h.Type)
		}
	}
}

// TestReadG711 reads the G.711 test files as frames of 20ms: 160 bytes each.
func TestReadG711(t *testing.T) {
	for _, w := range []struct{ file, codec string }{{media.PCMU, "pcmu"}, {media.PCMA, "pcma"}} {
		t.Run(w.codec, func(t *testing.T) {
			samples, err := ReadG711(media.Path(t, w.file))
			if err != nil {
				t.Fatal(err)
			}
			if len(samples) != 1500 {
				t.Fatalf("%v frames, want 1500 in 30s", len(samples))
			}
			for i, s := range samples {
				if s.Video || s.Codec != w.codec || s.DTS != time.Duration(i)*20*time.Millisecond || len(s.Data) != 160 {
					t.Fatalf("frame %v: video %v codec %q at %v, %v bytes", i, s.Video, s.Codec, s.DTS, len(s.Data))
				}
			}
		})
	}
}

// codecFiles are the test media of each codec the client sends, with what
// a player of its first 2s reports.
var codecFiles = []struct {
	name, file, codec, profile string
	rate, channels             int
}{
	{"hevc", media.HEVCOpus, "hevc", "Main", 0, 0},
	{"vp9", media.VP9Opus, "vp9", "0", 0, 0},
	{"av1", media.AV1Opus, "av1", "Main", 0, 0},
	{"pcmu", media.PCMU, "pcmu", "", 8000, 1},
	{"pcma", media.PCMA, "pcma", "", 8000, 1},
}

// readCodecSamples reads the samples of a test file before d.
func readCodecSamples(t *testing.T, file string, d time.Duration) []Sample {
	t.Helper()
	path := media.Path(t, file)
	read := ReadMP4
	if strings.HasSuffix(file, ".ul") || strings.HasSuffix(file, ".al") {
		read = ReadG711
	}
	all, err := read(path)
	if err != nil {
		t.Fatal(err)
	}
	var out []Sample
	for _, s := range all {
		if s.DTS < d {
			out = append(out, s)
		}
	}
	return out
}

// TestAnalyzerCodecs packetizes the first 2s of each codec's test file as a
// publisher does, and analyzes the packets: the report is the file's.
func TestAnalyzerCodecs(t *testing.T) {
	for _, w := range codecFiles {
		t.Run(w.name, func(t *testing.T) {
			samples := readCodecSamples(t, w.file, 2*time.Second)
			senders := map[string]*sender{}
			var a Analyzer
			for _, s := range samples {
				key := sdpKey(s.Codec)
				snd := senders[key]
				if snd == nil {
					snd = newCodecSender(key)
					senders[key] = snd
				}
				for _, p := range snd.packets(s) {
					if err := a.Add(key, snd.clock, p); err != nil {
						t.Fatal(err)
					}
				}
			}
			checkCodecReport(t, w.codec, w.profile, w.rate, w.channels, a.Report())
		})
	}
}

// checkCodecReport checks the report of the first 2s of a codec's test file:
// the video, or the audio of a G.711 file.
func checkCodecReport(t *testing.T, codec, profile string, rate, channels int, r *report.Report) {
	t.Helper()
	if rate != 0 {
		a := r.Audio
		if a == nil || a.Codec != codec || a.SampleRate != rate || a.Channels != channels || a.Frames != 100 {
			t.Fatalf("report %v, want %v %vHz %vch with 100 frames", r, codec, rate, channels)
		}
		// Any video is the WHIP file's, at 25 fps.
		if err := r.CheckTimestamps(50 * time.Millisecond); err != nil {
			t.Error(err)
		}
		return
	}
	v := r.Video
	if v == nil || v.Codec != codec || v.Profile != profile || v.Width != 768 || v.Height != 320 || v.SequenceHeaders != 1 {
		t.Fatalf("report %v, want %v %v 768x320 with 1 sequence header", r, codec, profile)
	}
	if v.Keyframes != 2 || v.Frames != 50 || v.LeadingFrames != 0 || v.FirstDTS != 0 || v.LastDTS != 1960*time.Millisecond {
		t.Errorf("report %v, want 2 keyframes, 50 frames from 0 to 1.96s", r)
	}
	if err := r.CheckTimestamps(50 * time.Millisecond); err != nil {
		t.Error(err)
	}
}

// TestPublishPlayCodecs publishes the first 2s of each codec's test file
// through a fake server to a player that offers every codec: each session
// offers what its options name, and the player gets every frame.
func TestPublishPlayCodecs(t *testing.T) {
	every := &Options{Video: []string{"H264", "H265", "VP9", "AV1"}, Audio: []string{"opus", "PCMU", "PCMA"}}
	for _, w := range []struct {
		name, file string
		video      webrtc.RTPCodecCapability
		audio      webrtc.RTPCodecCapability
		opts       *Options
	}{
		{"hevc", media.HEVCOpus, capabilityOf(t, "H265"), capabilityOf(t, "opus"), &Options{Video: []string{"H265"}}},
		{"vp9", media.VP9Opus, capabilityOf(t, "VP9"), capabilityOf(t, "opus"), &Options{Video: []string{"VP9"}}},
		{"av1", media.AV1Opus, capabilityOf(t, "AV1"), capabilityOf(t, "opus"), &Options{Video: []string{"AV1"}}},
		{"pcmu", media.PCMU, capabilityOf(t, "H264"), capabilityOf(t, "PCMU"), &Options{Audio: []string{"PCMU"}}},
		{"pcma", media.PCMA, capabilityOf(t, "H264"), capabilityOf(t, "PCMA"), &Options{Audio: []string{"PCMA"}}},
	} {
		t.Run(w.name, func(t *testing.T) {
			sfu := newFakeSFUWith(t, w.video, w.audio)
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()

			player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", every)
			if err != nil {
				t.Fatal(err)
			}
			defer player.Close()
			for _, name := range []string{"H264/90000", "H265/90000", "VP9/90000", "AV1/90000", "opus/48000", "PCMU/8000", "PCMA/8000"} {
				if !strings.Contains(player.Offer, name) {
					t.Errorf("the player's offer has no %v", name)
				}
			}
			pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", w.opts)
			if err != nil {
				t.Fatal(err)
			}
			defer pub.Close()
			want := strings.ToUpper(w.name)
			if want == "HEVC" {
				want = "H265"
			}
			if !strings.Contains(strings.ToUpper(pub.Offer), " "+want+"/") {
				t.Errorf("the publisher's offer has no %v", want)
			}
			if w.opts.Video != nil && strings.Contains(pub.Offer, "H264/") {
				t.Errorf("the publisher's offer has H264, want only %v", w.opts.Video)
			}

			samples := readCodecSamples(t, w.file, 2*time.Second)
			if w.opts.Audio != nil {
				// G.711 has no video: send it with the H.264 of the WHIP file.
				for _, s := range readSamples(t, 2*time.Second) {
					if s.Video {
						samples = append(samples, s)
					}
				}
			} else {
				var video []Sample
				for _, s := range samples {
					if s.Video {
						video = append(video, s)
					}
				}
				samples = video
			}
			sortSamples(samples)
			if err := pub.Send(ctx, samples, nil); err != nil {
				t.Fatal(err)
			}
			f := codecFiles[0]
			for _, cf := range codecFiles {
				if cf.name == w.name {
					f = cf
				}
			}
			u := report.Until{VideoHeader: true, Keyframes: 2, VideoFrames: 50}
			if f.rate != 0 {
				u = report.Until{AudioFrames: 100}
			}
			r, err := player.Until(ctx, u)
			if err != nil {
				t.Fatal(err)
			}
			checkCodecReport(t, f.codec, f.profile, f.rate, f.channels, r)
		})
	}
}

// TestPublishWrongCodec writes a sample of a codec the track didn't
// negotiate: an error, not a broken stream.
func TestPublishWrongCodec(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	samples := readCodecSamples(t, media.VP9Opus, 100*time.Millisecond)
	if err := pub.Write(samples[0]); err == nil || !strings.Contains(err.Error(), "vp9") {
		t.Fatalf("write a VP9 sample to an H.264 track: %v, want an error naming vp9", err)
	}
}

// TestOptionsUnknownCodec refuses a codec the client doesn't know.
func TestOptionsUnknownCodec(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := Publish(ctx, "http://127.0.0.1:1/rtc/v1/whip/", &Options{Video: []string{"VP8"}})
	if err == nil || !strings.Contains(err.Error(), "VP8") {
		t.Fatalf("publish with VP8: %v, want an error naming it", err)
	}
}

// capabilityOf is a codec of the client, for the fake server's tracks.
func capabilityOf(t *testing.T, name string) webrtc.RTPCodecCapability {
	t.Helper()
	c, ok := codecTable[strings.ToLower(name)]
	if !ok {
		t.Fatalf("no codec %v", name)
	}
	return c.capability
}

// TestOptionsOf offers the codecs of the samples, and Every every codec.
func TestOptionsOf(t *testing.T) {
	o := OptionsOf([]Sample{{Video: true, Codec: "hevc"}, {Codec: "pcmu"}, {Video: true, Codec: "hevc"}})
	if o == nil || strings.Join(o.Video, ",") != "H265" || strings.Join(o.Audio, ",") != "PCMU" {
		t.Errorf("options %+v, want H265 and PCMU", o)
	}
	o = OptionsOf([]Sample{{Video: true, Codec: "vp9"}})
	if o == nil || strings.Join(o.Video, ",") != "VP9" || o.Audio != nil {
		t.Errorf("options %+v, want VP9 and the default audio", o)
	}
	e := Every()
	if strings.Join(e.Video, ",") != "H264,H265,VP9,AV1" || strings.Join(e.Audio, ",") != "opus,PCMU,PCMA" {
		t.Errorf("every %+v", e)
	}
	if _, _, err := sessionCodecs(e); err != nil {
		t.Error(err)
	}
}
