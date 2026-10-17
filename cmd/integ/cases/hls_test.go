// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
	"srs-integ/internal/ts"
)

// TestHls_Common publishes over RTMP without real-time pacing, and plays the
// HLS of the Common server, with its TS segments, at SRS's defaults: 10s
// segments, a 60s window, and the hls_ctx master playlist.
func TestHls_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlayHLS(c, media.H264HighAAC, "h264", "aac", segmentsUntil)
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlayHLS(c, media.HEVCAAC, "hevc", "aac", segmentUntil)
	})

	check.Case(t, "PublishPlayMp3", check.Normal, func(c *check.C) {
		r := playHLSOf(c, server.Common(c), media.H264HighMP3, httpClient, false, report.Until{VideoHeader: true, Duration: segmentUntil.Duration})
		checkVideo(c, r, "h264")
		checkAudio(c, r, "mp3", 0)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	check.Case(t, "PublishPlayVideoOnly", check.Normal, func(c *check.C) {
		r := playHLSOf(c, server.Common(c), media.H264Only, httpClient, false, report.Until{VideoHeader: true, Duration: segmentUntil.Duration})
		checkVideo(c, r, "h264")
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
		c.Check(r.Video.Keyframes >= 8, "%v keyframes, want a 1s GOP over 8s", r.Video.Keyframes)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	// With no video, SRS cuts a segment at hls_aof_ratio times hls_fragment,
	// 2.1 by 10s by default, not on keyframes, plus at most a batch of audio.
	check.Case(t, "PublishPlayAudioOnly", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.AACOnly, 23*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		checkDurations(c, pl, 21*time.Second, 21*time.Second+pureAudioBatch)

		r := playHLS(c, httpClient, flvURL(srs, stream+".m3u8"), report.Until{AudioHeader: true, Duration: 15 * time.Second})
		checkAudio(c, r, "aac", 1)
		c.Check(r.Video == nil, "video %v, want none", r.Video)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	check.Case(t, "HttpsPublishPlay", check.Normal, func(c *check.C) {
		cfg, err := server.ClientTLS()
		c.Must(err, "the client TLS config")
		tr := &http.Transport{TLSClientConfig: cfg}
		c.Cleanup(tr.CloseIdleConnections)
		client := &http.Client{Transport: tr}

		r := playHLSOf(c, server.Common(c), media.H264HighAAC, client, true, segmentUntil)
		checkPlayed(c, r, "h264", "aac")
	})

	// The master playlist of a new session, then its media playlist: the
	// tags, the sequence numbers from 0, and the 10s segments of a 20s stream
	// that is still published, which has two closed segments.
	check.Case(t, "PlaylistTags", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 20*time.Second)
		pl, mediaURL := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(1))

		// A new session gets a master playlist with its hls_ctx.
		res, body := httpGet(c, httpClient, flvURL(srs, stream+".m3u8"))
		c.CheckEqual(res.StatusCode, 200, "status of the master playlist")
		c.CheckEqual(res.Header.Get("Content-Type"), "application/vnd.apple.mpegurl", "content type of the master playlist")
		base, _ := url.Parse(flvURL(srs, stream+".m3u8"))
		master, err := hls.Parse(body, base)
		c.Must(err, "parse the master playlist")
		if c.Check(master.Master && len(master.Variants) == 1, "master playlist %+v, want one variant", master) {
			v, _ := url.Parse(master.Variants[0].URI)
			c.CheckEqual(v.Path, "/"+stream+".m3u8", "variant path")
			c.Check(v.Query().Get("hls_ctx") != "", "variant %v, want an hls_ctx", v)
		}

		res, _ = httpGet(c, httpClient, mediaURL)
		c.CheckEqual(res.Header.Get("Content-Type"), "application/vnd.apple.mpegurl", "content type of the media playlist")
		c.CheckEqual(pl.Version, 3, "version")
		c.CheckEqual(pl.TargetDuration, 10, "target duration")
		c.CheckEqual(pl.MediaSequence, 0, "media sequence")
		c.Check(!pl.EndList, "a live playlist has no #EXT-X-ENDLIST")
		ctx := query(c, mediaURL).Get("hls_ctx")
		for i, s := range pl.Segments {
			u, _ := url.Parse(s.URI)
			c.CheckEqual(u.Path, fmt.Sprintf("/%v-%v.ts", stream, i), "segment path")
			c.CheckEqual(u.Query().Get("hls_ctx"), ctx, "segment hls_ctx")
			c.CheckEqual(s.Sequence, i, "segment sequence")
		}
		c.CheckEqual(len(pl.Segments), 2, "closed segments")
		if len(pl.Segments) > 0 {
			c.Check(pl.Segments[0].Discontinuity, "the first segment has no discontinuity, want one at the start of a publish")
		}
		checkDurations(c, pl, 10*time.Second, 10100*time.Millisecond)
	})

	// Each segment starts with a keyframe and holds 10 GOPs of 1s, and the
	// next starts where it ends.
	check.Case(t, "SegmentsOnGop", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 20*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(1))

		var reports []*report.Report
		for _, s := range pl.Segments {
			r := segmentReport(c, httpClient, s.URI)
			if !c.Check(r.Video != nil && r.Audio != nil, "segment %v: %v", s.Sequence, r) {
				return
			}
			c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
			c.CheckEqual(r.Video.Keyframes, 10, "keyframes")
			c.Check(near(r.Video.Duration(), 10*time.Second, 50*time.Millisecond), "video %v, want 10s", r.Video.Duration())
			reports = append(reports, r)
		}
		if len(reports) == 2 {
			step := reports[1].Video.FirstDTS - reports[0].Video.FirstDTS
			c.Check(near(step, 10*time.Second, time.Millisecond), "the second segment starts %v after the first, want 10s", step)
		}
	})

	check.Case(t, "NeverPublished", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		for _, p := range []string{stream + ".m3u8", stream + "-0.ts"} {
			res, _ := httpGet(c, httpClient, flvURL(srs, p))
			c.CheckEqual(res.StatusCode, 404, p)
		}
	})

	// The playlist of a published stream, asked under another app.
	check.Case(t, "MissingPlaylist", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		publishThrough(c, srs, "live/"+name, media.H264HighAAC, 10*time.Second)
		waitPlaylist(c, httpClient, flvURL(srs, "live/"+name+".m3u8"), hasSegment(0))
		res, _ := httpGet(c, httpClient, flvURL(srs, "other/"+name+".m3u8"))
		c.CheckEqual(res.StatusCode, 404, "status")
	})

	check.Case(t, "MissingSegment", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 10*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		res, _ := httpGet(c, httpClient, pl.Segments[0].URI)
		c.CheckEqual(res.StatusCode, 200, "status of a listed segment")
		res, _ = httpGet(c, httpClient, flvURL(srs, stream+"-99.ts"))
		c.CheckEqual(res.StatusCode, 404, "status of a segment that is not there")
	})
}

