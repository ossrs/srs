// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/mp4"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// TestHls_Fmp4 is a variant with HLS in fragmented MP4 (hls_use_fmp4), and
// the settings of TestHls_Options that apply to it: 2s fragments with
// hls_td_ratio 1.5, a 3s target, which makes segments of 4s on the 1s GOP
// (fmp4Segment); hls_aof_ratio 2.5; a 7s window; the files under ./hls; and
// an entry prefix. The segment and init
// files have templates of their own, beside the playlist, which is where SRS
// links them from (TestHls_Fmp4Default shows the default init file is not).
func TestHls_Fmp4(t *testing.T) {
	t.Parallel()
	cfg := server.HLSConfig(server.HLS{
		FMP4:        true,
		Fragment:    2 * time.Second,
		TDRatio:     1.5,
		AOFRatio:    2.5,
		Window:      7 * time.Second,
		Path:        "./hls",
		FMP4File:    "[app]/[stream]-seg-[seq].m4s",
		InitFile:    "[app]/[stream]-init.mp4",
		EntryPrefix: "http://" + cdnHost,
	})
	cfg.Listeners = append(cfg.Listeners, server.HTTPSServer)
	srs := server.Group(t, cfg)

	// play publishes a file through d at once, which closes the 4s segments
	// before d, and plays its HLS until u.
	play := func(c *check.C, file string, d time.Duration, client *http.Client, secure bool, u report.Until) *report.Report {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, file, d)
		addr := "http://" + srs.Addr(server.HTTPServer)
		if secure {
			addr = "https://" + srs.Addr(server.HTTPSServer)
		}
		return playHLS(c, client, addr+"/"+stream+".m3u8", u)
	}
	// fmp4Until is two 4s segments: both sequence headers and 5s of media.
	fmp4Until := report.Until{VideoHeader: true, AudioHeader: true, Duration: 5 * time.Second}

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		r := play(c, media.H264HighAAC, 2*fmp4Segment, httpClient, false, fmp4Until)
		checkPlayed(c, r, "h264", "aac")
		c.Check(r.Video.Keyframes >= 5, "%v keyframes, want a 1s GOP over 5s", r.Video.Keyframes)
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		r := play(c, media.HEVCAAC, 2*fmp4Segment, httpClient, false, fmp4Until)
		checkPlayed(c, r, "hevc", "aac")
		c.Check(r.Video.Keyframes >= 5, "%v keyframes, want a 1s GOP over 5s", r.Video.Keyframes)
	})

	// The init file has the MP3 track, and the segments play.
	check.Case(t, "PublishPlayMp3", check.Normal, func(c *check.C) {
		check.KnownBug(c, "the init file of an MP3 stream has no audio track, while its segments carry the MP3 samples on track 2")
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighMP3, 2*fmp4Segment)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(1))
		c.CheckEqual(fmt.Sprint(initTracks(c, pl)), "[vide:avc1 soun:mp4a]", "tracks of the init file")
		r := playHLS(c, httpClient, flvURL(srs, stream+".m3u8"), report.Until{VideoHeader: true, Duration: 5 * time.Second})
		checkVideo(c, r, "h264")
		checkAudio(c, r, "mp3", 0)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	check.Case(t, "PublishPlayVideoOnly", check.Normal, func(c *check.C) {
		r := play(c, media.H264Only, 2*fmp4Segment, httpClient, false, report.Until{VideoHeader: true, Duration: 5 * time.Second})
		checkVideo(c, r, "h264")
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
		c.Check(r.Video.Keyframes >= 5, "%v keyframes, want a 1s GOP over 5s", r.Video.Keyframes)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	// With no video, segments are cut at hls_aof_ratio times the fragment,
	// 5s plus at most a batch of audio.
	check.Case(t, "PublishPlayAudioOnly", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.AACOnly, 12*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(1))
		checkDurations(c, pl, 5*time.Second, 5*time.Second+pureAudioBatch)
		c.CheckEqual(pl.TargetDuration, 6, "target duration, the longest segment rounded up")

		r := playHLS(c, httpClient, flvURL(srs, stream+".m3u8"), report.Until{AudioHeader: true, Duration: 8 * time.Second})
		checkAudio(c, r, "aac", 1)
		c.Check(r.Video == nil, "video %v, want none", r.Video)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	check.Case(t, "HttpsPublishPlay", check.Normal, func(c *check.C) {
		cfg, err := server.ClientTLS()
		c.Must(err, "the client TLS config")
		tr := &http.Transport{TLSClientConfig: cfg}
		c.Cleanup(tr.CloseIdleConnections)
		r := play(c, media.H264HighAAC, 2*fmp4Segment, &http.Client{Transport: tr}, true, fmp4Until)
		checkPlayed(c, r, "h264", "aac")
	})

	// The master playlist of a new session, then the media playlist of an 8s
	// stream that is still published: version 7, the init file in
	// #EXT-X-MAP, and two 4s segments named by hls_fmp4_file in the session.
	check.Case(t, "PlaylistTags", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 2*fmp4Segment)
		pl, mediaURL := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(1))

		res, body := httpGet(c, httpClient, flvURL(srs, stream+".m3u8"))
		c.CheckEqual(res.StatusCode, 200, "status of the master playlist")
		c.CheckEqual(res.Header.Get("Content-Type"), "application/vnd.apple.mpegurl", "content type of the master playlist")
		base, _ := url.Parse(flvURL(srs, stream+".m3u8"))
		master, err := hls.Parse(body, base)
		c.Must(err, "parse the master playlist")
		c.Check(master.Master && len(master.Variants) == 1, "master playlist %+v, want one variant", master)

		res, _ = httpGet(c, httpClient, mediaURL)
		c.CheckEqual(res.Header.Get("Content-Type"), "application/vnd.apple.mpegurl", "content type of the media playlist")
		c.CheckEqual(pl.Version, 7, "version")
		c.CheckEqual(pl.TargetDuration, 4, "target duration, the longest segment")
		c.CheckEqual(pl.MediaSequence, 0, "media sequence")
		c.Check(!pl.EndList, "a live playlist has no #EXT-X-ENDLIST")
		c.CheckEqual(len(pl.Segments), 2, "closed segments")
		ctx := query(c, mediaURL).Get("hls_ctx")
		for i, s := range pl.Segments {
			u, _ := url.Parse(s.URI)
			c.CheckEqual(u.Path, fmt.Sprintf("/%v-seg-%v.m4s", stream, i), "segment path")
			c.CheckEqual(u.Query().Get("hls_ctx"), ctx, "segment hls_ctx")
			c.CheckEqual(s.Sequence, i, "segment sequence")
			if c.Check(s.Map != nil, "segment %v has no #EXT-X-MAP", i) {
				u, _ := url.Parse(s.Map.URI)
				c.CheckEqual(u.Path, "/"+stream+"-init.mp4", "init path")
			}
		}
		checkDurations(c, pl, fmp4Segment-100*time.Millisecond, fmp4Segment+100*time.Millisecond)
	})

	// The init file has an H.264 and an AAC track and no samples, and is
	// served as video/mp4.
	check.Case(t, "InitFile", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, fmp4Segment)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		if !c.Check(pl.Segments[0].Map != nil, "no #EXT-X-MAP") {
			return
		}
		res, _ := httpGet(c, httpClient, pl.Segments[0].Map.URI)
		c.CheckEqual(res.Header.Get("Content-Type"), "video/mp4", "content type of the init file")
		c.CheckEqual(fmt.Sprint(initTracks(c, pl)), "[vide:avc1 soun:mp4a]", "tracks of the init file")
	})

	// Each segment starts with a keyframe and holds 4 GOPs of 1s, and the
	// next starts where it ends.
	check.Case(t, "SegmentsOnGop", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 2*fmp4Segment)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(1))

		var reports []*report.Report
		for _, s := range pl.Segments {
			res, _ := httpGet(c, httpClient, s.URI)
			c.CheckEqual(res.Header.Get("Content-Type"), "video/iso.segment", "content type of "+s.URI)
			r := fmp4SegmentReport(c, httpClient, s)
			if !c.Check(r.Video != nil && r.Audio != nil, "segment %v: %v", s.Sequence, r) {
				return
			}
			c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
			c.CheckEqual(r.Video.Keyframes, 4, "keyframes")
			c.Check(near(r.Video.Duration(), fmp4Segment, 50*time.Millisecond), "video %v, want 4s", r.Video.Duration())
			reports = append(reports, r)
		}
		if len(reports) == 2 {
			step := reports[1].Video.FirstDTS - reports[0].Video.FirstDTS
			c.Check(near(step, fmp4Segment, time.Millisecond), "the second segment starts %v after the first, want 4s", step)
		}
	})

	// The playlist keeps the last segments that pass the window: 2 of 4s for
	// 7s, so it starts at 3 of the 5 segments of 20s; hls_cleanup removes
	// the others, and keeps the init file.
	check.Case(t, "WindowAndCleanup", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 20*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(4))
		c.CheckEqual(pl.MediaSequence, 3, "media sequence")
		c.CheckEqual(len(pl.Segments), 2, "segments in the window")

		dir := filepath.Join(srs.Dir, "hls")
		for i := range 5 {
			_, err := os.Stat(filepath.Join(dir, fmt.Sprintf("%v-seg-%v.m4s", stream, i)))
			if i < 3 {
				c.Check(errors.Is(err, os.ErrNotExist), "segment %v: %v, want it removed", i, err)
			} else {
				c.CheckNoError(err, fmt.Sprintf("segment %v", i))
			}
		}
		_, err := os.Stat(filepath.Join(dir, stream+"-init.mp4"))
		c.CheckNoError(err, "the init file")
		res, _ := httpGet(c, httpClient, flvURL(srs, stream+"-seg-0.m4s"))
		c.CheckEqual(res.StatusCode, 404, "status of an expired segment")
	})

	// hls_path, hls_fmp4_file and hls_init_file: the files are where the
	// templates put them, and not at the default names.
	check.Case(t, "PathTemplates", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, fmp4Segment)
		waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		dir := filepath.Join(srs.Dir, "hls")
		for _, f := range []string{stream + ".m3u8", stream + "-seg-0.m4s", stream + "-init.mp4"} {
			_, err := os.Stat(filepath.Join(dir, f))
			c.CheckNoError(err, "the file "+f+" under hls_path")
		}
		for _, f := range []string{stream + "-0.m4s", stream + "/init.mp4"} {
			_, err := os.Stat(filepath.Join(dir, f))
			c.Check(errors.Is(err, os.ErrNotExist), "the default file %v: %v, want none", f, err)
		}
	})

	// hls_entry_prefix is the base URL of each segment URI, as with TS.
	check.Case(t, "EntryPrefix", check.Normal, func(c *check.C) {
		check.KnownBug(c, "fmp4 playlists list each segment and the init file by its file name, without hls_entry_prefix")
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, fmp4Segment)
		pl, _ := waitPlaylist(c, cdnClient(c, srs), flvURL(srs, stream+".m3u8"), hasSegment(0))
		for _, s := range pl.Segments {
			u, _ := url.Parse(s.URI)
			c.CheckEqual(u.Host, cdnHost, "segment host")
			if s.Map != nil {
				u, _ := url.Parse(s.Map.URI)
				c.CheckEqual(u.Host, cdnHost, "init host")
			}
		}
	})

	// A republish goes on with the next sequence number, and its segments
	// play.
	check.Case(t, "RepublishSequence", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		pl := republishFmp4(c, srs, stream)
		var seqs []int
		for _, s := range pl.Segments {
			seqs = append(seqs, s.Sequence)
		}
		c.CheckEqual(fmt.Sprint(seqs), "[0 1 2]", "segment sequences")
		if len(pl.Segments) == 3 {
			c.Check(near(pl.Segments[1].Duration, time.Second, 100*time.Millisecond), "segment 1 lasts %v, want the 1s left at the unpublish", pl.Segments[1].Duration)
			r := fmp4SegmentReport(c, httpClient, pl.Segments[2])
			checkPlayed(c, r, "h264", "aac")
		}
	})

	// The republished stream starts again at timestamp 0, so the segment
	// after the republish needs a discontinuity, as with TS.
	check.Case(t, "RepublishDiscontinuity", check.Normal, func(c *check.C) {
		check.KnownBug(c, "fmp4 playlists have no #EXT-X-DISCONTINUITY, also not after a republish")
		stream := "live/" + streamName(c)
		pl := republishFmp4(c, srs, stream)
		if c.Check(len(pl.Segments) == 3, "segments %+v, want 3", pl.Segments) {
			c.Check(pl.Segments[2].Discontinuity, "the segment after the republish has no discontinuity")
		}
	})

	check.Case(t, "NeverPublished", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		for _, p := range []string{stream + ".m3u8", stream + "-seg-0.m4s", stream + "-init.mp4"} {
			res, _ := httpGet(c, httpClient, flvURL(srs, p))
			c.CheckEqual(res.StatusCode, 404, p)
		}
	})

	check.Case(t, "MissingSegment", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, fmp4Segment)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		res, _ := httpGet(c, httpClient, pl.Segments[0].URI)
		c.CheckEqual(res.StatusCode, 200, "status of a listed segment")
		res, _ = httpGet(c, httpClient, flvURL(srs, stream+"-seg-99.m4s"))
		c.CheckEqual(res.StatusCode, 404, "status of a segment that is not there")
	})
}

