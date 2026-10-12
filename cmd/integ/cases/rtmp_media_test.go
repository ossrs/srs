// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"os"
	"slices"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// rtmpMedia adds the codec cases to TestRtmp_Common: each file of the test
// media that FLV carries is published in real time and played back.
func rtmpMedia(t *testing.T) {
	check.Case(t, "PublishPlayBaseline", check.Normal, func(c *check.C) {
		r := publishPlayRTMP(c, media.H264BaselineAAC, "h264", "aac")
		if r.Video != nil {
			c.CheckEqual(r.Video.Profile, "Baseline", "video profile")
		}
	})

	check.Case(t, "PublishPlayAudioOnly", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.AACOnly), 0)
		r := playUntil(c, url, report.Until{Metadata: true, AudioHeader: true, AudioFrames: 5})
		c.Check(r.Video == nil, "video in an audio-only stream: %v", r)
		checkAudio(c, r, "aac", 1)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	check.Case(t, "PublishPlayVideoOnly", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264Only), 0)
		r := playUntil(c, url, report.Until{Metadata: true, VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		c.Check(r.Audio == nil, "audio in a video-only stream: %v", r)
		checkVideo(c, r, "h264")
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	// PublishPlayMp3 plays H.264 with MP3, which has no sequence header.
	check.Case(t, "PublishPlayMp3", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighMP3), 0)
		r := playUntil(c, url, report.Until{Metadata: true, VideoHeader: true, Keyframes: 1, VideoFrames: 5, AudioFrames: 5})
		checkVideo(c, r, "h264")
		checkAudio(c, r, "mp3", 0)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
		c.Check(r.AVSync <= 100*time.Millisecond, "A/V sync %v, want at most 100ms", r.AVSync)
	})
}

// rtmpLifecycle adds the lifecycle cases to TestRtmp_Common: a player before
// the publisher, many players, a second publisher, unpublish and republish,
// and the API after a client leaves.
func rtmpLifecycle(t *testing.T) {
	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)

		var a flv.Analyzer
		c.Must(readUntil(player, &a, playedUntil, 2*time.Second), "play")
		checkPlayed(c, &a.Report, "h264", "aac")
		c.CheckEqual(a.Report.Metadata, 1, "onMetaData messages")
	})

	check.Case(t, "ManyPlayers", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)

		const n = 8
		reports, errs := make([]*report.Report, n), make([]error, n)
		var wg sync.WaitGroup
		for i := range n {
			wg.Add(1)
			go func() {
				defer wg.Done()
				ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
				defer cancel()
				reports[i], errs[i] = flv.PlayRTMP(ctx, url, nil, playedUntil)
			}()
		}
		wg.Wait()
		for i := range n {
			if c.CheckNoError(errs[i], fmt.Sprintf("player %d", i)) {
				checkPlayed(c, reports[i], "h264", "aac")
			}
		}
	})

	// SecondPublisherRejected publishes a stream that is already published:
	// SRS refuses it without NetStream.Publish.Start, and the first
	// publisher's stream still plays.
	check.Case(t, "SecondPublisherRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)

		second := dialRTMP(c, url, nil)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := second.Publish(ctx, rtmp.FMLE)
		c.Check(err != nil, "the second publisher started, want it refused")
		sts := statusList(second.Statuses())
		c.Check(!slices.Contains(sts, "onStatus status NetStream.Publish.Start"), "second publisher statuses %v, want no NetStream.Publish.Start", sts)
		playChecked(c, url)
	})

	// RepublishNewConnection unpublishes by closing the publisher: a player
	// gets no status and no media, stays connected, and plays the stream again
	// when a new publisher comes, from its sequence headers.
	check.Case(t, "RepublishNewConnection", check.Normal, func(c *check.C) {
		republishCase(c, func(c *check.C, pub *rtmp.Client) {
			pub.Close()
		}, func(c *check.C, url string, pub *rtmp.Client, tags []*media.FLVTag) {
			publishTags(c, url, tags)
		})
	})

	// RepublishSameConnection unpublishes with FCUnpublish and publishes
	// again on the same connection, which SRS serves as a republish.
	check.Case(t, "RepublishSameConnection", check.Normal, func(c *check.C) {
		republishCase(c, func(c *check.C, pub *rtmp.Client) {
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			_, err := pub.Unpublish(ctx)
			c.Must(err, "unpublish")
		}, func(c *check.C, url string, pub *rtmp.Client, tags []*media.FLVTag) {
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			_, err := pub.Publish(ctx, rtmp.FMLE)
			c.Must(err, "publish again on the same connection")
			c.Must(writeTags(pub, tags), "publish the media again")
		})
	})

	// PublisherCloseCleanup closes a publisher: its client and its stream,
	// which has no players, are gone from the API.
	check.Case(t, "PublisherCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := publishOnly(c, rtmpURL(srs, "live/"+stream), rtmp.FMLE)
		h := waitHook(c, srs, "on_publish", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")
		c.CheckEqual(apiCode(c, srs, streamPath), 0, "the stream in the API")

		pub.Close()
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after it closed")
		c.CheckNoError(untilGone(c, srs, streamPath), "the stream after its publisher closed")
	})

	// PlayerCloseCleanup closes a player: its client is gone from the API,
	// and the stream counts only the publisher again.
	check.Case(t, "PlayerCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		url := rtmpURL(srs, "live/"+stream)
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)
		player := playerOf(c, url)
		h := waitHook(c, srs, "on_play", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the player in the API")
		c.CheckEqual(streamClients(c, srs, streamPath), 2, "stream clients with a player")

		player.Close()
		waitHook(c, srs, "on_stop", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")
		c.CheckEqual(streamClients(c, srs, streamPath), 1, "stream clients after the player closed")
	})
}