// pureAudioBatch is how much more than its length a segment with no video
// can last: SRS writes such audio in batches of up to 720ms, and cuts the
// segment only between batches.
const pureAudioBatch = 800 * time.Millisecond

// cdnHost is the hls_entry_prefix of the Options variant: a CDN in front of
// SRS, which the cases reach through cdnClient.
const cdnHost = "cdn.example.com"

// TestHls_Options is a variant with the HLS settings Common keeps at their
// defaults: 2s fragments with hls_td_ratio 1.5, so segments of 3s on the 1s
// GOP; hls_aof_ratio 2.5, so 5s segments with no video; a 7s window; the
// files under ./hls, with the playlist and segments in a dir per stream; and
// an entry prefix before each segment URI.
func TestHls_Options(t *testing.T) {
	t.Parallel()
	cfg := server.HLSConfig(server.HLS{
		Fragment:    2 * time.Second,
		TDRatio:     1.5,
		AOFRatio:    2.5,
		Window:      7 * time.Second,
		Path:        "./hls",
		M3U8File:    "[app]/[stream]/index.m3u8",
		TSFile:      "[app]/[stream]/seg-[seq].ts",
		EntryPrefix: "http://" + cdnHost,
	})
	// Every hook on a mock, for the sessions' on_play.
	cfg.Hooks = true
	srs := server.Group(t, cfg)

	// publish30s publishes the 30s test file at once and waits for its last
	// closed segment, 8: segments of 3s are cut at 3s, 6s, up to 27s.
	publish30s := func(c *check.C, stream string) *hls.Playlist {
		publishAll(c, srs, stream, media.Path(c, media.H264HighAAC))
		pl, _ := waitPlaylist(c, cdnClient(c, srs), flvURL(srs, stream+"/index.m3u8"), hasSegment(8))
		return pl
	}

	// The playlist keeps the last segments that pass the window: 3 of 3s for
	// 7s, so it starts at 6.
	check.Case(t, "WindowAndSequence", check.Normal, func(c *check.C) {
		pl := publish30s(c, "live/"+streamName(c))
		c.CheckEqual(pl.TargetDuration, 3, "target duration, the fragment times hls_td_ratio")
		c.CheckEqual(pl.MediaSequence, 6, "media sequence")
		c.CheckEqual(len(pl.Segments), 3, "segments in the window")
		checkDurations(c, pl, 3*time.Second, 3100*time.Millisecond)
		var all, rest time.Duration
		for i, s := range pl.Segments {
			all += s.Duration
			if i > 0 {
				rest += s.Duration
			}
		}
		c.Check(all > 7*time.Second && rest <= 7*time.Second, "segments of %v, %v without the first, want the fewest that pass 7s", all, rest)
	})

	// hls_cleanup, on by default, removes the segments that left the window.
	check.Case(t, "ExpiredSegmentsRemoved", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publish30s(c, stream)
		dir := filepath.Join(srs.Dir, "hls", stream)
		for i := range 9 {
			_, err := os.Stat(filepath.Join(dir, fmt.Sprintf("seg-%v.ts", i)))
			if i < 6 {
				c.Check(errors.Is(err, os.ErrNotExist), "seg-%v.ts: %v, want it removed", i, err)
			} else {
				c.CheckNoError(err, fmt.Sprintf("seg-%v.ts", i))
			}
		}
		res, _ := httpGet(c, httpClient, flvURL(srs, stream+"/seg-0.ts"))
		c.CheckEqual(res.StatusCode, 404, "status of an expired segment")
		res, _ = httpGet(c, httpClient, flvURL(srs, stream+"/seg-6.ts"))
		c.CheckEqual(res.StatusCode, 200, "status of a segment in the window")
	})

	// hls_path, hls_m3u8_file and hls_ts_file: the files are where the
	// templates put them, and the HTTP server serves them from there.
	check.Case(t, "PathTemplates", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publish30s(c, stream)
		for _, f := range []string{"index.m3u8", "seg-8.ts"} {
			_, err := os.Stat(filepath.Join(srs.Dir, "hls", stream, f))
			c.CheckNoError(err, "the file "+f+" under hls_path")
		}
		_, err := os.Stat(filepath.Join(srs.Dir, "objs", "nginx", "html", stream+".m3u8"))
		c.Check(errors.Is(err, os.ErrNotExist), "the default playlist: %v, want none", err)
		res, _ := httpGet(c, httpClient, flvURL(srs, stream+".m3u8"))
		c.CheckEqual(res.StatusCode, 404, "status of the default playlist path")
	})

	// Each segment URI is the prefix, the playlist's dir and the segment, and
	// a player through the CDN plays them.
	check.Case(t, "EntryPrefix", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		pl := publish30s(c, stream)
		for _, s := range pl.Segments {
			u, _ := url.Parse(s.URI)
			c.CheckEqual(u.Host, cdnHost, "segment host")
			c.CheckEqual(u.Path, fmt.Sprintf("/%v/seg-%v.ts", stream, s.Sequence), "segment path")
		}
		r := playHLS(c, cdnClient(c, srs), flvURL(srs, stream+"/index.m3u8"), report.Until{VideoHeader: true, AudioHeader: true, Duration: 5 * time.Second})
		checkPlayed(c, r, "h264", "aac")
	})

	// A republish starts a new segment with a discontinuity, and the sequence
	// numbers go on; the first publish has one at its first segment too.
	// This variant has no HTTP remux, so the republish doesn't wait while SRS
	// tears down an HTTP-FLV mount, which Common does on one worker for every
	// stream.
	check.Case(t, "RepublishDiscontinuity", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		client := cdnClient(c, srs)
		pub := publishThrough(c, srs, stream, media.H264HighAAC, 5*time.Second)
		_, mediaURL := waitPlaylist(c, client, flvURL(srs, stream+"/index.m3u8"), hasSegment(0))

		// The unpublish closes the last segment, 2s, which ends the first
		// publish.
		pub.Close()
		waitPlaylist(c, client, mediaURL, hasSegment(1))
		pub = republish(c, rtmpURL(srs, stream), 300*time.Millisecond)
		publishTagsOn(c, srs, pub, readTags(c, media.H264HighAAC, 0, 3*time.Second+time.Millisecond))
		pl, _ := waitPlaylist(c, client, mediaURL, hasSegment(2))

		c.CheckEqual(pl.MediaSequence, 0, "media sequence")
		var seqs []int
		for _, s := range pl.Segments {
			seqs = append(seqs, s.Sequence)
			c.CheckEqual(s.Discontinuity, s.Sequence != 1, fmt.Sprintf("discontinuity of segment %v", s.Sequence))
		}
		c.CheckEqual(fmt.Sprint(seqs), "[0 1 2]", "segment sequences")
		if len(pl.Segments) == 3 {
			c.Check(near(pl.Segments[1].Duration, 2*time.Second, 100*time.Millisecond), "segment 1 lasts %v, want the 2s left at the unpublish", pl.Segments[1].Duration)
			checkPlayed(c, segmentReport(c, client, pl.Segments[2].URI), "h264", "aac")
		}
	})

	// With the playlist in a dir per stream, an HLS session is of the stream
	// it plays: its on_play and its API client name the app and the stream
	// the playlist was published with, not the playlist's dir and file name.
	check.Case(t, "SessionNames", check.Normal, func(c *check.C) {
		name := streamName(c)
		publishThrough(c, srs, "live/"+name, media.H264HighAAC, 4*time.Second)
		pub := waitHook(c, srs, "on_publish", name)
		u := flvURL(srs, "live/"+name+"/index.m3u8")
		waitPlaylist(c, cdnClient(c, srs), u, hasSegment(0))

		master := sessionMaster(c, u)
		ctx := query(c, master.Variants[0].URI).Get("hls_ctx")
		play := waitClientHook(c, srs, "on_play", ctx)
		c.CheckEqual(play.Field("app"), "live", "on_play app")
		c.CheckEqual(play.Field("stream"), name, "on_play stream")
		c.CheckEqual(play.Field("tcUrl"), "http://"+srs.Addr(server.HTTPServer)+"/live", "on_play tcUrl")
		c.CheckEqual(play.Field("stream_id"), pub.Field("stream_id"), "on_play stream_id, the publisher's")

		var v struct {
			Code   int `json:"code"`
			Client struct {
				Type   string `json:"type"`
				URL    string `json:"url"`
				Stream string `json:"stream"`
			} `json:"client"`
		}
		apiGet(c, srs, "/api/v1/clients/"+ctx, &v)
		c.CheckEqual(v.Code, 0, "code of the session's client")
		c.CheckEqual(v.Client.Type, "hls-play", "client type")
		c.CheckEqual(v.Client.URL, "/live/"+name, "client url")
		c.CheckEqual(v.Client.Stream, pub.Field("stream_id"), "client stream, the publisher's")
	})

	// With no video, segments are cut at hls_aof_ratio times the fragment,
	// 5s plus at most a batch of audio, not at hls_td_ratio's 3s.
	check.Case(t, "AudioOnlyAofRatio", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishAll(c, srs, stream, media.Path(c, media.AACOnly))
		pl, _ := waitPlaylist(c, cdnClient(c, srs), flvURL(srs, stream+"/index.m3u8"), hasSegment(4))
		checkDurations(c, pl, 5*time.Second, 5*time.Second+pureAudioBatch)
		c.CheckEqual(pl.TargetDuration, 6, "target duration, the longest segment rounded up")
	})
}

