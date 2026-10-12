// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"fmt"
	"slices"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// absBase is the timestamp an absolute-time publisher starts at, such as an
// encoder that stamps its stream with the time of day, here 100s.
const absBase = 100000

// rtmpTimestamps adds the timestamp and latency cases of the play defaults to
// TestRtmp_Common: time_jitter full, atc and mix_correct off, min_latency on.
func rtmpTimestamps(t *testing.T) {
	// JitterFullStartsAtZero publishes a stream that starts at 100s to a
	// player that joined before: the player's media starts near zero, with
	// the publisher's steps between messages.
	check.Case(t, "JitterFullStartsAtZero", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := shiftTags(readTags(c, media.H264HighAAC, 0, time.Second), absBase)
		publishTags(c, url, tags)

		got := readMedia(c, player, tags)
		pub, play := avTags(tags), avTags(got)
		c.Check(play[0].Timestamp < 100, "the first A/V message at %dms, want under 100ms", play[0].Timestamp)
		for i := range play {
			c.CheckEqual(play[i].Timestamp-play[0].Timestamp, pub[i].Timestamp-pub[0].Timestamp, fmt.Sprintf("step to A/V message %d", i))
		}
	})

	// JitterFullFixesJumps publishes a stream that jumps 10s forward, then 5s
	// back: the player's timestamps only go forward, without a gap.
	check.Case(t, "JitterFullFixesJumps", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := jumpTags(c, true)
		publishTags(c, url, tags)

		r := analyzeTags(c, readMedia(c, player, tags))
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps across the jumps")
	})

	// InterleaveKept publishes audio 200ms after the video of the same time:
	// without mix_correct, the player gets the messages in the order sent.
	check.Case(t, "InterleaveKept", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := lateAudio(readTags(c, media.H264HighAAC, 0, time.Second), 200)
		publishTags(c, url, tags)

		got := readMedia(c, player, tags)
		c.CheckEqual(tagTimes(avTags(got)), tagTimes(avTags(tags)), "A/V messages")
		r := analyzeTags(c, got)
		c.Check(r.Unordered > 0, "the player got the tracks in time order, want the order sent: %v", r)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps of each track")
	})

	// MinLatencyNoHold publishes the headers and one keyframe to a player
	// that joined before: with min_latency, SRS sends them at once, though
	// they are fewer than the 8 messages SRS merges by default.
	check.Case(t, "MinLatencyNoHold", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		publishTags(c, url, firstKeyframe(c, media.H264HighAAC))

		var a flv.Analyzer
		u := report.Until{Metadata: true, VideoHeader: true, AudioHeader: true, Keyframes: 1}
		c.CheckNoError(readUntil(player, &a, u, 200*time.Millisecond), "the headers and keyframe at once")
	})
}

// TestRtmp_JitterZero plays on a variant with time_jitter zero.
func TestRtmp_JitterZero(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{TimeJitter: "zero"}))

	// StartsAtZero publishes a stream that starts at 100s to a player that
	// joined before: the player gets every message 100s earlier.
	check.Case(t, "StartsAtZero", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := shiftTags(readTags(c, media.H264HighAAC, 0, time.Second), absBase)
		publishTags(c, url, tags)

		got := readMedia(c, player, tags)
		c.CheckEqual(tagTimes(got), tagTimes(shiftTags(tags, -absBase)), "messages")
	})

	// KeepsJumps publishes a stream from zero that jumps 10s forward: zero
	// only moves the start, so the player gets the jump.
	check.Case(t, "KeepsJumps", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := jumpTags(c, false)
		publishTags(c, url, tags)

		c.CheckEqual(tagTimes(readMedia(c, player, tags)), tagTimes(tags), "messages")
	})

	// BackJumpPlays publishes a stream that jumps 10s forward, then 5s back:
	// the player gets the media after the jump back too.
	check.Case(t, "BackJumpPlays", check.Normal, func(c *check.C) {
		check.KnownBug(c, "without time correction, a player gets nothing after the timestamps jump back, until they pass the old ones")
		backJumpPlays(c, srs, 0)
	})
}

