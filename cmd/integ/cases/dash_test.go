// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/dash"
	"srs-integ/internal/media"
	"srs-integ/internal/mp4"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// commonDashSegment is how long the DASH segments of the Common server last:
// SRS cuts a segment at the first keyframe where it already lasts
// dash_fragment, 10s, measured to its last frame, not to the keyframe. On
// the 1s GOP the keyframe at 10s finds 9.96s, so the cut comes at 11s.
const commonDashSegment = 11 * time.Second

// TestDash_Common publishes over RTMP without real-time pacing, and plays the
// DASH of the Common server: its MPD, and the init and m4s segments of the
// video and audio representations, at the default names and 10s fragments.
func TestDash_Common(t *testing.T) {
	t.Parallel()

	// Two 11s segments make the 15s the player needs.
	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 2*commonDashSegment+time.Second)
		r := playDASH(c, flvURL(srs, stream+".mpd"), segmentsUntil)
		checkPlayed(c, r, "h264", "aac")
		c.Check(r.Video.Keyframes >= 15, "%v keyframes, want a 1s GOP over 15s", r.Video.Keyframes)
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishHevcThrough(c, srs, stream, commonDashSegment+time.Second)
		r := playDASH(c, flvURL(srs, stream+".mpd"), segmentUntil)
		checkPlayed(c, r, "hevc", "aac")
		c.Check(r.Video.Keyframes >= 8, "%v keyframes, want a 1s GOP over 8s", r.Video.Keyframes)
	})

	// The MPD, the init files and the segments are at the default names
	// under the default dir, and served with their content types.
	check.Case(t, "DefaultFiles", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		stream := "live/" + name
		publishThrough(c, srs, stream, media.H264HighAAC, commonDashSegment+time.Second)
		m, streams := waitMPD(c, flvURL(srs, stream+".mpd"), hasDashSegment(0))
		c.CheckEqual(m.BaseURL, name+"/", "BaseURL")

		dir := filepath.Join(srs.Dir, "objs", "nginx", "html")
		for _, f := range []string{stream + ".mpd", stream + "/video-init.mp4", stream + "/audio-init.mp4", stream + "/video-0.m4s", stream + "/audio-0.m4s"} {
			_, err := os.Stat(filepath.Join(dir, f))
			c.CheckNoError(err, "the file "+f)
		}
		res, _ := httpGet(c, httpClient, flvURL(srs, stream+".mpd"))
		c.CheckEqual(res.Header.Get("Content-Type"), "application/dash+xml", "content type of the MPD")
		for _, s := range streams {
			c.CheckEqual(urlPath(c, s.Init), "/"+stream+"/"+s.Kind+"-init.mp4", s.Kind+" init path")
			res, _ = httpGet(c, httpClient, s.Init)
			c.CheckEqual(res.StatusCode, 200, "status of "+s.Init)
			c.CheckEqual(res.Header.Get("Content-Type"), "video/mp4", "content type of "+s.Init)
			c.CheckEqual(urlPath(c, s.Segments[0].URL), "/"+stream+"/"+s.Kind+"-0.m4s", s.Kind+" segment path")
			res, _ = httpGet(c, httpClient, s.Segments[0].URL)
			c.CheckEqual(res.StatusCode, 200, "status of "+s.Segments[0].URL)
			c.CheckEqual(res.Header.Get("Content-Type"), "video/iso.segment", "content type of "+s.Segments[0].URL)
		}
		if v := dashStream(streams, "video"); v != nil {
			c.CheckEqual(fmt.Sprint(v.Segments[0].Time, v.Segments[0].Duration), "0 11000", "the first video segment's time and duration")
		}
	})

	check.Case(t, "NeverPublished", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		for _, p := range []string{stream + ".mpd", stream + "/video-init.mp4", stream + "/video-0.m4s"} {
			res, _ := httpGet(c, httpClient, flvURL(srs, p))
			c.CheckEqual(res.StatusCode, 404, p)
		}
	})

	check.Case(t, "MissingSegment", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, commonDashSegment+time.Second)
		_, streams := waitMPD(c, flvURL(srs, stream+".mpd"), hasDashSegment(0))
		res, _ := httpGet(c, httpClient, streams[0].Segments[0].URL)
		c.CheckEqual(res.StatusCode, 200, "status of a listed segment")
		res, _ = httpGet(c, httpClient, flvURL(srs, stream+"/video-99.m4s"))
		c.CheckEqual(res.StatusCode, 404, "status of a segment that is not there")
	})
}

