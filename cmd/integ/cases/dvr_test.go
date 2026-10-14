// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/mp4"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// dvrSegment is the dvr_duration of the segment groups, in whole seconds like
// SRS reads it, 2s, so a segment spans more than one 1s GOP.
const dvrSegment = 2 * time.Second

// dvrConfig is the config of a DVR group: RTMP to publish, the HTTP API, the
// hooks for on_dvr, and DVR with the settings d. The files go under ./dvr in
// the server's directory, where the cases read them.
func dvrConfig(d server.DVR) server.Config {
	return server.WithDVR(server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI},
		Hooks:     true,
	}, d)
}

// TestDvr_Session records each publish to one FLV file, the session plan, at
// a path with every variable of dvr_path: the vhost, app and stream, and the
// date and time to the millisecond. dvr_wait_keyframe is on, SRS's default.
func TestDvr_Session(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, dvrConfig(server.DVR{
		Path: "./dvr/[vhost]/[app]/[stream]/[2006]-[01]-[02]/[15].[04].[05].[999].flv",
	}))

	// Each codec of the test media, 5s published at once: the file has
	// every frame, from the first keyframe, with its sequence headers.
	for _, tc := range []struct {
		name, file, vcodec, acodec string
	}{
		{"FlvH264Aac", media.H264HighAAC, "h264", "aac"},
		{"FlvHevc", media.HEVCAAC, "hevc", "aac"},
		{"FlvMp3", media.H264HighMP3, "h264", "mp3"},
		{"FlvAudioOnly", media.AACOnly, "", "aac"},
		{"FlvVideoOnly", media.H264Only, "h264", ""},
	} {
		check.Case(t, tc.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			tags := readTags(c, tc.file, 0, 5*time.Second)
			publishDvr(c, srs, "live/"+stream, tags)
			files := dvrFiles(c, srs, stream, 1)
			r, _ := analyzeDvrFLV(c, files[0])
			checkDvrMedia(c, r, publishedOf(c, tags), tc.vcodec, tc.acodec)
			c.CheckEqual(r.Metadata, 1, "metadata")
			if r.Video != nil {
				c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
			}
			c.Check(!exists(files[0]+".tmp"), "the temporary file %v.tmp is left", files[0])
		})
	}

	// The onMetaData of the file has its duration and size, which SRS
	// writes once the publish ends.
	check.Case(t, "MetadataDuration", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, 5*time.Second))
		files := dvrFiles(c, srs, stream, 1)
		_, meta := analyzeDvrFLV(c, files[0])
		duration, _ := meta["duration"].(float64)
		c.Check(near(time.Duration(duration*float64(time.Second)), 5*time.Second, 100*time.Millisecond), "metadata duration %vs, want about 5s", duration)
		st, err := os.Stat(files[0])
		c.Must(err, "stat the file")
		c.CheckEqual(meta["filesize"], float64(st.Size()), "metadata filesize")
	})

	// The file is at the path of the template: the vhost, app and stream,
	// then the date and time of the publish, to the millisecond.
	check.Case(t, "PathVariables", check.Normal, func(c *check.C) {
		stream := streamName(c)
		start := time.Now()
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, time.Second))
		hooks := dvrHooks(c, srs, stream, 1)
		end := time.Now()
		re := regexp.MustCompile(`^\./dvr/__defaultVhost__/live/` + regexp.QuoteMeta(stream) + `/(\d{4}-\d{2}-\d{2}/\d{2}\.\d{2}\.\d{2}\.\d{3})\.flv$`)
		m := re.FindStringSubmatch(hooks[0].Field("file"))
		if !c.Check(m != nil, "file %q, want ./dvr/__defaultVhost__/live/%v/<date>/<time>.flv", hooks[0].Field("file"), stream) {
			return
		}
		at, err := time.ParseInLocation("2006-01-02/15.04.05.000", m[1], time.Local)
		c.Must(err, "parse the time of the file")
		c.Check(!at.Before(start.Truncate(time.Millisecond)) && !at.After(end), "file time %v, want between the publish %v and its on_dvr %v", at, start, end)
		c.Check(exists(dvrFilePath(c, srs, hooks[0])), "no file %v", hooks[0].Field("file"))
	})

	// on_dvr has every field: the publisher's ids, IP, vhost, app, stream,
	// param and tcUrl as on_publish has them, the server's working directory
	// and the file, relative to it. SRS sends it after the publish ended, and
	// it lacks the stream_id when the stream has left the API by then, now
	// and then (not asserted), so stream_id is checked only when it is there;
	// OnDvrPerSegment checks it on the files cut while the stream is published.
	check.Case(t, "OnDvrPayload", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream+"?k1=v1", readTags(c, media.H264HighAAC, 0, time.Second))
		h := dvrHooks(c, srs, stream, 1)[0]
		pub := waitHookCountOf(c, srs, mock.Match{Action: "on_publish", Stream: stream}, 1)[0]
		for _, k := range []string{"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "tcUrl", "stream", "param", "cwd", "file", "stream_url"} {
			_, ok := h.JSON[k]
			c.Check(ok, "on_dvr has no %s: %s", k, h.Body)
		}
		ids := apiIDs(c, srs)
		c.CheckEqual(h.Field("server_id"), ids.Server, "server_id, the API's server")
		c.CheckEqual(h.Field("service_id"), ids.Service, "service_id, the API's service")
		for _, k := range []string{"client_id", "ip", "vhost", "app", "tcUrl", "stream", "param", "stream_url"} {
			c.CheckEqual(h.Field(k), pub.Field(k), k+", the on_publish's")
		}
		if _, ok := h.JSON["stream_id"]; ok {
			c.CheckEqual(h.Field("stream_id"), pub.Field("stream_id"), "stream_id, the on_publish's")
		}
		c.CheckEqual(h.Field("ip"), "127.0.0.1", "ip")
		c.CheckEqual(h.Field("param"), "?k1=v1", "param")
		c.CheckEqual(h.Field("stream_url"), "/live/"+stream, "stream_url")
		c.CheckEqual(sameDir(c, h.Field("cwd"), srs.Dir), true, "cwd "+h.Field("cwd")+", the server's directory "+srs.Dir)
		c.Check(strings.HasPrefix(h.Field("file"), "./dvr/"), "file %q, want relative to the cwd", h.Field("file"))
		c.Check(exists(dvrFilePath(c, srs, h)), "no file %v at the on_dvr", h.Field("file"))
	})

	// A publish again of the stream is a new session: a new file, with only
	// its own media.
	check.Case(t, "RepublishNewFile", check.Normal, func(c *check.C) {
		stream := streamName(c)
		first := readTags(c, media.H264HighAAC, 0, 2*time.Second)
		publishDvr(c, srs, "live/"+stream, first)
		dvrFiles(c, srs, stream, 1)
		waitHookCount(c, srs, mock.Match{Action: "on_unpublish", Stream: stream}, 1)
		second := readTags(c, media.H264HighAAC, 0, 3*time.Second)
		publishDvr(c, srs, "live/"+stream, second)
		files := dvrFiles(c, srs, stream, 2)
		c.Check(files[0] != files[1], "both sessions in %v", files[0])
		for i, tags := range [][]*media.FLVTag{first, second} {
			r, _ := analyzeDvrFLV(c, files[i])
			checkDvrMedia(c, r, publishedOf(c, tags), "h264", "aac")
		}
	})

	// A publisher that starts mid-GOP: with dvr_wait_keyframe on, the file
	// drops the video before the first keyframe, and keeps the sequence
	// headers and the audio.
	check.Case(t, "WaitKeyframeDropsLeading", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, midGOPTags(c, media.H264HighAAC, 500*time.Millisecond, 3*time.Second))
		r, _ := analyzeDvrFLV(c, dvrFiles(c, srs, stream, 1)[0])
		checkVideo(c, r, "h264")
		checkAudio(c, r, "aac", 1)
		c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
		c.CheckEqual(r.Video.Keyframes, 2, "keyframes, at 1s and 2s")
	})
}

