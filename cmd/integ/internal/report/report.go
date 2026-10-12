// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package report is the stream report that every analyzer of the suite
// fills, whatever the protocol: the codecs, the picture size and sample rate,
// the sequence headers, keyframes and frames, whether timestamps only go
// forward without gaps, and the A/V sync difference. Until is the stop
// condition a case states.
package report

import (
	"errors"
	"fmt"
	"strings"
	"time"
)

// A Track is what the report knows of one track.
type Track struct {
	Codec           string
	SequenceHeaders int
	Frames          int
	// FirstDTS and LastDTS are the decode times of the first and last frame.
	FirstDTS, LastDTS time.Duration
	// Backwards counts frames whose decode time is before the frame before.
	Backwards int
	// MaxGap is the largest step between the decode times of two frames.
	MaxGap time.Duration
}

// Duration is the media time of the frames: from the first to the last, plus
// one average frame.
func (t *Track) Duration() time.Duration {
	if t.Frames < 2 || t.LastDTS <= t.FirstDTS {
		return 0
	}
	d := t.LastDTS - t.FirstDTS
	return d + d/time.Duration(t.Frames-1)
}

// add counts a frame at dts.
func (t *Track) add(codec string, dts time.Duration) {
	if t.Codec == "" {
		t.Codec = codec
	}
	if t.Frames == 0 {
		t.FirstDTS = dts
	} else if dts < t.LastDTS {
		t.Backwards++
	} else if gap := dts - t.LastDTS; gap > t.MaxGap {
		t.MaxGap = gap
	}
	t.LastDTS = dts
	t.Frames++
}

// check describes what is wrong with the track's timestamps, if anything.
func (t *Track) check(name string, maxGap time.Duration) []string {
	var bad []string
	if t.Backwards > 0 {
		bad = append(bad, fmt.Sprintf("%v steps back %v times", name, t.Backwards))
	}
	if t.MaxGap > maxGap {
		bad = append(bad, fmt.Sprintf("%v has a gap of %v, over %v", name, t.MaxGap, maxGap))
	}
	return bad
}

// summary describes the counts and times of the track.
func (t *Track) summary() string {
	return fmt.Sprintf("%v, %v, %v to %v", plural(t.SequenceHeaders, "sequence header"), plural(t.Frames, "frame"), t.FirstDTS, t.LastDTS)
}

// Video is the video track.
type Video struct {
	Track
	Profile       string
	Width, Height int
	Keyframes     int
}

// Audio is the audio track.
type Audio struct {
	Track
	SampleRate, Channels int
}

// A Report describes a stream as a player got it.
type Report struct {
	// Metadata counts the metadata messages, such as onMetaData.
	Metadata int
	// Video and Audio are nil until the stream has a header or a frame of them.
	Video *Video
	Audio *Audio
	// AVSync is the largest distance between the latest video and the latest
	// audio decode time, once both tracks have frames.
	AVSync time.Duration
}

// AddMetadata counts a metadata message.
func (r *Report) AddMetadata() {
	r.Metadata++
}

func (r *Report) video() *Video {
	if r.Video == nil {
		r.Video = &Video{}
	}
	return r.Video
}

func (r *Report) audio() *Audio {
	if r.Audio == nil {
		r.Audio = &Audio{}
	}
	return r.Audio
}

// VideoHeader counts a video sequence header, and takes its codec, profile
// and picture size.
func (r *Report) VideoHeader(codec, profile string, width, height int) {
	v := r.video()
	v.Codec, v.Profile, v.Width, v.Height = codec, profile, width, height
	v.SequenceHeaders++
}

// AudioHeader counts an audio sequence header, and takes its codec, sample
// rate and channels.
func (r *Report) AudioHeader(codec string, sampleRate, channels int) {
	a := r.audio()
	a.Codec, a.SampleRate, a.Channels = codec, sampleRate, channels
	a.SequenceHeaders++
}

// AddVideo counts a video frame at its decode time.
func (r *Report) AddVideo(codec string, dts time.Duration, key bool) {
	v := r.video()
	v.add(codec, dts)
	if key {
		v.Keyframes++
	}
	r.sync()
}

// AddAudio counts an audio frame at its decode time.
func (r *Report) AddAudio(codec string, dts time.Duration) {
	r.audio().add(codec, dts)
	r.sync()
}

func (r *Report) sync() {
	if r.Video == nil || r.Audio == nil || r.Video.Frames == 0 || r.Audio.Frames == 0 {
		return
	}
	d := r.Video.LastDTS - r.Audio.LastDTS
	if d < 0 {
		d = -d
	}
	if d > r.AVSync {
		r.AVSync = d
	}
}