// optionsDashSegment is how long the DASH segments of TestDash_Options and
// TestDash_StreamDir last: their 2s dash_fragment cuts at the keyframe at
// 3s, like commonDashSegment.
const optionsDashSegment = 3 * time.Second

// dashDir is where the RemuxOff server writes DASH, dash_path, under the
// dir its HTTP server serves.
const dashDir = "dash"

// TestDash_Options plays DASH on the shared RemuxOff server (see
// sharedRemuxOff), whose DASH settings are 2s fragments, a window of 3
// segments, the files under ./objs/nginx/html/dash, which the HTTP server
// serves as /dash, and the MPD at [app]/[stream]-manifest.mpd. The segments
// are in a dir per stream beside the MPD.
func TestDash_Options(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	// publishSegments publishes the start of a test file at once, through
	// the keyframe that closes n segments of 3s, and waits for the MPD to
	// list the last of them in both representations. It publishes no more
	// than a case needs, since the other groups share the server's thread.
	publishSegments := func(c *check.C, stream, file string, n int) (*dash.MPD, []dash.Stream) {
		publishThrough(c, srs, stream, file, time.Duration(n)*optionsDashSegment+time.Second)
		return waitMPD(c, flvURL(srs, dashDir+"/"+stream+"-manifest.mpd"), hasDashSegment(n-1))
	}

	// The presentation and its two representations, as a live profile
	// player needs them.
	check.Case(t, "MpdTags", check.Normal, func(c *check.C) {
		name := streamName(c)
		start := time.Now()
		m, _ := publishSegments(c, "live/"+name, media.H264HighAAC, 3)
		res, _ := httpGet(c, httpClient, flvURL(srs, dashDir+"/live/"+name+"-manifest.mpd"))
		c.CheckEqual(res.Header.Get("Content-Type"), "application/dash+xml", "content type")

		c.Check(strings.Contains(m.Profiles, "urn:mpeg:dash:profile:isoff-live:2011"), "profiles %q, want the live profile", m.Profiles)
		c.CheckEqual(m.Type, "dynamic", "type")
		c.CheckEqual(m.BaseURL, name+"/", "BaseURL")
		available, err := time.Parse(time.RFC3339, m.AvailabilityStartTime)
		c.CheckNoError(err, "availabilityStartTime")
		published, err := time.Parse(time.RFC3339, m.PublishTime)
		c.CheckNoError(err, "publishTime")
		// The times are in whole seconds; the stream starts at 0 now.
		c.Check(!available.Before(start.Truncate(time.Second)) && !available.After(time.Now()), "availabilityStartTime %v, want the publish at %v", available, start)
		c.Check(!published.Before(available), "publishTime %v before availabilityStartTime %v", published, available)
		if !c.Check(len(m.Periods) == 1, "periods %+v, want one", m.Periods) {
			return
		}
		c.CheckEqual(m.Periods[0].Start, "PT0S", "period start")

		var kinds []string
		for _, as := range m.Periods[0].AdaptationSets {
			kinds = append(kinds, as.MimeType)
			c.Check(as.SegmentAlignment && as.StartWithSAP == 1, "%v: segmentAlignment %v, startWithSAP %v, want aligned segments that start with a SAP", as.MimeType, as.SegmentAlignment, as.StartWithSAP)
			if !c.Check(len(as.Representations) == 1, "%v: representations %+v, want one", as.MimeType, as.Representations) {
				continue
			}
			rep := as.Representations[0]
			c.CheckEqual(rep.ID, strings.TrimSuffix(as.MimeType, "/mp4"), "representation id")
			c.Check(rep.Bandwidth > 0, "%v: bandwidth %v", rep.ID, rep.Bandwidth)
			if c.Check(rep.SegmentTemplate != nil, "%v: no SegmentTemplate", rep.ID) {
				st := rep.SegmentTemplate
				c.CheckEqual(st.Initialization, "$RepresentationID$-init.mp4", rep.ID+" initialization")
				c.CheckEqual(st.Media, "$RepresentationID$-$Number$.m4s", rep.ID+" media")
				c.CheckEqual(st.Timescale, uint64(1000), rep.ID+" timescale")
			}
			switch rep.ID {
			case "video":
				c.Check(strings.HasPrefix(rep.Codecs, "avc1."), "video codecs %q, want avc1", rep.Codecs)
				c.CheckEqual(fmt.Sprint(rep.Width, "x", rep.Height), "768x320", "video size")
			case "audio":
				c.CheckEqual(rep.Codecs, "mp4a.40.2", "audio codecs, AAC LC")
			}
		}
		c.CheckEqual(fmt.Sprint(kinds), "[audio/mp4 video/mp4]", "adaptation sets")
	})

	// The segments of each timeline follow each other without a gap, and
	// the video segment n starts at 3n s and lasts 3s on the 2s fragment;
	// each audio segment ends at the first audio frame of the next video
	// segment.
	check.Case(t, "SegmentTimeline", check.Normal, func(c *check.C) {
		_, streams := publishSegments(c, "live/"+streamName(c), media.H264HighAAC, 3)
		for _, s := range streams {
			for i, seg := range s.Segments {
				if i > 0 {
					prev := s.Segments[i-1]
					c.CheckEqual(seg.Number, prev.Number+1, s.Kind+" number")
					c.CheckEqual(seg.Time, prev.Time+prev.Duration, fmt.Sprintf("%v %v time, where the one before ends", s.Kind, seg.Number))
				}
				if s.Kind == "video" {
					c.CheckEqual(seg.Time, uint64(seg.Number)*3000, fmt.Sprintf("video %v time", seg.Number))
					c.CheckEqual(seg.Duration, uint64(3000), fmt.Sprintf("video %v duration", seg.Number))
				} else {
					end, want := seg.Time+seg.Duration, uint64(seg.Number+1)*3000
					c.Check(end >= want && end < want+50, "audio %v ends at %v, want the first frame from %v", seg.Number, end, want)
				}
			}
		}
	})

	// Each init file has its one track, with no samples.
	check.Case(t, "InitFiles", check.Normal, func(c *check.C) {
		_, streams := publishSegments(c, "live/"+streamName(c), media.H264HighAAC, 3)
		c.CheckEqual(len(streams), 2, "streams")
		for _, s := range streams {
			init := dashInit(c, s)
			if !c.Check(len(init.Tracks) == 1, "%v init tracks %+v, want one", s.Kind, init.Tracks) {
				continue
			}
			tr := init.Tracks[0]
			c.CheckEqual(len(tr.Samples), 0, s.Kind+" init samples")
			if s.Kind == "video" {
				c.CheckEqual(fmt.Sprint(tr.Handler, ":", tr.Codec, " ", tr.Width, "x", tr.Height), "vide:avc1 768x320", "video track")
			} else {
				c.CheckEqual(fmt.Sprint(tr.Handler, ":", tr.Codec, " ", tr.SampleRate, "Hz ", tr.Channels, "ch"), "soun:mp4a 44100Hz 2ch", "audio track")
			}
		}
	})

	// Each video segment starts with a keyframe and holds 3 GOPs of 1s, the
	// next starting where it ends; each audio segment is AAC.
	check.Case(t, "SegmentsOnGop", check.Normal, func(c *check.C) {
		_, streams := publishSegments(c, "live/"+streamName(c), media.H264HighAAC, 3)
		for _, s := range streams {
			init := dashInit(c, s)
			var first []time.Duration
			for _, seg := range s.Segments[len(s.Segments)-3:] {
				r := dashSegmentReport(c, init, seg)
				if s.Kind == "audio" {
					if c.Check(r.Audio != nil && r.Video == nil, "audio %v: %v", seg.Number, r) {
						checkAudio(c, r, "aac", 1)
					}
					continue
				}
				if !c.Check(r.Video != nil && r.Audio == nil, "video %v: %v", seg.Number, r) {
					return
				}
				c.CheckEqual(r.Video.Codec, "h264", "codec")
				c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
				c.CheckEqual(r.Video.Keyframes, 3, "keyframes")
				c.Check(near(r.Video.Duration(), optionsDashSegment, 50*time.Millisecond), "video %v lasts %v, want 3s", seg.Number, r.Video.Duration())
				first = append(first, r.Video.FirstDTS)
			}
			for i := 1; i < len(first); i++ {
				c.Check(near(first[i]-first[i-1], optionsDashSegment, time.Millisecond), "a video segment starts %v after the one before, want 3s", first[i]-first[i-1])
			}
		}
	})

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishSegments(c, stream, media.H264HighAAC, 3)
		r := playDASH(c, flvURL(srs, dashDir+"/"+stream+"-manifest.mpd"), segmentUntil)
		checkPlayed(c, r, "h264", "aac")
		c.Check(r.Video.Keyframes >= 8, "%v keyframes, want a 1s GOP over 8s", r.Video.Keyframes)
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishHevcThrough(c, srs, stream, 3*optionsDashSegment+time.Second)
		_, streams := waitMPD(c, flvURL(srs, dashDir+"/"+stream+"-manifest.mpd"), hasDashSegment(2))
		if v := dashStream(streams, "video"); c.Check(v != nil, "no video stream") {
			init := dashInit(c, *v)
			if c.Check(len(init.Tracks) == 1, "video init tracks %+v", init.Tracks) {
				c.Check(init.Tracks[0].Codec == "hvc1" || init.Tracks[0].Codec == "hev1", "video init codec %v, want hvc1 or hev1", init.Tracks[0].Codec)
			}
		}
		r := playDASH(c, flvURL(srs, dashDir+"/"+stream+"-manifest.mpd"), segmentUntil)
		checkPlayed(c, r, "hevc", "aac")
		c.Check(r.Video.Keyframes >= 8, "%v keyframes, want a 1s GOP over 8s", r.Video.Keyframes)
	})

	// An encoder's info frame, such as the colorInfo metadata ffmpeg sends
	// with HEVC over enhanced RTMP, is not a picture, so it is not a sample:
	// the first segment has the 75 frames of its 3s, and each is an HEVC
	// access unit.
	check.Case(t, "HevcInfoFrameSkipped", check.Normal, func(c *check.C) {
		_, streams := publishSegments(c, "live/"+streamName(c), media.HEVCAAC, 3)
		v := dashStream(streams, "video")
		if !c.Check(v != nil, "no video stream") {
			return
		}
		init := dashInit(c, *v)
		seg := v.Segments[0]
		res, body := httpGet(c, httpClient, seg.URL)
		c.CheckEqual(res.StatusCode, 200, "status of "+seg.URL)
		samples, err := mp4.ParseFragments(init, body)
		c.Must(err, "parse "+seg.URL)
		c.CheckEqual(len(samples), 75, "samples of the first 3s")
		var a mp4.Analyzer
		c.CheckNoError(a.Add(samples, report.Until{}), "analyze "+seg.URL)
	})

	// The codecs of the video representation name its codec, so a player
	// can tell whether it can play it before it fetches a segment.
	check.Case(t, "HevcCodecs", check.Normal, func(c *check.C) {
		_, streams := publishSegments(c, "live/"+streamName(c), media.HEVCAAC, 3)
		if v := dashStream(streams, "video"); c.Check(v != nil, "no video stream") {
			codecs := v.Representation.Codecs
			c.Check(strings.HasPrefix(codecs, "hvc1.") || strings.HasPrefix(codecs, "hev1."), "video codecs %q, want hvc1 or hev1", codecs)
		}
	})

	// dash_path and dash_mpd_file: the files are where the templates put
	// them, and nothing is at the default names.
	check.Case(t, "PathTemplates", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishSegments(c, stream, media.H264HighAAC, 3)
		html := filepath.Join(srs.Dir, "objs", "nginx", "html")
		for _, f := range []string{stream + "-manifest.mpd", stream + "/video-init.mp4", stream + "/audio-init.mp4", stream + "/video-2.m4s", stream + "/audio-2.m4s"} {
			_, err := os.Stat(filepath.Join(html, dashDir, f))
			c.CheckNoError(err, "the file "+f+" under dash_path")
		}
		for _, f := range []string{stream + ".mpd", stream + "-manifest.mpd", stream + "/video-init.mp4"} {
			_, err := os.Stat(filepath.Join(html, f))
			c.Check(errors.Is(err, os.ErrNotExist), "%v under the default dash_path: %v, want none", f, err)
		}
		res, _ := httpGet(c, httpClient, flvURL(srs, stream+".mpd"))
		c.CheckEqual(res.StatusCode, 404, "status of the default MPD path")
	})

	// dash_window_size 3: the MPD lists the last 3 of the 5 segments of
	// each representation.
	check.Case(t, "WindowSize", check.Normal, func(c *check.C) {
		_, streams := publishSegments(c, "live/"+streamName(c), media.H264HighAAC, 5)
		for _, s := range streams {
			c.CheckEqual(fmt.Sprint(segmentNumbers(s)), "[2 3 4]", s.Kind+" segments in the window")
		}
	})

	// The MPD asks players to fetch it again every dash_update_period, 5s
	// by default.
	check.Case(t, "UpdatePeriod", check.Normal, func(c *check.C) {
		m, _ := publishSegments(c, "live/"+streamName(c), media.H264HighAAC, 3)
		d, err := dash.Duration(m.MinimumUpdatePeriod)
		c.CheckNoError(err, "minimumUpdatePeriod")
		c.CheckEqual(d, 5*time.Second, "minimumUpdatePeriod")
	})
}