// republishCase publishes the first second of the test file to a player that
// joined before, unpublishes it with unpublish, and publishes the same media
// again with publish. Between the two, the player must get no status and no
// media; after, it gets the second publish's sequence headers and frames,
// with timestamps that still only go forward.
func republishCase(c *check.C, unpublish func(c *check.C, pub *rtmp.Client), publish func(c *check.C, url string, pub *rtmp.Client, tags []*media.FLVTag)) {
	srs := server.Common(c)
	stream := streamName(c)
	url := rtmpURL(srs, "live/"+stream)
	player := playerOf(c, url)
	tags := readTags(c, media.H264HighAAC, 0, time.Second)
	ref := analyzeTags(c, tags)

	pub := publishTags(c, url, tags)
	var a flv.Analyzer
	u := report.Until{VideoFrames: ref.Video.Frames, AudioFrames: ref.Audio.Frames}
	c.Must(readUntil(player, &a, u, time.Second), "play the first publish")
	statuses := len(player.Statuses())

	unpublish(c, pub)
	waitHook(c, srs, "on_unpublish", stream)
	c.CheckNoError(readNothing(player, 100*time.Millisecond), "the player after the unpublish")
	publish(c, url, pub, tags)
	u = report.Until{VideoFrames: 2 * ref.Video.Frames, AudioFrames: 2 * ref.Audio.Frames}
	c.Must(readUntil(player, &a, u, time.Second), "play the republished stream")
	r := &a.Report
	c.CheckEqual(r.Video.SequenceHeaders, 2, "video sequence headers")
	c.CheckEqual(r.Audio.SequenceHeaders, 2, "audio sequence headers")
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps across the republish")
	c.CheckEqual(statusList(player.Statuses()[statuses:]), []string(nil), "player statuses after the first publish")
}