// Duration is the media time of the longer track.
func (r *Report) Duration() time.Duration {
	var d time.Duration
	if r.Video != nil {
		d = r.Video.Duration()
	}
	if r.Audio != nil {
		d = max(d, r.Audio.Duration())
	}
	return d
}

// CheckTimestamps fails when a track's decode times step back, or have a
// step longer than maxGap.
func (r *Report) CheckTimestamps(maxGap time.Duration) error {
	var bad []string
	if r.Video != nil {
		bad = append(bad, r.Video.check("video", maxGap)...)
	}
	if r.Audio != nil {
		bad = append(bad, r.Audio.check("audio", maxGap)...)
	}
	if len(bad) == 0 {
		return nil
	}
	return errors.New(strings.Join(bad, "; "))
}

// String describes the report on one line.
func (r *Report) String() string {
	var parts []string
	if v := r.Video; v != nil {
		desc := v.Codec
		if v.Profile != "" {
			desc += " " + v.Profile
		}
		if v.Width > 0 {
			desc += fmt.Sprintf(" %vx%v", v.Width, v.Height)
		}
		parts = append(parts, fmt.Sprintf("video %v: %v, %v, %v, %v to %v", desc,
			plural(v.SequenceHeaders, "sequence header"), plural(v.Keyframes, "keyframe"), plural(v.Frames, "frame"), v.FirstDTS, v.LastDTS))
	} else {
		parts = append(parts, "no video")
	}
	if a := r.Audio; a != nil {
		desc := a.Codec
		if a.SampleRate > 0 {
			desc += fmt.Sprintf(" %vHz %vch", a.SampleRate, a.Channels)
		}
		parts = append(parts, fmt.Sprintf("audio %v: %v", desc, a.summary()))
	} else {
		parts = append(parts, "no audio")
	}
	parts = append(parts, fmt.Sprintf("metadata %v", r.Metadata), fmt.Sprintf("A/V sync %v", r.AVSync))
	return strings.Join(parts, "; ")
}

// plural is n and the noun, with an s unless n is 1.
func plural(n int, noun string) string {
	if n == 1 {
		return "1 " + noun
	}
	return fmt.Sprintf("%v %vs", n, noun)
}

// Until is when an analyzer stops: every condition that is set must hold.
// The zero Until reads until the stream ends.
type Until struct {
	Metadata                 bool
	VideoHeader, AudioHeader bool
	Keyframes                int
	VideoFrames, AudioFrames int
	// Duration is the media time of the longer track.
	Duration time.Duration
}

// Done reports whether the report meets every condition. It is never true
// for the zero Until.
func (u Until) Done(r *Report) bool {
	if u == (Until{}) {
		return false
	}
	var vh, keys, vframes, ah, aframes int
	if v := r.Video; v != nil {
		vh, keys, vframes = v.SequenceHeaders, v.Keyframes, v.Frames
	}
	if a := r.Audio; a != nil {
		ah, aframes = a.SequenceHeaders, a.Frames
	}
	return (!u.Metadata || r.Metadata > 0) &&
		(!u.VideoHeader || vh > 0) && (!u.AudioHeader || ah > 0) &&
		keys >= u.Keyframes && vframes >= u.VideoFrames && aframes >= u.AudioFrames &&
		r.Duration() >= u.Duration
}

// String describes the conditions, such as "video and audio sequence
// headers, 1 keyframe, 25 video frames".
func (u Until) String() string {
	var parts []string
	if u.Metadata {
		parts = append(parts, "metadata")
	}
	switch {
	case u.VideoHeader && u.AudioHeader:
		parts = append(parts, "video and audio sequence headers")
	case u.VideoHeader:
		parts = append(parts, "video sequence header")
	case u.AudioHeader:
		parts = append(parts, "audio sequence header")
	}
	if u.Keyframes > 0 {
		parts = append(parts, plural(u.Keyframes, "keyframe"))
	}
	if u.VideoFrames > 0 {
		parts = append(parts, plural(u.VideoFrames, "video frame"))
	}
	if u.AudioFrames > 0 {
		parts = append(parts, plural(u.AudioFrames, "audio frame"))
	}
	if u.Duration > 0 {
		parts = append(parts, fmt.Sprintf("%v of media", u.Duration))
	}
	if len(parts) == 0 {
		return "the end of the stream"
	}
	return strings.Join(parts, ", ")
}