// TestDash_StreamDir plays DASH on the shared Fmp4Keys server (see
// sharedFmp4Keys), whose MPD is at [app]/[stream]/index.mpd, in a dir per
// stream, with 2s fragments and the default window of 5 segments.
func TestDash_StreamDir(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())

	// publishSegments publishes the start of the H.264 test file at once,
	// through the keyframe that closes n segments of 3s, and waits for the
	// MPD to list the last of them in both representations.
	publishSegments := func(c *check.C, stream string, n int) []dash.Stream {
		publishThrough(c, srs, stream, media.H264HighAAC, time.Duration(n)*optionsDashSegment+time.Second)
		_, streams := waitMPD(c, flvURL(srs, stream+"/index.mpd"), hasDashSegment(n-1))
		return streams
	}

	// The MPD lists the last 5 of the 7 segments of each representation.
	check.Case(t, "DefaultWindow", check.Normal, func(c *check.C) {
		streams := publishSegments(c, "live/"+streamName(c), 7)
		c.CheckEqual(len(streams), 2, "streams")
		for _, s := range streams {
			c.CheckEqual(fmt.Sprint(segmentNumbers(s)), "[2 3 4 5 6]", s.Kind+" segments in the window")
		}
		if v := dashStream(streams, "video"); v != nil && len(v.Segments) > 0 {
			c.CheckEqual(v.Segments[0].Time, uint64(6000), "the time of the first video segment in the window")
		}
	})

	// The segments are in the dir of the MPD's BaseURL, and a player looks
	// for the init files there too.
	check.Case(t, "InitBesideSegments", check.Normal, func(c *check.C) {
		name := streamName(c)
		stream := "live/" + name
		streams := publishSegments(c, stream, 1)
		for _, s := range streams {
			seg := s.Segments[len(s.Segments)-1]
			c.CheckEqual(path.Dir(urlPath(c, seg.URL)), "/"+stream+"/"+name, s.Kind+" segment dir")
			res, _ := httpGet(c, httpClient, seg.URL)
			c.CheckEqual(res.StatusCode, 200, "status of "+seg.URL)
		}
		for _, s := range streams {
			res, _ := httpGet(c, httpClient, s.Init)
			c.CheckEqual(res.StatusCode, 200, "status of "+s.Init)
		}
	})
}

