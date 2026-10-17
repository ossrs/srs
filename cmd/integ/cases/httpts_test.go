// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"fmt"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// TestHttpTs_TsMount publishes over RTMP and plays HTTP-TS, on the shared
// TsMount server (see sharedTsMountConfig), which mounts every stream as .ts,
// because a vhost has one HTTP remux mount and Common's is FLV. It has SRS's
// defaults, so the PMT comes from the first frames, hooks, and the HTTPS
// server.
func TestHttpTs_TsMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsMount", sharedTsMountConfig())

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlayHTTPTS(c, srs, media.H264HighAAC, "h264", "aac")
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlayHTTPTS(c, srs, media.HEVCAAC, "hevc", "aac")
	})

	// PublishPlayMp3 plays H.264 with MP3, which TS carries as MPEG audio.
	check.Case(t, "PublishPlayMp3", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.H264HighMP3, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5, AudioFrames: 5})
		checkVideo(c, p.Report, "h264")
		checkAudio(c, p.Report, "mp3", 0)
		// The PMT comes from the first frames, video from the cached GOP, and
		// SRS sends a new one when the MP3 comes.
		c.CheckEqual(p.Programs[len(p.Programs)-1], []string{"h264", "mp3"}, "the last PMT")
	})

	// PublishPlayAudioOnly plays an audio-only stream: the PMT has audio only.
	check.Case(t, "PublishPlayAudioOnly", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.AACOnly, report.Until{AudioHeader: true, AudioFrames: 5})
		checkPlayedAudio(c, p.Report, "aac", 1)
		checkPrograms(c, p, []string{"aac"})
	})

	// PublishPlayVideoOnly plays a video-only stream: the PMT has video only.
	check.Case(t, "PublishPlayVideoOnly", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.H264Only, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		checkVideo(c, p.Report, "h264")
		c.Check(p.Report.Audio == nil, "audio %v, want none", p.Report.Audio)
		checkPrograms(c, p, []string{"h264"})
	})

	// EarlyJoinKeepsAudio has a player join a stream that has sent video
	// frames but no audio frame yet: the first PMT has video only, and when
	// the audio comes SRS sends a new PMT with both, so the audio plays,
	// unlike the FLV header that HTTP-FLV guesses.
	check.Case(t, "EarlyJoinKeepsAudio", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		held, later := earlyJoinTags(c)
		pub := publishTags(c, rtmpURL(srs, stream), held)
		p := remuxPlayerOf(c, srs, stream+".ts", nil)
		c.Must(p.readUntil(report.Until{VideoFrames: 1}, time.Second), "the first frame")
		c.Must(writeTags(pub, later), "publish the rest")

		c.Must(p.readUntil(report.Until{AudioHeader: true, AudioFrames: 5}, time.Second), "play")
		checkVideo(c, p.Report, "h264")
		checkAudio(c, p.Report, "aac", 1)
		c.CheckNoError(p.Report.CheckTimestamps(100*time.Millisecond), "timestamps")
		c.CheckEqual(p.Programs[0], []string{"h264"}, "the first PMT")
		c.CheckEqual(p.Programs[len(p.Programs)-1], []string{"aac", "h264"}, "the PMT after the audio came")
	})

	// LateJoinFromKeyframe has a player join a held stream: it gets the GOP
	// cache, from its keyframe.
	check.Case(t, "LateJoinFromKeyframe", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		tags := readTags(c, media.H264HighAAC, 0, 2500*time.Millisecond)
		publishHeld(c, srs, stream, tags)

		gop := analyzeTags(c, lastGOP(tags))
		p := remuxPlayerOf(c, srs, stream+".ts", nil)
		// A video PES has no length, so the last one comes with the next.
		c.Must(p.readUntil(report.Until{VideoFrames: gop.Video.Frames - 1, AudioFrames: gop.Audio.Frames}, time.Second), "the cached GOP")
		c.CheckEqual(p.Report.Video.LeadingFrames, 0, "frames before the first keyframe")
		c.CheckEqual(p.Report.Video.Keyframes, 1, "keyframes")
	})

	remuxCases(t, srs, tsMount)
	httpsTsCases(t, srs)
}

