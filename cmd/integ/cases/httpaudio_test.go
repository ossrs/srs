// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// TestHttpAudio_AacMount publishes over RTMP and plays the audio as raw AAC
// over HTTP, on the shared AacMount server, which mounts every stream as
// .aac, with hooks.
func TestHttpAudio_AacMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "AacMount", sharedAacMountConfig())

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		checkPlayedAudio(c, audioPlay(c, srs, media.H264HighAAC, aacMount), "aac", 1)
	})

	check.Case(t, "PublishPlayAudioOnly", check.Normal, func(c *check.C) {
		checkPlayedAudio(c, audioPlay(c, srs, media.AACOnly, aacMount), "aac", 1)
	})

	// Mp3StreamEnds plays a stream whose audio is MP3: the AAC mount can't
	// carry it, so the stream ends without a frame.
	check.Case(t, "Mp3StreamEnds", check.Normal, func(c *check.C) {
		wrongCodecEnds(c, srs, media.H264HighMP3, ".aac")
	})

	// NoCacheLateJoin has a player join a held stream: the audio mounts have
	// no GOP cache, and the fast cache is off by default, so nothing comes.
	check.Case(t, "NoCacheLateJoin", check.Normal, func(c *check.C) {
		noCacheLateJoin(c, srs, media.H264HighAAC, ".aac")
	})

	remuxCases(t, srs, aacMount)
}

// TestHttpAudio_Mp3Mount publishes over RTMP and plays the audio as MP3 over
// HTTP, on the shared Mp3Mount server, which mounts every stream as .mp3,
// with hooks.
func TestHttpAudio_Mp3Mount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Mp3Mount", sharedMp3MountConfig())

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		checkPlayedAudio(c, audioPlay(c, srs, media.H264HighMP3, mp3Mount), "mp3", 0)
	})

	// AacStreamEnds plays a stream whose audio is AAC: the MP3 mount can't
	// carry it, so the stream ends without a frame.
	check.Case(t, "AacStreamEnds", check.Normal, func(c *check.C) {
		wrongCodecEnds(c, srs, media.H264HighAAC, ".mp3")
	})

	check.Case(t, "NoCacheLateJoin", check.Normal, func(c *check.C) {
		noCacheLateJoin(c, srs, media.H264HighMP3, ".mp3")
	})

	remuxCases(t, srs, mp3Mount)
}

// audioPlay publishes a file in real time, joining 200ms in, and plays its
// audio over HTTP from the mount m until m.until.
func audioPlay(c *check.C, srs *server.Server, file string, m remuxMount) *report.Report {
	c.Helper()
	stream := "live/" + streamName(c)
	startPublisher(c, rtmpURL(srs, stream), media.Path(c, file), 200*time.Millisecond)
	p := remuxPlayerOf(c, srs, stream+"."+m.ext, nil)
	c.Must(p.readUntil(m.until, time.Second), "play")
	return p.Report
}

// wrongCodecEnds publishes a file whose audio the mount ext can't carry, and
// checks that its player gets no frame before the stream ends.
func wrongCodecEnds(c *check.C, srs *server.Server, file, ext string) {
	c.Helper()
	stream := "live/" + streamName(c)
	startPublisher(c, rtmpURL(srs, stream), media.Path(c, file), 200*time.Millisecond)
	p := remuxPlayerOf(c, srs, stream+ext, nil)
	c.CheckNoError(p.untilEnds(time.Second), "the player")
	c.Check(p.Report.Audio == nil, "audio %v, want none", p.Report.Audio)
}

// noCacheLateJoin publishes 2.5s of a file at once and holds the stream, and
// checks that a player that joins gets nothing for 100ms.
func noCacheLateJoin(c *check.C, srs *server.Server, file, ext string) {
	c.Helper()
	stream := "live/" + streamName(c)
	publishHeld(c, srs, stream, readTags(c, file, 0, 2500*time.Millisecond))
	p := remuxPlayerOf(c, srs, stream+ext, nil)
	c.CheckNoError(p.readNothing(100*time.Millisecond), "a late player")
}

// checkPlayedAudio checks the report of an audio-only player: no video, the
// codec, the source's 44.1 kHz stereo, its sequence headers, and timestamps
// that only go forward, without gaps.
func checkPlayedAudio(c *check.C, r *report.Report, codec string, headers int) {
	c.Helper()
	c.Check(r.Video == nil, "video %v, want none", r)
	a := r.Audio
	if !c.Check(a != nil, "no audio: %v", r) {
		return
	}
	c.CheckEqual(a.Codec, codec, "audio codec")
	c.Check(a.SampleRate == 44100 && a.Channels == 2, "audio %vHz %vch, want 44100Hz 2ch", a.SampleRate, a.Channels)
	c.CheckEqual(a.SequenceHeaders, headers, "audio sequence headers")
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
}

// TestTimer_HttpAudio_AacFastCache plays raw AAC on a variant with a 1s fast
// cache, which SRS fills on a coroutine that polls the stream every 500ms.
func TestTimer_HttpAudio_AacFastCache(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxVariant("aac", server.Remux{FastCache: time.Second}))

	check.Case(t, "LateJoinGetsCache", check.Timer, func(c *check.C) {
		fastCacheLateJoin(c, srs, media.H264HighAAC, ".aac", 1)
	})
}

// TestTimer_HttpAudio_Mp3FastCache is TestTimer_HttpAudio_AacFastCache for
// MP3.
func TestTimer_HttpAudio_Mp3FastCache(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxVariant("mp3", server.Remux{FastCache: time.Second}))

	check.Case(t, "LateJoinGetsCache", check.Timer, func(c *check.C) {
		fastCacheLateJoin(c, srs, media.H264HighMP3, ".mp3", 0)
	})
}

// fastCacheLateJoin publishes 900ms of a file at once, less than the 1s fast
// cache, and holds the stream. Once the cache has it, a player that joins
// gets every audio frame of it at once, and nothing more, where without the
// cache it gets nothing (NoCacheLateJoin). Until then a player gets nothing,
// so a new one joins every 100ms, for at most 2s.
func fastCacheLateJoin(c *check.C, srs *server.Server, file, ext string, headers int) {
	c.Helper()
	stream := "live/" + streamName(c)
	tags := readTags(c, file, 0, 900*time.Millisecond)
	publishHeld(c, srs, stream, tags)
	want := analyzeTags(c, tags).Audio.Frames

	deadline := time.Now().Add(2 * time.Second)
	for {
		p := remuxPlayerOf(c, srs, stream+ext, nil)
		if err := p.readUntil(report.Until{AudioFrames: 1}, 100*time.Millisecond); err == nil {
			c.CheckNoError(p.readUntil(report.Until{AudioFrames: want}, time.Second), "the cached audio")
			c.CheckNoError(p.readNothing(100*time.Millisecond), "after the cached audio")
			c.CheckEqual(p.Report.Audio.SequenceHeaders, headers, "audio sequence headers")
			return
		}
		p.close()
		if time.Now().After(deadline) {
			c.Fatalf("no player got the cached audio within 2s")
		}
	}
}