// playDASH plays a DASH MPD URL until u, for at most 2s.
func playDASH(c *check.C, raw string, u report.Until) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	r, err := dash.PlayHTTP(ctx, httpClient, raw, u)
	c.Must(err, "play")
	return r
}

// hasDashSegment is an MPD whose video and audio both list the segment of
// number n.
func hasDashSegment(n int) func([]dash.Stream) bool {
	return func(streams []dash.Stream) bool {
		found := 0
		for _, s := range streams {
			for _, seg := range s.Segments {
				if seg.Number == n {
					found++
					break
				}
			}
		}
		return len(streams) == 2 && found == 2
	}
}

// waitMPD fetches an MPD every 10ms until its streams are ok, for at most
// 1s, and returns it with its streams.
func waitMPD(c *check.C, raw string, ok func([]dash.Stream) bool) (*dash.MPD, []dash.Stream) {
	c.Helper()
	base, err := url.Parse(raw)
	c.Must(err, "parse "+raw)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	var last []dash.Stream
	for {
		res, body := httpGet(c, httpClient, raw)
		if res.StatusCode == http.StatusOK {
			m, err := dash.ParseMPD(body)
			c.Must(err, "parse the MPD")
			streams, err := m.Streams(base)
			c.Must(err, "the MPD's streams")
			if ok(streams) {
				return m, streams
			}
			last = streams
		} else if res.StatusCode != http.StatusNotFound {
			c.Fatalf("MPD %v: status %v", raw, res.Status)
		}
		select {
		case <-ctx.Done():
			c.Fatalf("MPD %v: %+v", raw, last)
		case <-time.After(10 * time.Millisecond):
		}
	}
}