// TestHls_TsFloor is hls_ts_floor: segments are named by the wall time over
// the 2s fragment, [timestamp], one more for each segment. Its server, shared
// with TestHls_Keys and TestHls_Sessions, encrypts the segments, so the
// player reaches the keys through the CDN host of hls_key_url.
func TestHls_TsFloor(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsFloor", sharedTsFloor())

	check.Case(t, "FloorNames", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		floor := time.Now().Unix() / 2
		publishAll(c, srs, stream, media.Path(c, media.H264HighAAC))
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(2))

		var names []int64
		for _, s := range pl.Segments {
			u, _ := url.Parse(s.URI)
			n, err := strconv.ParseInt(strings.TrimSuffix(strings.TrimPrefix(path.Base(u.Path), path.Base(stream)+"-"), ".ts"), 10, 64)
			if !c.Check(err == nil, "segment %v, want <stream>-<timestamp>.ts", u.Path) {
				return
			}
			names = append(names, n)
		}
		for i := 1; i < len(names); i++ {
			c.CheckEqual(names[i], names[i-1]+1, "the timestamp after "+strconv.FormatInt(names[i-1], 10))
		}
		// SRS starts one before the current floor.
		c.Check(names[0] >= floor-2 && names[0] <= floor+1, "first timestamp %v, want near %v, the wall time over 2s", names[0], floor-1)
	})

	check.Case(t, "FloorSegmentsPlay", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishAll(c, srs, stream, media.Path(c, media.H264HighAAC))
		r := playHLS(c, cdnClient(c, srs), flvURL(srs, stream+".m3u8"), report.Until{VideoHeader: true, AudioHeader: true, Duration: 5 * time.Second})
		checkPlayed(c, r, "h264", "aac")
	})
}