// sharedRemuxOff is the config of the server that TestHttpFlv_RemuxOff,
// TestHls_Fmp4Default, TestDash_Options, TestSrt_Latencies and
// TestRtsp_Options share: RTMP, the HTTP API and server, Common's latency,
// the HTTP remux off, HLS in fmp4 at SRS's defaults, DASH with the settings
// of TestDash_Options under ./objs/nginx/html/dash, SRT with its own
// receive and peer latencies, and RTSP with rtmp_to_rtsp off. HLS and DASH
// write different files, and the SRT and RTSP cases only connect, so none of
// the groups depends on what the others set.
func sharedRemuxOff() server.Config {
	cfg := server.HLSConfig(server.HLS{FMP4: true})
	cfg.Env["SRS_VHOST_HTTP_REMUX_ENABLED"] = "off"
	cfg = server.WithSRT(server.WithDASH(cfg, server.DASH{
		Fragment:   2 * time.Second,
		WindowSize: 3,
		Path:       "./objs/nginx/html/" + dashDir,
		MPDFile:    "[app]/[stream]-manifest.mpd",
	}), sharedRemuxOffSRT)
	return server.WithRTSP(cfg, server.RTSPOptions{NoRTMPToRTSP: true})
}

// TestHls_Fmp4Default plays HLS in fmp4 with every file at SRS's default
// names: the playlist [app]/[stream].m3u8, the segments
// [app]/[stream]-[seq].m4s and the init file [app]/[stream]/init.mp4, and the
// 10s fragment, which makes segments of 11s on the 1s GOP (see fmp4Segment).
func TestHls_Fmp4Default(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	// The files are at the default names, and the playlist lists the segment
	// and the init file.
	check.Case(t, "DefaultFiles", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 11*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		dir := filepath.Join(srs.Dir, "objs", "nginx", "html")
		for _, f := range []string{stream + ".m3u8", stream + "-0.m4s", stream + "/init.mp4"} {
			_, err := os.Stat(filepath.Join(dir, f))
			c.CheckNoError(err, "the file "+f)
		}
		if c.Check(len(pl.Segments) == 1 && pl.Segments[0].Map != nil, "segments %+v, want one with #EXT-X-MAP", pl.Segments) {
			u, _ := url.Parse(pl.Segments[0].URI)
			c.CheckEqual(u.Path, "/"+stream+"-0.m4s", "segment path")
			checkDurations(c, pl, 11*time.Second-100*time.Millisecond, 11*time.Second+100*time.Millisecond)
		}
		res, _ := httpGet(c, httpClient, flvURL(srs, stream+"/init.mp4"))
		c.CheckEqual(res.StatusCode, 200, "status of the init file at its path")
	})

	// A player follows #EXT-X-MAP to the init file and plays the segment.
	check.Case(t, "DefaultInitPlays", check.Normal, func(c *check.C) {
		check.KnownBug(c, "the playlist lists the default init file [app]/[stream]/init.mp4 by its file name, init.mp4, which is not beside the playlist")
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 11*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		if !c.Check(pl.Segments[0].Map != nil, "no #EXT-X-MAP") {
			return
		}
		u, _ := url.Parse(pl.Segments[0].Map.URI)
		c.CheckEqual(u.Path, "/"+stream+"/init.mp4", "init path")
		res, _ := httpGet(c, httpClient, pl.Segments[0].Map.URI)
		c.CheckEqual(res.StatusCode, 200, "status of the listed init file")
	})
}