// rtmpPlayOptions adds the cases of the play settings at their defaults to
// TestRtmp_Common; the variants below set them otherwise.
func rtmpPlayOptions(t *testing.T) {
	// GopCacheLateJoin publishes 2.5s at once and holds the stream: a player
	// that joins then gets the last GOP from the cache, from its keyframe, and
	// nothing before it.
	check.Case(t, "GopCacheLateJoin", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		tags := readTags(c, media.H264HighAAC, 0, 2500*time.Millisecond)
		publishTags(c, url, tags)

		gop := analyzeTags(c, lastGOP(tags))
		player := playerOf(c, url)
		var a flv.Analyzer
		u := report.Until{VideoHeader: true, AudioHeader: true, VideoFrames: gop.Video.Frames, AudioFrames: gop.Audio.Frames}
		c.Must(readUntil(player, &a, u, time.Second), "play the cached GOP")
		c.CheckNoError(readNothing(player, 100*time.Millisecond), "after the cached GOP")
		r := &a.Report
		c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
		c.CheckEqual(r.Video.Keyframes, 1, "keyframes")
		c.CheckEqual(r.Metadata, 1, "onMetaData messages")
	})

	// SequenceHeaderRepeated sends the sequence headers again mid-stream: a
	// player gets both.
	check.Case(t, "SequenceHeaderRepeated", check.Normal, func(c *check.C) {
		r := repeatHeaders(c, server.Common(c), media.H264HighAAC)
		c.CheckEqual(r.Video.SequenceHeaders, 2, "video sequence headers")
		c.CheckEqual(r.Audio.SequenceHeaders, 2, "audio sequence headers")
	})
}

// TestRtmp_GopCacheOff plays on a variant with the GOP cache off.
func TestRtmp_GopCacheOff(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{GopCacheOff: true}))

	// PlayGetsNoCache joins a held stream: the player gets the metadata and
	// sequence headers but no frame, then the live frames from mid-GOP.
	check.Case(t, "PlayGetsNoCache", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		pub := publishTags(c, url, readTags(c, media.H264HighAAC, 0, 2500*time.Millisecond))

		player := playerOf(c, url)
		var a flv.Analyzer
		c.Must(readUntil(player, &a, report.Until{VideoHeader: true, AudioHeader: true}, time.Second), "play the headers")
		c.CheckNoError(readNothing(player, 100*time.Millisecond), "frames without the GOP cache")

		live := readTags(c, media.H264HighAAC, 2500*time.Millisecond, 3500*time.Millisecond)
		c.Must(writeTags(pub, live), "publish more")
		ref := analyzeTags(c, live)
		c.Must(readUntil(player, &a, report.Until{VideoFrames: ref.Video.Frames, AudioFrames: ref.Audio.Frames}, time.Second), "play the live frames")
		c.Check(a.Report.Video.LeadingFrames > 0, "the player started at a keyframe, want the live frames from mid-GOP: %v", &a.Report)
	})
}

// TestRtmp_GopCacheMaxFrames plays on a variant whose GOP cache holds at most
// 10 messages, fewer than a GOP of the test media.
func TestRtmp_GopCacheMaxFrames(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{GopCacheMaxFrames: 10}))

	// CacheLimited joins a held stream: the player gets fewer cached frames
	// than the GOP, and at most 10 messages.
	check.Case(t, "CacheLimited", check.Normal, func(c *check.C) {
		r, gop := joinLimitedCache(c, srs)
		frames := framesOf(r)
		c.Check(frames <= 10, "%d cached frames, want at most 10: %v", frames, r)
		c.Check(r.Video == nil || r.Video.Frames < gop.Video.Frames, "the whole GOP of %d video frames was cached: %v", gop.Video.Frames, r)
	})

	// CacheStartsAtKeyframe joins the same held stream: what the cache gives
	// a late player starts at a keyframe, or is empty. SRS clears an
	// overflowing cache, then caches again from the next video frame, which
	// is not a keyframe, so the player starts mid-GOP.
	check.Case(t, "CacheStartsAtKeyframe", check.Normal, func(c *check.C) {
		check.KnownBug(c, "after gop_cache_max_frames clears the cache, it caches again from a non-keyframe")
		r, _ := joinLimitedCache(c, srs)
		c.Check(r.Video == nil || r.Video.LeadingFrames == 0, "the cached frames start before a keyframe: %v", r)
	})
}

// joinLimitedCache publishes 2.5s at once and holds the stream, then plays it
// late and reads what the GOP cache gives. It returns the player's report
// and the report of the last GOP published.
func joinLimitedCache(c *check.C, srs *server.Server) (*report.Report, *report.Report) {
	c.Helper()
	url := rtmpURL(srs, "live/"+streamName(c))
	tags := readTags(c, media.H264HighAAC, 0, 2500*time.Millisecond)
	publishTags(c, url, tags)

	player := playerOf(c, url)
	var a flv.Analyzer
	c.Must(readUntil(player, &a, report.Until{VideoHeader: true, AudioHeader: true}, time.Second), "play the headers")
	c.Must(readIdle(player, &a, report.Until{}, 100*time.Millisecond), "the cached frames")
	return &a.Report, analyzeTags(c, lastGOP(tags))
}