// TestDvr_SessionMp4 records each publish to one MP4 file, by the .mp4 of
// dvr_path, named by the [timestamp] of the publish.
func TestDvr_SessionMp4(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, dvrConfig(server.DVR{Path: "./dvr/[app]/[stream].[timestamp].mp4"}))

	// Each codec of the test media, 5s published at once. Mp4Hevc publishes
	// the HEVC file without ffmpeg's info frame, the colorInfo metadata of
	// enhanced RTMP; Mp4HevcInfoFrame with it, which holds no picture, so the
	// file has only the pictures as samples.
	for _, tc := range []struct {
		name, file, vcodec, acodec string
		noInfoFrames               bool
	}{
		{"Mp4H264Aac", media.H264HighAAC, "h264", "aac", false},
		{"Mp4Hevc", media.HEVCAAC, "hevc", "aac", true},
		{"Mp4HevcInfoFrame", media.HEVCAAC, "hevc", "aac", false},
		{"Mp4Mp3", media.H264HighMP3, "h264", "mp3", false},
		{"Mp4AudioOnly", media.AACOnly, "", "aac", false},
		{"Mp4VideoOnly", media.H264Only, "h264", "", false},
	} {
		check.Case(t, tc.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			var tags []*media.FLVTag
			for _, tag := range readTags(c, tc.file, 0, 5*time.Second) {
				if !tc.noInfoFrames || !isInfoFrame(tag) {
					tags = append(tags, tag)
				}
			}
			publishDvr(c, srs, "live/"+stream, tags)
			files := dvrFiles(c, srs, stream, 1)
			r, err := mp4.AnalyzeFile(files[0], report.Until{})
			c.Must(err, "analyze the file")
			checkDvrMedia(c, r, publishedOf(c, tags), tc.vcodec, tc.acodec)
		})
	}

	// The audio sample entry has the sample rate in 16.16 fixed point, as
	// ISO/IEC 14496-12 has it: 44100Hz for the AAC of the test media.
	check.Case(t, "SampleEntryRate", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, time.Second))
		m := readDvrMP4(c, dvrFiles(c, srs, stream, 1)[0])
		if at := m.Track("soun"); c.Check(at != nil, "no audio track") {
			c.CheckEqual(at.SampleRate, uint32(44100), "sample rate of the audio sample entry")
		}
	})

	// The file is named by the time of the publish in milliseconds.
	check.Case(t, "TimestampName", check.Normal, func(c *check.C) {
		stream := streamName(c)
		start := time.Now()
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, time.Second))
		h := dvrHooks(c, srs, stream, 1)[0]
		end := time.Now()
		m := regexp.MustCompile(`^\./dvr/live/` + regexp.QuoteMeta(stream) + `\.(\d+)\.mp4$`).FindStringSubmatch(h.Field("file"))
		if !c.Check(m != nil, "file %q, want ./dvr/live/%v.<timestamp>.mp4", h.Field("file"), stream) {
			return
		}
		ms, _ := strconv.ParseInt(m[1], 10, 64)
		at := time.UnixMilli(ms)
		c.Check(!at.Before(start.Truncate(time.Millisecond)) && !at.After(end), "file time %v, want between the publish %v and its on_dvr %v", at, start, end)
	})

	// A publisher that sends each picture with a second video message of
	// the same time, an access unit delimiter, as some encoders send SEI and
	// AUD apart from the picture: the MP4 keeps the zero step between the two
	// in stts, so its timeline is the published one, not twice as long.
	check.Case(t, "ZeroDeltaKept", check.Normal, func(c *check.C) {
		stream := streamName(c)
		tags := readTags(c, media.H264HighAAC, 0, 3*time.Second)
		var withAUD []*media.FLVTag
		extra := 0
		for _, tag := range tags {
			withAUD = append(withAUD, tag)
			if v, err := media.ParseVideo(tag.Data); err == nil && tag.Type == media.TagVideo && !v.SequenceHeader {
				withAUD = append(withAUD, audTag(tag.Timestamp))
				extra++
			}
		}
		publishDvr(c, srs, "live/"+stream, withAUD)
		m := readDvrMP4(c, dvrFiles(c, srs, stream, 1)[0])
		vt := m.Track("vide")
		if !c.Check(vt != nil, "no video track") {
			return
		}
		c.CheckEqual(len(vt.Samples), 2*extra, "video samples, each picture and its delimiter")
		same := 0
		for i := 1; i < len(vt.Samples); i++ {
			if vt.Samples[i].DTS == vt.Samples[i-1].DTS {
				same++
			}
		}
		c.CheckEqual(same, extra, "samples at the time of the one before")
		last := vt.Samples[len(vt.Samples)-1]
		end := time.Duration(last.DTS) * time.Second / time.Duration(vt.Timescale)
		c.Check(end < 3*time.Second, "the last video sample at %v, want before the 3s published", end)
	})
}

