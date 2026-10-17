// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package report

import (
	"strings"
	"testing"
	"time"
)

const ms = time.Millisecond

// stream feeds a report the way a player gets a stream: metadata, both
// sequence headers, then video at 25 fps and audio every 23ms, interleaved.
func stream(r *Report, video, audio int) {
	r.AddMetadata()
	r.VideoHeader("h264", "High", 768, 320)
	r.AudioHeader("aac", 44100, 2)
	vi, ai := 0, 0
	for vi < video || ai < audio {
		vts, ats := time.Duration(vi)*40*ms, time.Duration(ai)*23*ms
		if vi < video && (ai >= audio || vts <= ats) {
			r.AddVideo("h264", vts, vi%25 == 0)
			vi++
		} else {
			r.AddAudio("aac", ats)
			ai++
		}
	}
}

func TestReportCounts(t *testing.T) {
	var r Report
	stream(&r, 50, 87)

	if r.Metadata != 1 {
		t.Errorf("metadata %v, want 1", r.Metadata)
	}
	v := r.Video
	if v == nil {
		t.Fatal("no video")
	}
	if v.Codec != "h264" || v.Profile != "High" || v.Width != 768 || v.Height != 320 {
		t.Errorf("video %v %v %vx%v, want h264 High 768x320", v.Codec, v.Profile, v.Width, v.Height)
	}
	if v.SequenceHeaders != 1 || v.Keyframes != 2 || v.Frames != 50 {
		t.Errorf("video %v headers %v keyframes %v frames, want 1 2 50", v.SequenceHeaders, v.Keyframes, v.Frames)
	}
	if v.FirstDTS != 0 || v.LastDTS != 1960*ms || v.Duration() != 2*time.Second {
		t.Errorf("video %v to %v lasts %v, want 0s to 1.96s lasting 2s", v.FirstDTS, v.LastDTS, v.Duration())
	}
	if v.Backwards != 0 || v.MaxGap != 40*ms {
		t.Errorf("video backwards %v max gap %v, want 0 and 40ms", v.Backwards, v.MaxGap)
	}

	a := r.Audio
	if a == nil {
		t.Fatal("no audio")
	}
	if a.Codec != "aac" || a.SampleRate != 44100 || a.Channels != 2 || a.SequenceHeaders != 1 || a.Frames != 87 {
		t.Errorf("audio %+v, want aac 44100Hz 2ch, 1 header, 87 frames", a)
	}
	if a.MaxGap != 23*ms || a.Backwards != 0 {
		t.Errorf("audio max gap %v backwards %v, want 23ms and 0", a.MaxGap, a.Backwards)
	}
	if r.AVSync > 40*ms {
		t.Errorf("A/V sync %v, want at most one video frame", r.AVSync)
	}
	if err := r.CheckTimestamps(100 * ms); err != nil {
		t.Errorf("timestamps: %v", err)
	}
}

func TestReportEmpty(t *testing.T) {
	var r Report
	if r.Video != nil || r.Audio != nil || r.Duration() != 0 {
		t.Errorf("empty report has %+v", r)
	}
	if s := r.String(); !strings.Contains(s, "no video") || !strings.Contains(s, "no audio") {
		t.Errorf("empty report %q, want no video and no audio", s)
	}
	if err := r.CheckTimestamps(time.Second); err != nil {
		t.Errorf("empty report timestamps: %v", err)
	}
}

func TestReportBackwardsAndGaps(t *testing.T) {
	var r Report
	r.AddVideo("h264", 0, true)
	r.AddVideo("h264", 40*ms, false)
	r.AddVideo("h264", 20*ms, false)  // Back.
	r.AddVideo("h264", 520*ms, false) // A 500ms gap.
	r.AddAudio("aac", 0)
	r.AddAudio("aac", 23*ms)

	if r.Video == nil || r.Audio == nil {
		t.Fatalf("report %+v, want video and audio", r)
	}
	if r.Video.Backwards != 1 || r.Video.MaxGap != 500*ms {
		t.Errorf("backwards %v max gap %v, want 1 and 500ms", r.Video.Backwards, r.Video.MaxGap)
	}
	err := r.CheckTimestamps(100 * ms)
	if err == nil || !strings.Contains(err.Error(), "back") || !strings.Contains(err.Error(), "gap") {
		t.Errorf("timestamps error %v, want one naming the step back and the gap", err)
	}
	if r.Audio.Backwards != 0 {
		t.Errorf("audio backwards %v, want 0", r.Audio.Backwards)
	}
}

// TestReportAVSync checks the largest distance between the latest video and
// the latest audio timestamp, once both have frames.
func TestReportAVSync(t *testing.T) {
	var r Report
	r.AddVideo("h264", 0, true)
	r.AddVideo("h264", 40*ms, false)
	r.AddVideo("h264", 80*ms, false)
	if r.AVSync != 0 {
		t.Errorf("A/V sync %v before any audio, want 0", r.AVSync)
	}
	r.AddAudio("aac", 300*ms) // 220ms ahead of the video.
	r.AddVideo("h264", 120*ms, false)
	r.AddAudio("aac", 130*ms)
	if r.AVSync != 220*ms {
		t.Errorf("A/V sync %v, want 220ms", r.AVSync)
	}
}