// dashStream is the stream of a kind, "video" or "audio", or nil.
func dashStream(streams []dash.Stream, kind string) *dash.Stream {
	for i := range streams {
		if streams[i].Kind == kind {
			return &streams[i]
		}
	}
	return nil
}

// segmentNumbers are the numbers of a stream's segments.
func segmentNumbers(s dash.Stream) []int {
	var n []int
	for _, seg := range s.Segments {
		n = append(n, seg.Number)
	}
	return n
}

// dashInit fetches the init file of a stream, which must be there.
func dashInit(c *check.C, s dash.Stream) *mp4.Init {
	c.Helper()
	res, body := httpGet(c, httpClient, s.Init)
	c.CheckEqual(res.StatusCode, 200, "status of "+s.Init)
	init, err := mp4.ParseInit(body)
	c.Must(err, "parse "+s.Init)
	return init
}

// dashSegmentReport fetches a segment, which must be there, and analyzes it
// whole with its init file.
func dashSegmentReport(c *check.C, init *mp4.Init, seg dash.SegmentURL) *report.Report {
	c.Helper()
	res, body := httpGet(c, httpClient, seg.URL)
	c.CheckEqual(res.StatusCode, 200, "status of "+seg.URL)
	samples, err := mp4.ParseFragments(init, body)
	c.Must(err, "parse "+seg.URL)
	var a mp4.Analyzer
	c.Must(a.Add(samples, report.Until{}), "analyze "+seg.URL)
	return &a.Report
}