// TestDvr_Segment records each publish to FLV files of about dvr_duration,
// 2s, the segment plan, each cut at the first keyframe after it, as
// dvr_wait_keyframe on, SRS's default, asks.
func TestDvr_Segment(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, dvrConfig(server.DVR{
		Plan:     "segment",
		Path:     "./dvr/[app]/[stream].[timestamp].flv",
		Duration: dvrSegment,
	}))

	// 7s of media: the files start at keyframes, each with both sequence
	// headers, and together have every frame, in time order.
	check.Case(t, "SegmentsAtKeyframes", check.Normal, func(c *check.C) {
		stream := streamName(c)
		tags := readTags(c, media.H264HighAAC, 0, 7*time.Second)
		publishDvr(c, srs, "live/"+stream, tags)
		files := dvrFiles(c, srs, stream, 3)
		checkSegments(c, files, publishedOf(c, tags), true)
	})

	// Each file has its on_dvr, of the publisher, in order, each naming a
	// file of its own that is there.
	check.Case(t, "OnDvrPerSegment", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publisher := publishDvrOpen(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, 7*time.Second))
		pub := waitHookCountOf(c, srs, mock.Match{Action: "on_publish", Stream: stream}, 1)[0]
		// The files cut while the stream is published have its stream_id.
		for _, h := range dvrHooks(c, srs, stream, 2) {
			c.CheckEqual(h.Field("stream_id"), pub.Field("stream_id"), "stream_id of an on_dvr")
		}
		publisher.Close()
		dvrFiles(c, srs, stream, 3)
		hooks := srs.Hooks.Requests(mock.Match{Action: "on_dvr", Stream: stream})
		seen := map[string]bool{}
		for _, h := range hooks {
			c.CheckEqual(h.Field("client_id"), pub.Field("client_id"), "client_id of an on_dvr")
			c.Check(!seen[h.Field("file")], "on_dvr of %v twice", h.Field("file"))
			seen[h.Field("file")] = true
			c.Check(exists(dvrFilePath(c, srs, h)), "no file %v", h.Field("file"))
		}
	})

	// The media of 7s in files of 2s: the keyframe at 2s cuts the first.
	check.Case(t, "SegmentSpansDuration", check.Normal, func(c *check.C) {
		check.KnownBug(c, "with a 1s GOP each file spans 3s, not dvr_duration 2s: the keyframe at 2s doesn't cut, as SRS measures a file without its last frame")
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, 7*time.Second))
		files := dvrAllFiles(c, srs, stream)
		c.Check(len(files) >= 4, "files of 7s: %d, want 4", len(files))
		for i, file := range files[:len(files)-1] {
			r, _ := analyzeDvrFLV(c, file)
			if c.Check(r.Video != nil, "file %d has no video", i+1) {
				c.Check(near(r.Video.Duration(), dvrSegment, 100*time.Millisecond), "file %d spans %v, want %v", i+1, r.Video.Duration(), dvrSegment)
			}
		}
	})

	// A stream with no video, 7s: files of 2s too, as there is no keyframe
	// to wait for.
	check.Case(t, "AudioOnlySegments", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a stream with no video is never cut, one file for the whole publish: dvr_wait_keyframe waits for a video keyframe")
		stream := streamName(c)
		tags := readTags(c, media.AACOnly, 0, 7*time.Second)
		publishDvr(c, srs, "live/"+stream, tags)
		files := dvrAllFiles(c, srs, stream)
		c.Check(len(files) >= 3, "files of 7s: %d, want one each 2s or so", len(files))
	})

	// A publisher that starts mid-GOP: the first file starts at the first
	// keyframe, as with the session plan.
	check.Case(t, "WaitKeyframeDropsLeading", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, midGOPTags(c, media.H264HighAAC, 500*time.Millisecond, 3*time.Second))
		r, _ := analyzeDvrFLV(c, dvrFiles(c, srs, stream, 1)[0])
		checkVideo(c, r, "h264")
		c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
	})
}

