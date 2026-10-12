// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"testing"
	"time"

	"srs-integ/internal/audio"
	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// TestHttpAudio_AacMount publishes over RTMP and plays the audio as raw AAC
// over HTTP, on a variant that mounts every stream as .aac.
func TestHttpAudio_AacMount(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxConfig("aac"))

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		r := publishPlayHTTPAudio(c, srs, media.H264HighAAC, "aac", report.Until{AudioHeader: true, AudioFrames: 10})
		checkPlayedAudio(c, r, "aac", 1)
	})
}

// TestHttpAudio_Mp3Mount publishes over RTMP and plays the audio as MP3 over
// HTTP, on a variant that mounts every stream as .mp3.
func TestHttpAudio_Mp3Mount(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxConfig("mp3"))

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		r := publishPlayHTTPAudio(c, srs, media.H264HighMP3, "mp3", report.Until{AudioFrames: 10})
		checkPlayedAudio(c, r, "mp3", 0)
	})
}

// publishPlayHTTPAudio publishes a file over RTMP and plays its audio back
// over HTTP as ext, joining a stream that has already sent 200ms.
func publishPlayHTTPAudio(c *check.C, srs *server.Server, file, ext string, u report.Until) *report.Report {
	stream := "live/" + streamName(c)
	startPublisher(c, "rtmp://"+srs.Addr(server.RTMP)+"/"+stream, media.Path(c, file), 200*time.Millisecond)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	r, err := audio.PlayHTTP(ctx, nil, "http://"+srs.Addr(server.HTTPServer)+"/"+stream+"."+ext, u)
	c.Must(err, "play")
	return r
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