// segmentUntil is when a player of the Common server's HLS has seen one 10s
// segment play: both sequence headers and 8s of media.
var segmentUntil = report.Until{VideoHeader: true, AudioHeader: true, Duration: 8 * time.Second}

// publishPlayHLS publishes a file at once and plays it from the HLS of the
// Common server until u.
func publishPlayHLS(c *check.C, file, vcodec, acodec string, u report.Until) {
	r := playHLSOf(c, server.Common(c), file, httpClient, false, u)
	checkPlayed(c, r, vcodec, acodec)
	want := int(u.Duration / time.Second)
	c.Check(r.Video.Keyframes >= want, "%v keyframes, want a 1s GOP over %v", r.Video.Keyframes, u.Duration)
}

// playHLSOf publishes the 10s segments of a file that u needs at once, to a
// stream of its own, and plays its HLS from the server, over HTTPS when
// secure, until u.
func playHLSOf(c *check.C, srs *server.Server, file string, client *http.Client, secure bool, u report.Until) *report.Report {
	c.Helper()
	stream := "live/" + streamName(c)
	segments := (u.Duration + 10*time.Second - 1) / (10 * time.Second)
	publishThrough(c, srs, stream, file, segments*10*time.Second)
	addr := "http://" + srs.Addr(server.HTTPServer)
	if secure {
		addr = "https://" + srs.Addr(server.HTTPSServer)
	}
	return playHLS(c, client, addr+"/"+stream+".m3u8", u)
}