// TestDvr_SegmentNoWait is the segment plan with dvr_wait_keyframe off: a
// file is cut once it spans dvr_duration, 2s, at any frame.
func TestDvr_SegmentNoWait(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, dvrConfig(server.DVR{
		Plan:           "segment",
		Path:           "./dvr/[app]/[stream].[timestamp].flv",
		Duration:       dvrSegment,
		NoWaitKeyframe: true,
	}))

	// 7s of media from 0.5s, mid-GOP, so 2s cuts don't meet the keyframes
	// at whole seconds: each file but the last spans 2s, and a file starts
	// with frames before its first keyframe.
	check.Case(t, "SegmentsAtDuration", check.Normal, func(c *check.C) {
		stream := streamName(c)
		tags := midGOPTags(c, media.H264HighAAC, 500*time.Millisecond, 7500*time.Millisecond)
		publishDvr(c, srs, "live/"+stream, tags)
		files := dvrFiles(c, srs, stream, 4)
		checkSegments(c, files, publishedOf(c, tags), false)
	})

	// The first file keeps the video before the first keyframe.
	check.Case(t, "LeadingFramesKept", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, midGOPTags(c, media.H264HighAAC, 500*time.Millisecond, 1500*time.Millisecond))
		r, _ := analyzeDvrFLV(c, dvrFiles(c, srs, stream, 1)[0])
		checkVideo(c, r, "h264")
		c.Check(r.Video.LeadingFrames > 0, "no frame before the first keyframe, want the 500ms before it")
	})
}

