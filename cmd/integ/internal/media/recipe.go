// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package media is the suite's test media: the recipes that transcode the
// source, doc/source.flv, into a fixed set of files, the cache that keeps them
// in testdata/media, and readers for the FLV and MP4 files it makes.
package media

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"strings"
)

// The files of the test media, one per recipe.
const (
	// H264HighAAC is like a real encoder: H.264 High with B-frames and AAC.
	H264HighAAC = "h264-high-aac.flv"
	// H264BaselineAAC has no B-frames, for RTMP to WebRTC and RTSP.
	H264BaselineAAC = "h264-baseline-aac.flv"
	// H264BaselineOpus is for WHIP.
	H264BaselineOpus = "h264-baseline-opus.mp4"
	// HEVCAAC is for enhanced RTMP.
	HEVCAAC = "hevc-aac.flv"
	// HEVCOpus has no B-frames, for WebRTC.
	HEVCOpus = "hevc-opus.mp4"
	// AACOnly has no video.
	AACOnly = "aac-only.flv"
	// H264Only has no audio.
	H264Only = "h264-only.flv"
	// H264HighMP3 has MP3 audio.
	H264HighMP3 = "h264-high-mp3.flv"
	// VP9Opus is for WebRTC.
	VP9Opus = "vp9-opus.mp4"
	// AV1Opus is for WebRTC.
	AV1Opus = "av1-opus.mp4"
	// PCMU and PCMA are raw G.711 at 8 kHz mono, mu-law and A-law, for
	// WebRTC.
	PCMU = "pcmu.ul"
	PCMA = "pcma.al"
)

// Every file is a cut of this length from the start of the source.
const (
	Duration = 30 // seconds
	FPS      = 25
	// GOP is the fixed keyframe interval, in frames: one keyframe a second.
	GOP = FPS
)

// An Encoder is one ffmpeg encoder and its options.
type Encoder struct {
	Name string   `json:"name"`
	Args []string `json:"args"`
}

// A Recipe makes one file. Video and Audio list alternative encoders, the
// first one the ffmpeg has wins; an empty list leaves the stream out.
type Recipe struct {
	Name   string    `json:"name"`
	Format string    `json:"format"`
	Video  []Encoder `json:"video,omitempty"`
	Audio  []Encoder `json:"audio,omitempty"`
}

// The video options every recipe shares: a fixed 1s GOP at 25 fps.
var (
	gopArgs = []string{"-r", "25", "-g", "25", "-keyint_min", "25", "-pix_fmt", "yuv420p"}

	x264High     = Encoder{"libx264", join(gopArgs, "-sc_threshold", "0", "-preset", "veryfast", "-profile:v", "high", "-bf", "3")}
	x264Baseline = Encoder{"libx264", join(gopArgs, "-sc_threshold", "0", "-preset", "veryfast", "-profile:v", "baseline")}
	x265         = Encoder{"libx265", join(gopArgs, "-preset", "veryfast", "-x265-params", "keyint=25:min-keyint=25:scenecut=0:open-gop=0:log-level=error")}
	x265NoB      = Encoder{"libx265", join(gopArgs, "-preset", "veryfast", "-x265-params", "keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=0:log-level=error")}
	// libvpx adds keyframes on scene changes at the realtime deadline, not at good.
	vp9 = Encoder{"libvpx-vp9", join(gopArgs, "-deadline", "good", "-cpu-used", "8", "-lag-in-frames", "0", "-row-mt", "1", "-b:v", "1M")}
	av1 = Encoder{"libsvtav1", join(gopArgs, "-preset", "10", "-svtav1-params", "scd=0")}

	aac = Encoder{"aac", []string{"-ar", "44100", "-ac", "2", "-b:a", "128k"}}
	mp3 = Encoder{"libmp3lame", []string{"-ar", "44100", "-ac", "2", "-b:a", "128k"}}
	// G.711 encoders are native to ffmpeg.
	pcmu = Encoder{"pcm_mulaw", []string{"-ar", "8000", "-ac", "1"}}
	pcma = Encoder{"pcm_alaw", []string{"-ar", "8000", "-ac", "1"}}
	// Opus prefers libopus and falls back to ffmpeg's native encoder, which
	// is experimental.
	opus = []Encoder{
		{"libopus", []string{"-ar", "48000", "-ac", "2", "-b:a", "64k"}},
		{"opus", []string{"-strict", "-2", "-ar", "48000", "-ac", "2", "-b:a", "64k"}},
	}
)

// Recipes is the test media set.
var Recipes = []Recipe{
	{H264HighAAC, "flv", []Encoder{x264High}, []Encoder{aac}},
	{H264BaselineAAC, "flv", []Encoder{x264Baseline}, []Encoder{aac}},
	{H264BaselineOpus, "mp4", []Encoder{x264Baseline}, opus},
	{HEVCAAC, "flv", []Encoder{x265}, []Encoder{aac}},
	{HEVCOpus, "mp4", []Encoder{x265NoB}, opus},
	{AACOnly, "flv", nil, []Encoder{aac}},
	{H264Only, "flv", []Encoder{x264High}, nil},
	{H264HighMP3, "flv", []Encoder{x264High}, []Encoder{mp3}},
	{VP9Opus, "mp4", []Encoder{vp9}, opus},
	{AV1Opus, "mp4", []Encoder{av1}, opus},
	{PCMU, "mulaw", nil, []Encoder{pcmu}},
	{PCMA, "alaw", nil, []Encoder{pcma}},
}

func join(base []string, more ...string) []string {
	return append(append([]string{}, base...), more...)
}

// RecipeOf returns the recipe of a file name.
func RecipeOf(name string) (Recipe, bool) {
	return recipeIn(Recipes, name)
}

// Spec identifies the recipe's definition: it changes whenever the recipe
// does, so the cache regenerates its file.
func (r Recipe) Spec() string {
	b, _ := json.Marshal(r)
	sum := sha256.Sum256(b)
	return hex.EncodeToString(sum[:8])
}

// Args returns the ffmpeg arguments that transcode src into out with the
// encoders ff has. When ff lacks every alternative of a stream, it returns
// the missing encoders instead, such as "libopus or opus".
func (r Recipe) Args(ff *FFmpeg, src, out string) (args []string, missing string) {
	args = []string{"-hide_banner", "-loglevel", "error", "-nostdin", "-y",
		"-i", src, "-t", fmt.Sprint(Duration)}

	pick := func(alts []Encoder) (Encoder, bool) {
		for _, e := range alts {
			if ff.Has(e.Name) {
				return e, true
			}
		}
		return Encoder{}, false
	}
	names := func(alts []Encoder) string {
		var s []string
		for _, e := range alts {
			s = append(s, e.Name)
		}
		return strings.Join(s, " or ")
	}

	if len(r.Video) > 0 {
		e, ok := pick(r.Video)
		if !ok {
			return nil, names(r.Video)
		}
		args = append(args, "-map", "0:v:0", "-c:v", e.Name)
		args = append(args, e.Args...)
	}
	if len(r.Audio) > 0 {
		e, ok := pick(r.Audio)
		if !ok {
			return nil, names(r.Audio)
		}
		args = append(args, "-map", "0:a:0", "-c:a", e.Name)
		args = append(args, e.Args...)
	}
	return append(args, "-f", r.Format, out), ""
}