// TestHttpTs_NoAudio plays HTTP-TS on a variant with guess_has_av and
// has_audio off: the first PMT has the video has_video gives, and audio only
// once it comes.
func TestHttpTs_NoAudio(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxVariant("ts", server.Remux{GuessOff: true, NoAudio: true}))

	// VideoOnlyProgram plays a video-only stream: the PMT has video only.
	check.Case(t, "VideoOnlyProgram", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.H264Only, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		checkPrograms(c, p, []string{"h264"})
	})

	// AudioOnlyProgram plays an audio-only stream: without the guess, the
	// PMT has the video of has_video too, though none comes.
	check.Case(t, "AudioOnlyProgram", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.AACOnly, report.Until{AudioHeader: true, AudioFrames: 5})
		checkPlayedAudio(c, p.Report, "aac", 1)
		checkPrograms(c, p, []string{"aac", "h264"})
	})

	// AudioKept plays H.264 and AAC: has_audio sets only the first PMT, so
	// the audio still plays.
	check.Case(t, "AudioKept", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.H264HighAAC, tsMount.until)
		tsMount.check(c, p.Report)
	})
}

// TestHttpTs_NoVideo plays HTTP-TS on a variant with guess_has_av and
// has_video off: the first PMT has the audio has_audio gives, and video only
// once it comes.
func TestHttpTs_NoVideo(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxVariant("ts", server.Remux{GuessOff: true, NoVideo: true}))

	// AudioOnlyProgram plays an audio-only stream: the PMT has audio only.
	check.Case(t, "AudioOnlyProgram", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.AACOnly, report.Until{AudioHeader: true, AudioFrames: 5})
		checkPrograms(c, p, []string{"aac"})
	})

	// VideoOnlyProgram plays a video-only stream: without the guess, the PMT
	// has the audio of has_audio too, though none comes.
	check.Case(t, "VideoOnlyProgram", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.H264Only, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		checkVideo(c, p.Report, "h264")
		checkPrograms(c, p, []string{"aac", "h264"})
	})

	// VideoKept plays H.264 and AAC: has_video sets only the first PMT, so
	// the video still plays.
	check.Case(t, "VideoKept", check.Normal, func(c *check.C) {
		p := tsPlay(c, srs, media.H264HighAAC, tsMount.until)
		tsMount.check(c, p.Report)
	})
}

// publishPlayHTTPTS publishes a file over RTMP and plays it back over
// HTTP-TS, joining a stream that has already sent 200ms, like the HTTP-FLV
// cases.
func publishPlayHTTPTS(c *check.C, srs *server.Server, file, vcodec, acodec string) {
	checkPlayed(c, tsPlay(c, srs, file, tsMount.until).Report, vcodec, acodec)
}

// tsPlay publishes a file in real time, joining 200ms in, and plays it over
// HTTP-TS until u.
func tsPlay(c *check.C, srs *server.Server, file string, u report.Until) *remuxPlayer {
	c.Helper()
	stream := "live/" + streamName(c)
	startPublisher(c, rtmpURL(srs, stream), media.Path(c, file), 200*time.Millisecond)
	p := remuxPlayerOf(c, srs, stream+".ts", nil)
	c.Must(p.readUntil(u, time.Second), "play")
	return p
}

// checkPrograms checks that every PES an HTTP-TS player read had the PMT
// with the codecs want.
func checkPrograms(c *check.C, p *remuxPlayer, want []string) {
	c.Helper()
	for i, got := range p.Programs {
		if !c.CheckEqual(got, want, fmt.Sprintf("the PMT of PES %d", i)) {
			return
		}
	}
}