// TestDvr_Apply records only the streams of dvr_apply, to MP4 files of the
// segment plan. Each applied case publishes a stream named for it, which
// the server's dvr_apply lists before it starts, for each copy of the case.
func TestDvr_Apply(t *testing.T) {
	t.Parallel()
	b := make([]byte, 3)
	rand.Read(b)
	suffix := "-" + hex.EncodeToString(b)
	applied := []string{"AppliedMp4Segments", "SecondApplied"}
	var apply []string
	for _, name := range applied {
		for _, n := range check.CopyNames(name, check.Normal) {
			apply = append(apply, "live/"+n+suffix)
		}
	}
	srs := server.Group(t, dvrConfig(server.DVR{
		Plan:     "segment",
		Path:     "./dvr/[app]/[stream].[timestamp].mp4",
		Duration: dvrSegment,
		Apply:    apply,
	}))
	appliedName := func(c *check.C) string { return path.Base(c.Name()) + suffix }

	// An applied stream, 7s: MP4 files that start at keyframes, each with
	// the codecs, and together every frame.
	check.Case(t, "AppliedMp4Segments", check.Normal, func(c *check.C) {
		stream := appliedName(c)
		tags := readTags(c, media.H264HighAAC, 0, 7*time.Second)
		publishDvr(c, srs, "live/"+stream, tags)
		files := dvrFiles(c, srs, stream, 3)
		checkSegments(c, files, publishedOf(c, tags), true)
	})

	// The second stream of the list is recorded too.
	check.Case(t, "SecondApplied", check.Normal, func(c *check.C) {
		stream := appliedName(c)
		tags := readTags(c, media.H264HighAAC, 0, time.Second)
		publishDvr(c, srs, "live/"+stream, tags)
		r, err := mp4.AnalyzeFile(dvrFiles(c, srs, stream, 1)[0], report.Until{})
		c.Must(err, "analyze the file")
		checkDvrMedia(c, r, publishedOf(c, tags), "h264", "aac")
	})

	// A stream not in the list: no file and no on_dvr.
	check.Case(t, "OtherNotRecorded", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, 3*time.Second))
		waitHookCount(c, srs, mock.Match{Action: "on_unpublish", Stream: stream}, 1)
		time.Sleep(100 * time.Millisecond)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_dvr", Stream: stream})), 0, "on_dvr of a stream not applied")
		matches, _ := filepath.Glob(filepath.Join(srs.Dir, "dvr", "live", stream+"*"))
		c.CheckEqual(len(matches), 0, "files of a stream not applied")
	})
}