// TestTimer_Rtmp_QueueDefault plays on a variant with the default play queue,
// 30s. Its case pauses a player, and SRS reads an unpause only after the 500ms
// sleep of a paused player, so it is a timer case.
func TestTimer_Rtmp_QueueDefault(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{}))

	// QueueKeepsBurst sends 20s at once to a paused player: the queue keeps
	// all of it.
	check.Case(t, "QueueKeepsBurst", check.Timer, func(c *check.C) {
		r, ref := queueBurst(c, srs)
		c.CheckEqual(r.Video.Frames, ref.Video.Frames, "video frames")
		c.CheckEqual(r.Audio.Frames, ref.Audio.Frames, "audio frames")
	})
}

// TestTimer_Rtmp_QueueLength plays on a variant whose player queue holds 1s, a
// timer case for the pause like TestTimer_Rtmp_QueueDefault.
func TestTimer_Rtmp_QueueLength(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{QueueLength: time.Second}))

	// QueueShrinks sends 20s at once to a paused player: SRS drops what is
	// over 1s, so the player gets much less.
	check.Case(t, "QueueShrinks", check.Timer, func(c *check.C) {
		r, ref := queueBurst(c, srs)
		c.Check(r.Video == nil || r.Video.Frames < ref.Video.Frames-media.FPS, "the player got %v, want at least 1s of the %d video frames dropped", r, ref.Video.Frames)
	})
}

// TestRtmp_ReduceSequenceHeader plays on a variant with reduce_sequence_header
// on.
func TestRtmp_ReduceSequenceHeader(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{ReduceSequenceHeader: true}))

	// DuplicateDropped sends the same sequence headers again: the player gets
	// each once.
	check.Case(t, "DuplicateDropped", check.Normal, func(c *check.C) {
		r := repeatHeaders(c, srs, media.H264HighAAC)
		c.CheckEqual(r.Video.SequenceHeaders, 1, "video sequence headers")
		c.CheckEqual(r.Audio.SequenceHeaders, 1, "audio sequence headers")
	})

	// ChangedKept sends another video sequence header, the baseline file's:
	// it isn't a duplicate, so the player gets it.
	check.Case(t, "ChangedKept", check.Normal, func(c *check.C) {
		r := repeatHeaders(c, srs, media.H264BaselineAAC)
		c.CheckEqual(r.Video.SequenceHeaders, 2, "video sequence headers")
		c.CheckEqual(r.Video.Profile, "Baseline", "video profile of the last header")
		c.CheckEqual(r.Audio.SequenceHeaders, 1, "audio sequence headers")
	})
}

// queueBurst plays a stream before it is published and pauses, so SRS queues
// the stream for the player and sends nothing, while 20s of the test file is
// published at once. The FMLE unpublish is answered only after SRS has taken
// every message before it, so the player then unpauses and reads what its
// queue kept, until it has every frame or nothing more comes. It returns the player's
// report and the report of what was published.
func queueBurst(c *check.C, srs *server.Server) (*report.Report, *report.Report) {
	c.Helper()
	url := rtmpURL(srs, "live/"+streamName(c))
	player := playerOf(c, url)
	tags := readTags(c, media.H264HighAAC, 0, 20*time.Second)
	ref := analyzeTags(c, tags)
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	_, err := player.Pause(ctx, true)
	c.Must(err, "pause")

	free := srs.Burst(c)
	pub := publishTags(c, url, tags)
	_, err = pub.Unpublish(ctx)
	free()
	c.Must(err, "unpublish")
	_, err = player.Pause(ctx, false)
	c.Must(err, "unpause")

	var a flv.Analyzer
	u := report.Until{VideoFrames: ref.Video.Frames, AudioFrames: ref.Audio.Frames}
	c.Must(readIdle(player, &a, u, 200*time.Millisecond), "play")
	return &a.Report, ref
}