// TestRtmp_JitterOffAtcAuto plays on a variant with time_jitter off and
// atc_auto on, merged because atc_auto changes only the timestamps of the
// headers a late player gets, which time_jitter off leaves as published.
func TestRtmp_JitterOffAtcAuto(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{TimeJitter: "off", ATCAuto: true}))

	// KeepsTimestamps publishes a stream that starts at 100s and jumps 10s
	// forward: the player gets every timestamp as published.
	check.Case(t, "KeepsTimestamps", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := shiftTags(jumpTags(c, false), absBase)
		publishTags(c, url, tags)

		c.CheckEqual(tagTimes(readMedia(c, player, tags)), tagTimes(tags), "messages")
	})

	// BackJumpPlays is the case of TestRtmp_JitterZero for time_jitter off,
	// with the stream starting at 100s.
	check.Case(t, "BackJumpPlays", check.Normal, func(c *check.C) {
		check.KnownBug(c, "without time correction, a player gets nothing after the timestamps jump back, until they pass the old ones")
		backJumpPlays(c, srs, absBase)
	})

	// AtcAutoBravo holds a stream that starts at 100s, whose metadata has
	// bravo_atc "true": ATC is on, so a late player gets the metadata and
	// headers at the time of the cached GOP.
	check.Case(t, "AtcAutoBravo", check.Normal, func(c *check.C) {
		gop, headers := lateJoinHeaders(c, srs, true)
		c.CheckEqual(headers, []string{"M" + gop, "A" + gop, "V" + gop}, "metadata and headers")
	})

	// AtcAutoNoBravo holds the same stream without bravo_atc: ATC stays off,
	// so a late player gets the metadata and headers at their own time.
	check.Case(t, "AtcAutoNoBravo", check.Normal, func(c *check.C) {
		_, headers := lateJoinHeaders(c, srs, false)
		start := fmt.Sprint(absBase)
		c.CheckEqual(headers, []string{"M" + start, "A" + start, "V" + start}, "metadata and headers")
	})
}

// TestRtmp_AtcMixCorrect plays on a variant with atc and mix_correct on,
// merged because ATC keeps the timestamps the mix orders.
func TestRtmp_AtcMixCorrect(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{ATC: true, MixCorrect: true}))

	// KeepsTimestamps publishes a stream that starts at 100s and jumps 10s
	// forward: the player gets each track's timestamps as published. The mix
	// holds the last few messages back.
	check.Case(t, "KeepsTimestamps", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := shiftTags(jumpTags(c, false), absBase)
		publishTags(c, url, tags)

		got := readMixed(c, player, tags)
		for _, typ := range []uint8{media.TagVideo, media.TagAudio} {
			pub, play := tagTimes(ofType(tags, typ)), tagTimes(ofType(got, typ))
			c.Check(len(play) <= len(pub) && slices.Equal(play, pub[:len(play)]), "track %d: %v, want the start of %v", typ, play, pub)
		}
	})

	// LateJoinHeadersAtGop holds a stream that starts at 100s: a late player
	// gets the metadata and headers at the time of the cached GOP, then the
	// GOP at its own time.
	check.Case(t, "LateJoinHeadersAtGop", check.Normal, func(c *check.C) {
		gop, headers := lateJoinHeaders(c, srs, false)
		c.CheckEqual(headers, []string{"M" + gop, "A" + gop, "V" + gop}, "metadata and headers")
	})

	// MixCorrectOrders publishes audio 200ms after the video of the same
	// time: the player gets the tracks interleaved in time order.
	check.Case(t, "MixCorrectOrders", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := shiftTags(lateAudio(readTags(c, media.H264HighAAC, 0, time.Second), 200), absBase)
		publishTags(c, url, tags)

		r := analyzeTags(c, readMixed(c, player, tags))
		c.CheckEqual(r.Unordered, 0, "frames out of time order")
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps of each track")
	})
}