// publishDvr publishes tags at once, then closes the publisher, which ends
// the publish and its DVR session.
func publishDvr(c *check.C, srs *server.Server, path string, tags []*media.FLVTag) {
	c.Helper()
	publishDvrOpen(c, srs, path, tags).Close()
}

// publishDvrOpen publishes tags at once, and returns the publisher, closed
// when the case ends. It pauses 2ms before each keyframe after the first, so
// each segment of a group whose dvr_path has the time to the millisecond gets
// a name of its own; SRS refuses to write a file that is there.
func publishDvrOpen(c *check.C, srs *server.Server, path string, tags []*media.FLVTag) *rtmp.Client {
	c.Helper()
	pub := publishOnly(c, rtmpURL(srs, path), rtmp.FMLE)
	free := srs.Burst(c)
	defer free()
	keyframes := 0
	for _, tag := range tags {
		if tag.Type == media.TagVideo {
			if v, err := media.ParseVideo(tag.Data); err == nil && v.Key && !v.SequenceHeader {
				if keyframes++; keyframes > 1 {
					time.Sleep(2 * time.Millisecond)
				}
			}
		}
		c.Must(pub.WriteTag(tag), "publish the media")
	}
	return pub
}

// midGOPTags is the tags of a test file from a time mid-GOP to another,
// after its metadata and sequence headers, as a publisher that starts there
// sends them.
func midGOPTags(c *check.C, file string, from, to time.Duration) []*media.FLVTag {
	c.Helper()
	var tags []*media.FLVTag
	for _, tag := range readTags(c, file, 0, from) {
		switch tag.Type {
		case media.TagScript:
			tags = append(tags, tag)
		case media.TagVideo:
			if v, err := media.ParseVideo(tag.Data); err == nil && v.SequenceHeader {
				tags = append(tags, tag)
			}
		case media.TagAudio:
			if a, err := media.ParseAudio(tag.Data); err == nil && a.SequenceHeader {
				tags = append(tags, tag)
			}
		}
	}
	return append(tags, readTags(c, file, from, to)...)
}

// audTag is an H.264 video message at ts with only an access unit
// delimiter.
func audTag(ts uint32) *media.FLVTag {
	return &media.FLVTag{Type: media.TagVideo, Timestamp: ts, Data: []byte{0x27, 0x01, 0, 0, 0, 0, 0, 0, 2, 0x09, 0xf0}}
}