// repeatHeaders plays a stream before it is published, then publishes the
// first second of the test file, the video and audio sequence headers of
// file, and the next second. It returns the player's report once it has every
// frame.
func repeatHeaders(c *check.C, srs *server.Server, file string) *report.Report {
	c.Helper()
	url := rtmpURL(srs, "live/"+streamName(c))
	player := playerOf(c, url)
	first := readTags(c, media.H264HighAAC, 0, time.Second)
	second := readTags(c, media.H264HighAAC, time.Second, 2*time.Second)
	video, audio := headerTags(c, file)
	video.Timestamp, audio.Timestamp = 1000, 1000

	pub := publishTags(c, url, first)
	c.Must(writeTags(pub, []*media.FLVTag{video, audio}), "publish the headers again")
	c.Must(writeTags(pub, second), "publish the next second")

	ref := analyzeTags(c, append(append([]*media.FLVTag(nil), first...), second...))
	var a flv.Analyzer
	c.Must(readUntil(player, &a, report.Until{VideoFrames: ref.Video.Frames, AudioFrames: ref.Audio.Frames}, time.Second), "play")
	return &a.Report
}

// readTags reads the tags of a test file whose timestamps are in [from, to).
func readTags(c *check.C, file string, from, to time.Duration) []*media.FLVTag {
	c.Helper()
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	defer f.Close()
	r, err := media.NewFLVReader(f)
	c.Must(err, "read the media")
	var tags []*media.FLVTag
	for {
		tag, err := r.ReadTag()
		c.Must(err, "read the media")
		ts := time.Duration(tag.Timestamp) * time.Millisecond
		if ts >= to {
			return tags
		}
		if ts >= from {
			tags = append(tags, tag)
		}
	}
}

// headerTags returns the first video and audio sequence headers of a test
// file.
func headerTags(c *check.C, file string) (video, audio *media.FLVTag) {
	c.Helper()
	for _, tag := range readTags(c, file, 0, time.Second) {
		switch tag.Type {
		case media.TagVideo:
			if v, err := media.ParseVideo(tag.Data); err == nil && v.SequenceHeader && video == nil {
				video = tag
			}
		case media.TagAudio:
			if a, err := media.ParseAudio(tag.Data); err == nil && a.SequenceHeader && audio == nil {
				audio = tag
			}
		}
	}
	if video == nil || audio == nil {
		c.Fatalf("%s has no video or audio sequence header", file)
	}
	return video, audio
}

// lastGOP is the tags from the last video keyframe on.
func lastGOP(tags []*media.FLVTag) []*media.FLVTag {
	last := 0
	for i, tag := range tags {
		if tag.Type != media.TagVideo {
			continue
		}
		if v, err := media.ParseVideo(tag.Data); err == nil && v.Key && !v.SequenceHeader {
			last = i
		}
	}
	return tags[last:]
}

// analyzeTags is the report of tags, as a player that gets them all would
// have it.
func analyzeTags(c *check.C, tags []*media.FLVTag) *report.Report {
	c.Helper()
	var a flv.Analyzer
	for _, tag := range tags {
		c.Must(a.Add(tag), "analyze the media")
	}
	if a.Report.Video == nil || a.Report.Audio == nil {
		c.Fatalf("the media has no video or audio frames: %v", &a.Report)
	}
	return &a.Report
}

// framesOf counts the video and audio frames of a report.
func framesOf(r *report.Report) int {
	n := 0
	if r.Video != nil {
		n += r.Video.Frames
	}
	if r.Audio != nil {
		n += r.Audio.Frames
	}
	return n
}

// publishTags publishes tags at once, without pacing, and keeps the stream
// published until the case ends.
func publishTags(c *check.C, url string, tags []*media.FLVTag) *rtmp.Client {
	c.Helper()
	pub := publishOnly(c, url, rtmp.FMLE)
	c.Must(writeTags(pub, tags), "publish the media")
	return pub
}

// writeTags writes tags on a published stream.
func writeTags(pub *rtmp.Client, tags []*media.FLVTag) error {
	for _, tag := range tags {
		if err := pub.WriteTag(tag); err != nil {
			return err
		}
	}
	return nil
}