// TestReportCodecFromFrames fills the codec from the first frame when there is
// no sequence header, such as MP3.
func TestReportCodecFromFrames(t *testing.T) {
	var r Report
	r.AddAudio("mp3", 0)
	r.AddAudio("mp3", 26*ms)
	if r.Audio == nil || r.Audio.Codec != "mp3" || r.Audio.SequenceHeaders != 0 || r.Audio.Frames != 2 {
		t.Errorf("audio %+v, want mp3 with 2 frames and no header", r.Audio)
	}
	if r.Duration() != 52*ms {
		t.Errorf("duration %v, want 52ms", r.Duration())
	}
}

func TestReportString(t *testing.T) {
	var r Report
	stream(&r, 25, 44)
	s := r.String()
	for _, want := range []string{"h264 High 768x320", "1 sequence header", "1 keyframe", "25 frames", "aac 44100Hz 2ch", "44 frames", "metadata 1", "A/V sync"} {
		if !strings.Contains(s, want) {
			t.Errorf("report %q, want it to contain %q", s, want)
		}
	}
}

func TestUntil(t *testing.T) {
	var r Report
	stream(&r, 25, 44)

	cases := []struct {
		name string
		u    Until
		done bool
	}{
		{"Zero", Until{}, false},
		{"Headers", Until{VideoHeader: true, AudioHeader: true, Metadata: true}, true},
		{"Frames", Until{Keyframes: 1, VideoFrames: 25, AudioFrames: 44}, true},
		{"MoreVideo", Until{VideoFrames: 26}, false},
		{"MoreKeyframes", Until{Keyframes: 2}, false},
		{"MoreAudio", Until{AudioFrames: 45}, false},
		{"Duration", Until{Duration: time.Second}, true},
		{"LongerDuration", Until{Duration: 1100 * ms}, false},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := tc.u.Done(&r); got != tc.done {
				t.Errorf("done %v, want %v", got, tc.done)
			}
		})
	}

	var empty Report
	if (Until{VideoHeader: true}).Done(&empty) || (Until{AudioHeader: true}).Done(&empty) || (Until{Metadata: true}).Done(&empty) {
		t.Errorf("an empty report meets a header condition")
	}
}

func TestUntilString(t *testing.T) {
	u := Until{Metadata: true, VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 25, AudioFrames: 2, Duration: time.Second}
	want := "metadata, video and audio sequence headers, 1 keyframe, 25 video frames, 2 audio frames, 1s of media"
	if s := u.String(); s != want {
		t.Errorf("until %q, want %q", s, want)
	}
	if s := (Until{AudioHeader: true, Keyframes: 2}).String(); s != "audio sequence header, 2 keyframes" {
		t.Errorf("until %q", s)
	}
	if s := (Until{}).String(); s != "the end of the stream" {
		t.Errorf("zero until %q, want the end of the stream", s)
	}
}

// TestReportLeadingFrames counts the video frames before the first keyframe,
// which a player that joins a stream mid-GOP gets and can't decode.
func TestReportLeadingFrames(t *testing.T) {
	var r Report
	r.AddVideo("h264", 0, false)
	r.AddVideo("h264", 40*ms, false)
	r.AddVideo("h264", 80*ms, true)
	r.AddVideo("h264", 120*ms, false)
	r.AddVideo("h264", 160*ms, true)
	if r.Video == nil || r.Video.LeadingFrames != 2 {
		t.Errorf("video %+v, want 2 leading frames", r.Video)
	}

	var keyFirst Report
	stream(&keyFirst, 30, 0)
	if keyFirst.Video.LeadingFrames != 0 {
		t.Errorf("leading frames %v of a stream that starts at a keyframe, want 0", keyFirst.Video.LeadingFrames)
	}
}

// TestReportUnordered counts the frames whose decode time is before the latest
// decode time of the frames before, of either track: a stream whose tracks
// interleave out of time order.
func TestReportUnordered(t *testing.T) {
	var ordered Report
	stream(&ordered, 30, 40)
	if ordered.Unordered != 0 {
		t.Errorf("unordered %v of an interleaved stream in time order, want 0", ordered.Unordered)
	}

	// Audio sent 100ms late: each audio frame comes after video up to 100ms
	// past it.
	var late Report
	late.AddVideo("h264", 0, true)
	late.AddVideo("h264", 40*ms, false)
	late.AddVideo("h264", 80*ms, false)
	late.AddAudio("aac", 0)
	late.AddVideo("h264", 120*ms, false)
	late.AddAudio("aac", 23*ms)
	late.AddAudio("aac", 46*ms)
	late.AddVideo("h264", 160*ms, false)
	if late.Unordered != 3 {
		t.Errorf("unordered %v, want 3, the audio frames", late.Unordered)
	}
	if late.Video.Backwards != 0 || late.Audio.Backwards != 0 {
		t.Errorf("backwards video %v audio %v, want 0: each track is in order", late.Video.Backwards, late.Audio.Backwards)
	}
}