// dvrHooks waits for the first n on_dvr of a stream, and returns every one
// so far, in the order SRS sent them.
func dvrHooks(c *check.C, srs *server.Server, stream string, n int) []mock.Request {
	c.Helper()
	return waitHookCountOf(c, srs, mock.Match{Action: "on_dvr", Stream: stream}, n)
}

// dvrFiles waits for the on_dvr of a stream's n files, once its publish
// ended, and returns the path of each, in order. More on_dvr than n fail.
func dvrFiles(c *check.C, srs *server.Server, stream string, n int) []string {
	c.Helper()
	dvrHooks(c, srs, stream, n)
	waitHookCount(c, srs, mock.Match{Action: "on_unpublish", Stream: stream}, 1)
	var files []string
	for _, h := range srs.Hooks.Requests(mock.Match{Action: "on_dvr", Stream: stream}) {
		files = append(files, dvrFilePath(c, srs, h))
	}
	c.CheckEqual(len(files), n, "files of "+stream)
	return files
}

// dvrAllFiles is dvrFiles for a case that doesn't know how many files a
// publish makes: it waits for the publish to end and the first on_dvr, then
// 50ms more for the on_dvr of a last file, and returns every file.
func dvrAllFiles(c *check.C, srs *server.Server, stream string) []string {
	c.Helper()
	dvrHooks(c, srs, stream, 1)
	waitHookCount(c, srs, mock.Match{Action: "on_unpublish", Stream: stream}, 1)
	time.Sleep(50 * time.Millisecond)
	var files []string
	for _, h := range srs.Hooks.Requests(mock.Match{Action: "on_dvr", Stream: stream}) {
		files = append(files, dvrFilePath(c, srs, h))
	}
	return files
}

// readDvrMP4 reads the boxes and sample tables of an MP4 file.
func readDvrMP4(c *check.C, file string) *media.MP4File {
	c.Helper()
	f, err := os.Open(file)
	c.Must(err, "open "+file)
	c.Cleanup(func() { f.Close() })
	st, err := f.Stat()
	c.Must(err, "stat "+file)
	m, err := media.ReadMP4(f, st.Size())
	c.Must(err, "read "+file)
	return m
}

// dvrFilePath is the path of an on_dvr's file: its file, relative to its
// cwd, which must be the server's directory.
func dvrFilePath(c *check.C, srs *server.Server, h mock.Request) string {
	c.Helper()
	file := h.Field("file")
	if filepath.IsAbs(file) {
		return file
	}
	return filepath.Join(srs.Dir, file)
}

// sameDir reports whether two paths are the same directory, through symlinks
// such as /tmp on macOS.
func sameDir(c *check.C, a, b string) bool {
	c.Helper()
	ra, err := filepath.EvalSymlinks(a)
	c.Must(err, "resolve "+a)
	rb, err := filepath.EvalSymlinks(b)
	c.Must(err, "resolve "+b)
	return ra == rb
}

// exists reports whether a file is there.
func exists(file string) bool {
	_, err := os.Stat(file)
	return err == nil
}

// analyzeDvrFLV reads an FLV file to its end, and returns its report and the
// object of its first onMetaData.
func analyzeDvrFLV(c *check.C, file string) (*report.Report, map[string]any) {
	c.Helper()
	f, err := os.Open(file)
	c.Must(err, "open "+file)
	defer f.Close()
	r, err := media.NewFLVReader(f)
	c.Must(err, "read "+file)
	var a flv.Analyzer
	var meta map[string]any
	for {
		tag, err := r.ReadTag()
		if errors.Is(err, io.EOF) {
			break
		}
		c.Must(err, "read "+file)
		c.Must(a.Add(tag), "analyze "+file)
		if tag.Type == media.TagScript && meta == nil && flv.IsMetadata(tag.Data) {
			meta = metadataObject(tag.Data)
		}
	}
	return &a.Report, meta
}