// TestRtmp_MergedWrite plays on a variant with min_latency off, SRS's way to
// save writes and reads: it holds a player's messages until it has more
// than 4 that span at least 200ms, and waits 100ms after a small read from a
// publisher.
func TestRtmp_MergedWrite(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{MergedWrite: &server.Merged{
		WriteLatency: 200 * time.Millisecond, WriteMsgs: 4, ReadLatency: 100 * time.Millisecond,
	}}))

	// HoldsUntilLatency publishes 150ms of media, more than 4 messages, to a
	// player that joined before: SRS holds them until the stream spans 200ms.
	check.Case(t, "HoldsUntilLatency", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		first := readTags(c, media.H264HighAAC, 0, 150*time.Millisecond)
		pub := publishTags(c, url, first)
		c.CheckNoError(readNothing(player, 100*time.Millisecond), "the player under 200ms of media")

		c.Must(writeTags(pub, readTags(c, media.H264HighAAC, 150*time.Millisecond, 300*time.Millisecond)), "publish more")
		ref := analyzeTags(c, first)
		var a flv.Analyzer
		u := report.Until{VideoFrames: ref.Video.Frames, AudioFrames: ref.Audio.Frames}
		c.CheckNoError(readUntil(player, &a, u, 300*time.Millisecond), "the held messages")
	})

	// HoldsUntilMsgs publishes a video header and 2 frames that span 240ms
	// to a player that joined before: SRS holds them until it has 5
	// messages.
	check.Case(t, "HoldsUntilMsgs", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		header, frames := videoFrames(c, 4)
		for i, ts := range []uint32{0, 240, 280, 320} {
			frames[i].Timestamp = ts
		}
		pub := publishTags(c, url, []*media.FLVTag{header, frames[0], frames[1]})
		c.CheckNoError(readNothing(player, 100*time.Millisecond), "the player with 3 messages")

		c.Must(writeTags(pub, frames[2:]), "publish 2 more frames")
		var a flv.Analyzer
		c.CheckNoError(readUntil(player, &a, report.Until{VideoHeader: true, VideoFrames: 4}, 300*time.Millisecond), "the held messages")
	})

	// MergedReadPlays publishes in real time to SRS that merges its reads:
	// the player gets the stream with its timestamps in order.
	check.Case(t, "MergedReadPlays", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)
		checkPlayed(c, playUntil(c, url, playedUntil), "h264", "aac")
	})
}

// TestRtmp_SendMinInterval plays on a variant that sends a player at most one
// message every 20ms.
func TestRtmp_SendMinInterval(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{SendMinInterval: 20 * time.Millisecond}))

	// PacesMessages publishes 10 messages at once to a player that joined
	// before: they come at least 20ms apart, in the order and with the
	// timestamps sent.
	check.Case(t, "PacesMessages", check.Normal, func(c *check.C) {
		url := rtmpURL(srs, "live/"+streamName(c))
		player := playerOf(c, url)
		tags := readTags(c, media.H264HighAAC, 0, time.Second)[:10]
		publishTags(c, url, tags)

		var got []*media.FLVTag
		var first, last time.Time
		player.SetDeadline(time.Now().Add(time.Second))
		defer player.SetDeadline(time.Time{})
		for len(got) < len(tags) {
			tag, err := player.ReadTag()
			c.Must(err, "play")
			if tag.Type == media.TagScript && !flv.IsMetadata(tag.Data) {
				continue
			}
			if last = time.Now(); first.IsZero() {
				first = last
			}
			got = append(got, tag)
		}
		c.CheckEqual(tagTimes(got), tagTimes(tags), "messages")
		span, want := last.Sub(first), time.Duration(len(tags)-1)*20*time.Millisecond
		c.Check(span >= want*8/10, "%d messages came in %v, want at least about %v", len(tags), span, want)
	})
}

// lateJoinHeaders holds a stream that starts at 100s, 2.5s long, with
// bravo_atc "true" in its metadata if bravo, and plays it late. It returns
// the timestamp of the first frame the player gets, and the metadata (M),
// audio (A) and video (V) headers it gets with their timestamps, such as
// "V102000".
func lateJoinHeaders(c *check.C, srs *server.Server, bravo bool) (string, []string) {
	c.Helper()
	url := rtmpURL(srs, "live/"+streamName(c))
	tags := shiftTags(readTags(c, media.H264HighAAC, 0, 2500*time.Millisecond), absBase)
	if bravo {
		tags = withBravoATC(c, tags)
	}
	publishTags(c, url, tags)

	gop := lastGOP(tags)
	player := playerOf(c, url)
	got := readMixed(c, player, gop)
	var headers []string
	first := ""
	for i, s := range tagTimes(got) {
		if isHeader(got[i]) || got[i].Type == media.TagScript {
			headers = append(headers, s)
		} else if first == "" {
			first = s[1:]
		}
	}
	c.CheckEqual(first, fmt.Sprint(gop[0].Timestamp), "the first frame")
	return first, headers
}