// urlPath is the path of a URL.
func urlPath(c *check.C, raw string) string {
	c.Helper()
	u, err := url.Parse(raw)
	c.Must(err, "parse "+raw)
	return u.Path
}

// publishHevcThrough publishes the HEVC test file at once, from its start
// through the tags at d, without its info frames, and keeps the stream
// published until the case ends, like publishThrough. ffmpeg sends one
// info frame, the colorInfo metadata of enhanced RTMP, which
// TestDash_Options/HevcInfoFrameSkipped covers on its own.
func publishHevcThrough(c *check.C, srs *server.Server, stream string, d time.Duration) {
	c.Helper()
	var tags []*media.FLVTag
	for _, tag := range readTags(c, media.HEVCAAC, 0, d+time.Millisecond) {
		if !isInfoFrame(tag) {
			tags = append(tags, tag)
		}
	}
	pub := publishOnly(c, rtmpURL(srs, stream), rtmp.FMLE)
	publishTagsOn(c, srs, pub, tags)
}

// isInfoFrame reports whether a tag is a video info or command frame,
// frame type 5, in a legacy or an enhanced RTMP header.
func isInfoFrame(tag *media.FLVTag) bool {
	return tag.Type == 9 && len(tag.Data) > 0 && (tag.Data[0]>>4)&0x07 == 5
}