// metadataObject is the object of an onMetaData script tag.
func metadataObject(data []byte) map[string]any {
	values, _ := rtmp.DecodeAMF0(data)
	for _, v := range values {
		if m, ok := v.(map[string]any); ok {
			return m
		}
	}
	return nil
}

// checkDvrMedia checks the report of a recorded file against the report of
// the media published: each codec, an empty one for a track the file must
// not have, every frame and keyframe, one sequence header of each track,
// and timestamps that only go forward, without gaps.
func checkDvrMedia(c *check.C, r, want *report.Report, vcodec, acodec string) {
	c.Helper()
	if vcodec == "" {
		c.Check(r.Video == nil, "video %v, want none", r.Video)
	} else if c.Check(r.Video != nil, "no video: %v", r) {
		checkVideo(c, r, vcodec)
		c.CheckEqual(r.Video.Frames, want.Video.Frames, "video frames, as published")
		c.CheckEqual(r.Video.Keyframes, want.Video.Keyframes, "keyframes, as published")
	}
	if acodec == "" {
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
	} else if c.Check(r.Audio != nil, "no audio: %v", r) {
		headers := 1
		if acodec == "mp3" {
			headers = 0
		}
		checkAudio(c, r, acodec, headers)
		c.CheckEqual(r.Audio.Frames, want.Audio.Frames, "audio frames, as published")
	}
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
}

// checkSegments checks the files of a segment plan against the report of the
// media published: each file has both sequence headers, each but the last
// spans dvr_duration, from a keyframe when atKeyframes, and together they have
// every frame.
func checkSegments(c *check.C, files []string, want *report.Report, atKeyframes bool) {
	c.Helper()
	video, audio, leading := 0, 0, 0
	for i, file := range files {
		var r *report.Report
		if strings.HasSuffix(file, ".mp4") {
			var err error
			r, err = mp4.AnalyzeFile(file, report.Until{})
			c.Must(err, "analyze "+file)
		} else {
			r, _ = analyzeDvrFLV(c, file)
		}
		name := fmt.Sprintf("file %d of %d", i+1, len(files))
		if !c.Check(r.Video != nil && r.Audio != nil, "%s has no video or audio: %v", name, r) {
			continue
		}
		c.CheckEqual(r.Video.SequenceHeaders, 1, name+" video sequence headers")
		c.CheckEqual(r.Audio.SequenceHeaders, 1, name+" audio sequence headers")
		if atKeyframes {
			c.CheckEqual(r.Video.LeadingFrames, 0, name+" frames before its first keyframe")
		}
		leading += r.Video.LeadingFrames
		c.Check(r.Video.FirstDTS <= 100*time.Millisecond, "%s starts at %v, want its own timeline from 0", name, r.Video.FirstDTS)
		if i < len(files)-1 {
			// A cut at a keyframe comes up to a 1s GOP after dvr_duration.
			d, most := r.Video.Duration(), dvrSegment+100*time.Millisecond
			if atKeyframes {
				most += time.Second
			}
			c.Check(d >= dvrSegment && d <= most, "%s spans %v, want %v to %v", name, d, dvrSegment, most)
		}
		video += r.Video.Frames
		audio += r.Audio.Frames
	}
	c.CheckEqual(video, want.Video.Frames, "video frames of the files, as published")
	c.CheckEqual(audio, want.Audio.Frames, "audio frames of the files, as published")
	if !atKeyframes {
		c.Check(leading > 0, "every file starts at a keyframe, want a cut between keyframes")
	}
}

// publishedOf is the report of the tags a case published, which may have one
// track only.
func publishedOf(c *check.C, tags []*media.FLVTag) *report.Report {
	c.Helper()
	var a flv.Analyzer
	for _, tag := range tags {
		c.Must(a.Add(tag), "analyze the media")
	}
	return &a.Report
}