// readMedia reads a player's metadata, audio and video until it has as many
// as tags has, or fails.
func readMedia(c *check.C, player *rtmp.Client, tags []*media.FLVTag) []*media.FLVTag {
	c.Helper()
	got, err := readTagsOf(player, len(tags), time.Second, 0)
	c.Must(err, "play")
	return got
}

// readMixed reads a player's metadata, audio and video until it has as many
// as tags has, or until none comes for 100ms, because the mix of
// mix_correct holds the last few back.
func readMixed(c *check.C, player *rtmp.Client, tags []*media.FLVTag) []*media.FLVTag {
	c.Helper()
	got, err := readTagsOf(player, len(tags), time.Second, 100*time.Millisecond)
	c.Must(err, "play")
	c.Check(len(got) >= len(tags)-10, "the player got %d of %d messages", len(got), len(tags))
	return got
}

// readTagsOf reads a player's metadata, audio and video until it has n of
// them, and fails after timeout. With idle, it stops early when none comes
// for that long.
func readTagsOf(player tagPlayer, n int, timeout, idle time.Duration) ([]*media.FLVTag, error) {
	defer player.SetDeadline(time.Time{})
	deadline := time.Now().Add(timeout)
	var got []*media.FLVTag
	for len(got) < n {
		d := deadline
		if idle > 0 && time.Now().Add(idle).Before(d) {
			d = time.Now().Add(idle)
		}
		player.SetDeadline(d)
		tag, err := player.ReadTag()
		if idle > 0 && isTimeout(err) && time.Now().Before(deadline) {
			return got, nil
		} else if err != nil {
			return got, fmt.Errorf("%w, after %d of %d messages", err, len(got), n)
		}
		if tag.Type == media.TagScript && !flv.IsMetadata(tag.Data) {
			continue
		}
		got = append(got, tag)
	}
	return got, nil
}

// shiftTags is a copy of tags with d milliseconds added to each timestamp.
func shiftTags(tags []*media.FLVTag, d int64) []*media.FLVTag {
	out := make([]*media.FLVTag, len(tags))
	for i, tag := range tags {
		out[i] = &media.FLVTag{Type: tag.Type, Timestamp: uint32(int64(tag.Timestamp) + d), Data: tag.Data}
	}
	return out
}

// jumpTags is 3s of the test file whose second second jumps 10s forward, and,
// with back, whose third second then jumps 5s back: 0s to 1s, 11s to 12s, and
// 7s to 8s.
func jumpTags(c *check.C, back bool) []*media.FLVTag {
	c.Helper()
	tags := readTags(c, media.H264HighAAC, 0, time.Second)
	tags = append(tags, shiftTags(readTags(c, media.H264HighAAC, time.Second, 2*time.Second), 10000)...)
	if back {
		tags = append(tags, shiftTags(readTags(c, media.H264HighAAC, 2*time.Second, 3*time.Second), 5000)...)
	}
	return tags
}

// lateAudio reorders tags so each audio message comes after the video up to
// ms milliseconds later, like an encoder whose audio lags. The timestamps
// don't change.
func lateAudio(tags []*media.FLVTag, ms uint32) []*media.FLVTag {
	out := slices.Clone(tags)
	key := func(tag *media.FLVTag) uint32 {
		if tag.Type == media.TagAudio {
			return tag.Timestamp + ms
		}
		return tag.Timestamp
	}
	slices.SortStableFunc(out, func(a, b *media.FLVTag) int {
		return int(key(a)) - int(key(b))
	})
	return out
}

// withBravoATC is a copy of tags whose metadata has bravo_atc "true", which
// an encoder sends for absolute timestamps.
func withBravoATC(c *check.C, tags []*media.FLVTag) []*media.FLVTag {
	c.Helper()
	out := slices.Clone(tags)
	for i, tag := range out {
		if tag.Type != media.TagScript || !flv.IsMetadata(tag.Data) {
			continue
		}
		values, err := rtmp.DecodeAMF0(tag.Data)
		c.Must(err, "decode the metadata")
		if len(values) < 2 {
			c.Fatalf("metadata %v has no values", values)
		}
		meta := map[string]any{}
		switch m := values[1].(type) {
		case rtmp.ECMAArray:
			for k, v := range m {
				meta[k] = v
			}
		case map[string]any:
			for k, v := range m {
				meta[k] = v
			}
		}
		meta["bravo_atc"] = "true"
		data, err := rtmp.EncodeAMF0(values[0], rtmp.ECMAArray(meta))
		c.Must(err, "encode the metadata")
		out[i] = &media.FLVTag{Type: tag.Type, Timestamp: tag.Timestamp, Data: data}
		return out
	}
	c.Fatalf("no metadata in the tags")
	return nil
}