// playerOf plays a stream, published or not, until the case ends, and
// returns the player once SRS has answered NetStream.Play.Start.
func playerOf(c *check.C, url string) *rtmp.Client {
	c.Helper()
	player := dialRTMP(c, url, nil)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := player.Play(ctx)
	c.Must(err, "play")
	return player
}

// playUntil plays a stream until u, and returns the report.
func playUntil(c *check.C, url string, u report.Until) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	r, err := flv.PlayRTMP(ctx, url, nil, u)
	c.Must(err, "play")
	return r
}

// A tagPlayer is a player that reads FLV tags with a deadline: an RTMP
// client or an HTTP-FLV player.
type tagPlayer interface {
	ReadTag() (*media.FLVTag, error)
	SetDeadline(t time.Time) error
}

// readUntil adds a player's tags to a until its report meets u, or fails
// after timeout. The report carries on from the tags a read before.
func readUntil(player tagPlayer, a *flv.Analyzer, u report.Until, timeout time.Duration) error {
	player.SetDeadline(time.Now().Add(timeout))
	defer player.SetDeadline(time.Time{})
	for !u.Done(&a.Report) {
		tag, err := player.ReadTag()
		if err != nil {
			return fmt.Errorf("%w, before %v: %v", err, u, &a.Report)
		}
		if err := a.Add(tag); err != nil {
			return err
		}
	}
	return nil
}

// readNothing fails when a player gets media or metadata within d. Other
// data messages, such as the |RtmpSampleAccess SRS sends when play starts,
// don't count.
func readNothing(player tagPlayer, d time.Duration) error {
	player.SetDeadline(time.Now().Add(d))
	defer player.SetDeadline(time.Time{})
	for {
		tag, err := player.ReadTag()
		if isTimeout(err) {
			return nil
		} else if err != nil {
			return err
		}
		if tag.Type != media.TagScript || flv.IsMetadata(tag.Data) {
			return fmt.Errorf("got a tag of type %d at %dms, want none within %v", tag.Type, tag.Timestamp, d)
		}
	}
}

// readIdle adds a player's tags to a until its report meets u, or until no
// tag comes for idle.
func readIdle(player tagPlayer, a *flv.Analyzer, u report.Until, idle time.Duration) error {
	defer player.SetDeadline(time.Time{})
	for !u.Done(&a.Report) {
		player.SetDeadline(time.Now().Add(idle))
		tag, err := player.ReadTag()
		if isTimeout(err) {
			return nil
		} else if err != nil {
			return err
		}
		if err := a.Add(tag); err != nil {
			return err
		}
	}
	return nil
}

// isTimeout reports whether err is a network timeout.
func isTimeout(err error) bool {
	var ne net.Error
	return errors.As(err, &ne) && ne.Timeout()
}

// streamOf is the stream name of an rtmp:// URL.
func streamOf(url string) string {
	u, _ := rtmp.ParseURL(url)
	return u.Stream
}

// apiCode is the code of an HTTP API answer.
func apiCode(c *check.C, srs *server.Server, path string) int {
	c.Helper()
	var v struct {
		Code int `json:"code"`
	}
	apiGet(c, srs, path, &v)
	return v.Code
}

// streamClients is the clients of a stream in the HTTP API.
func streamClients(c *check.C, srs *server.Server, path string) int {
	c.Helper()
	var v struct {
		Stream struct {
			Clients int `json:"clients"`
		} `json:"stream"`
	}
	apiGet(c, srs, path, &v)
	return v.Stream.Clients
}

// apiGet decodes the answer of an HTTP API path into v.
func apiGet(c *check.C, srs *server.Server, path string, v any) {
	c.Helper()
	res, err := httpClient.Get(srs.APIURL(path))
	c.Must(err, "GET "+path)
	defer res.Body.Close()
	c.Must(json.NewDecoder(res.Body).Decode(v), "decode "+path)
}

// untilGone polls an HTTP API path until its code is not 0, for at most 1s.
func untilGone(c *check.C, srs *server.Server, path string) error {
	c.Helper()
	deadline := time.Now().Add(time.Second)
	for apiCode(c, srs, path) == 0 {
		if time.Now().After(deadline) {
			return fmt.Errorf("%s still answers code 0 after 1s", path)
		}
		time.Sleep(10 * time.Millisecond)
	}
	return nil
}