// playHLS plays an HLS URL until u, for at most 2s.
func playHLS(c *check.C, client *http.Client, url string, u report.Until) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	r, err := hls.PlayHTTP(ctx, client, url, u)
	c.Must(err, "play")
	return r
}

// publishThrough publishes a test file at once, from its start through the
// tags at d, and keeps the stream published until the case ends. With d at
// a keyframe, a multiple of 1s, that closes the segment before it. Like
// publishAll, it holds a burst slot while it publishes, and it sends no
// more than the case needs, since the bursts share Common's one thread with
// the real-time cases.
func publishThrough(c *check.C, srs *server.Server, stream, file string, d time.Duration) *rtmp.Client {
	c.Helper()
	pub := publishOnly(c, rtmpURL(srs, stream), rtmp.FMLE)
	publishTagsOn(c, srs, pub, readTags(c, file, 0, d+time.Millisecond))
	return pub
}

// publishTagsOn publishes tags at once on a client that publishes, in a
// burst slot of the server.
func publishTagsOn(c *check.C, srs *server.Server, pub *rtmp.Client, tags []*media.FLVTag) {
	c.Helper()
	free := burstOn(c, srs, pub)
	defer free()
	c.Must(writeTags(pub, tags), "publish the media")
}

// hasSegment is a playlist that lists the segment of sequence seq.
func hasSegment(seq int) func(*hls.Playlist) bool {
	return func(pl *hls.Playlist) bool {
		for _, s := range pl.Segments {
			if s.Sequence == seq {
				return true
			}
		}
		return false
	}
}