// firstKeyframe is the tags of a test file up to its first video keyframe:
// the metadata, the headers, and the keyframe.
func firstKeyframe(c *check.C, file string) []*media.FLVTag {
	c.Helper()
	var out []*media.FLVTag
	for _, tag := range readTags(c, file, 0, time.Second) {
		out = append(out, tag)
		if tag.Type == media.TagVideo && !isHeader(tag) {
			return out
		}
	}
	c.Fatalf("%s has no keyframe in its first second", file)
	return nil
}

// videoFrames is the video sequence header and the first n video frames of
// the video-only test file.
func videoFrames(c *check.C, n int) (*media.FLVTag, []*media.FLVTag) {
	c.Helper()
	var header *media.FLVTag
	var frames []*media.FLVTag
	for _, tag := range readTags(c, media.H264Only, 0, time.Second) {
		switch {
		case tag.Type != media.TagVideo:
		case isHeader(tag):
			header = tag
		case len(frames) < n:
			frames = append(frames, &media.FLVTag{Type: tag.Type, Timestamp: tag.Timestamp, Data: tag.Data})
		}
	}
	if header == nil || len(frames) < n {
		c.Fatalf("%s has no header or fewer than %d frames", media.H264Only, n)
	}
	return header, frames
}

// isHeader reports whether a tag is an audio or video sequence header.
func isHeader(tag *media.FLVTag) bool {
	switch tag.Type {
	case media.TagVideo:
		v, err := media.ParseVideo(tag.Data)
		return err == nil && v.SequenceHeader
	case media.TagAudio:
		a, err := media.ParseAudio(tag.Data)
		return err == nil && a.SequenceHeader
	}
	return false
}

// avTags is the audio and video tags of tags.
func avTags(tags []*media.FLVTag) []*media.FLVTag {
	return slices.DeleteFunc(slices.Clone(tags), func(tag *media.FLVTag) bool {
		return tag.Type == media.TagScript
	})
}

// ofType is the tags of one type.
func ofType(tags []*media.FLVTag, typ uint8) []*media.FLVTag {
	return slices.DeleteFunc(slices.Clone(tags), func(tag *media.FLVTag) bool {
		return tag.Type != typ
	})
}

// tagTimes describes each tag by its type, M for metadata, V for video and A
// for audio, and its timestamp, such as "V40".
func tagTimes(tags []*media.FLVTag) []string {
	out := make([]string, len(tags))
	for i, tag := range tags {
		kind := map[uint8]string{media.TagScript: "M", media.TagVideo: "V", media.TagAudio: "A"}[tag.Type]
		out[i] = fmt.Sprintf("%s%d", kind, tag.Timestamp)
	}
	return out
}

// backJumpPlays publishes a stream that starts at base and jumps 10s
// forward, to a player that joined before, waits until the player has it
// all, then publishes the media 5s back, and checks the player gets every
// message as published, the ones after the jump back within 300ms. The wait
// makes SRS send the jump forward before the jump back arrives; when one
// read took both, the player got them in one batch, past the bug.
func backJumpPlays(c *check.C, srs *server.Server, base int64) {
	c.Helper()
	url := rtmpURL(srs, "live/"+streamName(c))
	player := playerOf(c, url)
	tags := shiftTags(jumpTags(c, true), base)
	forward, back := tags[:len(jumpTags(c, false))], tags[len(jumpTags(c, false)):]
	pub := publishTags(c, url, forward)
	got, err := readTagsOf(player, len(forward), time.Second, 0)
	c.Must(err, "play up to the jump back")

	c.Must(writeTags(pub, back), "publish the jump back")
	more, err := readTagsOf(player, len(back), 300*time.Millisecond, 0)
	c.CheckNoError(err, "play across the jump back")
	c.CheckEqual(tagTimes(append(got, more...)), tagTimes(tags), "messages")
}