// fmp4Segment is how long the segments of TestHls_Fmp4 last: SRS cuts an
// fmp4 segment at the first keyframe where the segment already lasts its 3s
// target, measured to its last frame, not to the keyframe. On the 1s GOP the
// keyframe at 3s finds it at 2.96s, so it cuts at the next, 4s; a TS segment
// with the same settings is cut at 3s.
const fmp4Segment = 4 * time.Second

// republishFmp4 publishes 5s of a stream, unpublishes it, which closes a 1s
// segment, and publishes 4s again, and returns the playlist with the
// segment of the second publish.
func republishFmp4(c *check.C, srs *server.Server, stream string) *hls.Playlist {
	c.Helper()
	pub := publishThrough(c, srs, stream, media.H264HighAAC, 5*time.Second)
	_, mediaURL := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
	pub.Close()
	waitPlaylist(c, httpClient, mediaURL, hasSegment(1))
	pub = republish(c, rtmpURL(srs, stream), 300*time.Millisecond)
	publishTagsOn(c, srs, pub, readTags(c, media.H264HighAAC, 0, fmp4Segment+time.Millisecond))
	pl, _ := waitPlaylist(c, httpClient, mediaURL, hasSegment(2))
	return pl
}

// initTracks fetches the init file of a playlist's first segment, which
// must be there, checks its tracks have no samples, and returns them as
// "<handler>:<sample entry>".
func initTracks(c *check.C, pl *hls.Playlist) []string {
	c.Helper()
	if !c.Check(len(pl.Segments) > 0 && pl.Segments[0].Map != nil, "no #EXT-X-MAP: %+v", pl) {
		c.FailNow()
	}
	uri := pl.Segments[0].Map.URI
	res, body := httpGet(c, httpClient, uri)
	c.CheckEqual(res.StatusCode, 200, "status of "+uri)
	init, err := mp4.ParseInit(body)
	c.Must(err, "parse "+uri)
	var tracks []string
	for _, t := range init.Tracks {
		tracks = append(tracks, t.Handler+":"+t.Codec)
		c.CheckEqual(len(t.Samples), 0, "samples of the "+t.Handler+" track")
	}
	return tracks
}

// fmp4SegmentReport fetches an fmp4 segment and its init file, which must
// be there, and analyzes the segment whole.
func fmp4SegmentReport(c *check.C, client *http.Client, s hls.Segment) *report.Report {
	c.Helper()
	if !c.Check(s.Map != nil, "segment %v has no #EXT-X-MAP", s.Sequence) {
		c.FailNow()
	}
	res, body := httpGet(c, client, s.Map.URI)
	c.CheckEqual(res.StatusCode, 200, "status of "+s.Map.URI)
	init, err := mp4.ParseInit(body)
	c.Must(err, "parse "+s.Map.URI)
	res, body = httpGet(c, client, s.URI)
	c.CheckEqual(res.StatusCode, 200, "status of "+s.URI)
	samples, err := mp4.ParseFragments(init, body)
	c.Must(err, "parse "+s.URI)
	var a mp4.Analyzer
	c.Must(a.Add(samples, report.Until{}), "analyze "+s.URI)
	return &a.Report
}