// waitPlaylist fetches an HLS URL, a master playlist or a media one, every
// 10ms until its media playlist is ok, for at most 1s, and returns it with
// its URL. Once it has the media playlist's URL, it fetches only that, so
// it stays in one hls_ctx session.
func waitPlaylist(c *check.C, client *http.Client, raw string, ok func(*hls.Playlist) bool) (*hls.Playlist, string) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	var last *hls.Playlist
	var err error
	for {
		var pl *hls.Playlist
		var media string
		pl, media, err = hls.Fetch(ctx, client, raw)
		if err == nil {
			raw, last = media, pl
			if ok(pl) {
				return pl, media
			}
		} else if !errors.Is(err, hls.ErrNotFound) {
			break
		}
		select {
		case <-ctx.Done():
			c.Fatalf("playlist %v: %+v, %v", raw, last, err)
		case <-time.After(10 * time.Millisecond):
		}
	}
	c.Fatalf("playlist %v: %v", raw, err)
	return nil, ""
}

// checkDurations checks every segment of a playlist lasts from least to at
// most most.
func checkDurations(c *check.C, pl *hls.Playlist, least, most time.Duration) {
	c.Helper()
	for _, s := range pl.Segments {
		c.Check(s.Duration >= least && s.Duration <= most, "segment %v lasts %v, want %v to %v", s.Sequence, s.Duration, least, most)
	}
}

// segmentReport fetches a TS segment, which must be there, and analyzes it
// whole.
func segmentReport(c *check.C, client *http.Client, uri string) *report.Report {
	c.Helper()
	res, body := httpGet(c, client, uri)
	c.CheckEqual(res.StatusCode, 200, "status of "+uri)
	c.CheckEqual(res.Header.Get("Content-Type"), "video/MP2T", "content type of "+uri)
	r, err := ts.Analyze(ts.NewDemuxer(bytes.NewReader(body)), report.Until{})
	c.Must(err, "analyze "+uri)
	return r
}

// httpGet gets a URL and returns the response with its whole body.
func httpGet(c *check.C, client *http.Client, raw string) (*http.Response, []byte) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, raw, nil)
	c.Must(err, "request "+raw)
	res, err := client.Do(req)
	c.Must(err, "GET "+raw)
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	c.Must(err, "read "+raw)
	return res, body
}

// query is the query of a URL.
func query(c *check.C, raw string) url.Values {
	c.Helper()
	u, err := url.Parse(raw)
	c.Must(err, "parse "+raw)
	return u.Query()
}

// near reports whether d is within e of want.
func near(d, want, e time.Duration) bool {
	return d >= want-e && d <= want+e
}

// cdnClient is an HTTP client whose every connection goes to the server's
// HTTP server, as a CDN in front of it would, so a player can follow the
// absolute URIs of hls_entry_prefix.
func cdnClient(c *check.C, srs *server.Server) *http.Client {
	tr := &http.Transport{DialContext: func(ctx context.Context, network, _ string) (net.Conn, error) {
		var d net.Dialer
		return d.DialContext(ctx, network, srs.Addr(server.HTTPServer))
	}}
	c.Cleanup(tr.CloseIdleConnections)
	return &http.Client{Transport: tr}
}
