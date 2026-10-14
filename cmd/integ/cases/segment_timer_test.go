// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/dash"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

const (
	// disposeTick is how often SRS checks its sources for hls_dispose and
	// dash_dispose: a stream's files go at the first check after it has
	// sent no media for the dispose time, so up to this much later.
	disposeTick = 3 * time.Second
	// sessionTick is how often SRS checks the hls_ctx sessions: a session
	// ends at the first check after it has had no request for twice
	// hls_window.
	sessionTick = 5 * time.Second
	// segmentDispose is the hls_dispose and dash_dispose of the dispose
	// server. SRS reads a fraction of a second, but checks only every
	// disposeTick, so a smaller value gains little.
	segmentDispose = 500 * time.Millisecond
	// sessionWindow is the hls_window of the keep server, so its sessions
	// end after 1s without a request.
	sessionWindow = 500 * time.Millisecond
)

// sharedSegmentsDispose is the config of the timer server that
// TestTimer_Rtmp_QueueDefault, TestTimer_Hls_Dispose and
// TestTimer_Dash_Dispose share: the default play queue, and HLS and DASH
// with 1s fragments and hls_dispose and dash_dispose at 0.5s. The queue case
// only plays over RTMP, and the files of its stream are its own.
func sharedSegmentsDispose() server.Config {
	return server.WithDASH(
		server.WithHLS(server.PlayConfig(server.Play{}), server.HLS{Fragment: time.Second, Dispose: segmentDispose}),
		server.DASH{Fragment: time.Second, Dispose: segmentDispose})
}

// sharedSegmentsKeep is the config of the timer server that
// TestTimer_Rtmp_QueueLength, TestTimer_Hls_Cleanup, TestTimer_Dash_Cleanup
// and TestTimer_Hls_Sessions share: a 1s play queue, HLS with 1s fragments,
// a 0.5s window and hls_cleanup off, DASH with 1s fragments and dash_cleanup
// off, and every hook on a mock. The dispose times stay at SRS's default,
// 120s, so a held stream keeps its files for the sessions.
// TestTimer_Srt_PeerIdle shares it too, for SRT with a 1s peer_idle_timeout,
// TestTimer_Gb_MediaTimeout, for GB28181 with a 1s media_connect_timeout,
// and TestTimer_Convert_Reports, for WebRTC; the other groups use no SRT, no
// GB and no WebRTC. TestTimer_Reload_RawApi shares it for the RAW API with
// reload allowed, and TestTimer_Reload_Sighup for SIGHUP: a reload of an
// env-only server fails and changes nothing. TestTimer_Signal_Console sends
// it SIGUSR1, which reopens no log on the console.
// TestTimer_Heartbeat_Full shares it for the heartbeat with every setting on,
// every 1s, which the other groups never read.
func sharedSegmentsKeep() server.Config {
	cfg := server.WithDASH(
		server.WithHLS(server.PlayConfig(server.Play{QueueLength: time.Second}),
			server.HLS{Fragment: time.Second, Window: sessionWindow, NoCleanup: true}),
		server.DASH{Fragment: time.Second, NoCleanup: true})
	cfg.Hooks = true
	cfg.Heartbeat = &sharedHeartbeatFull
	cfg = server.WithGB(server.WithSRT(cfg, sharedSegmentsKeepSRT), sharedSegmentsKeepGB)
	return server.WithRawAPI(server.WithRTC(cfg, server.RTC{}), server.RawAPI{AllowReload: true})
}

// TestTimer_Hls_Dispose checks hls_dispose: SRS removes the playlist and
// segments of a stream that has sent no media for 0.5s, published or not.
func TestTimer_Hls_Dispose(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsDispose", sharedSegmentsDispose())

	// publish publishes 3s at once, which closes 3 segments, and waits for
	// the playlist. It returns the publisher, the stream, the session's
	// playlist URL, and when the last media was sent.
	publish := func(c *check.C) (*rtmp.Client, string, string, time.Time) {
		stream := "live/" + streamName(c)
		pub := publishThrough(c, srs, stream, media.H264HighAAC, 3*time.Second)
		sent := time.Now()
		_, playlist := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(2))
		return pub, stream, playlist, sent
	}

	// After the unpublish, the playlist and every segment are removed, at
	// least 0.5s after the last media and by the next check after it, and the
	// HTTP server answers 404 for them.
	check.Case(t, "FilesRemoved", check.Timer, func(c *check.C) {
		pub, stream, playlist, sent := publish(c)
		files := hlsFiles(srs, stream)
		c.Check(len(files) >= 4, "files %v, want the playlist and 3 segments", files)
		pub.Close()
		gone := untilNoFiles(c, func() []string { return hlsFiles(srs, stream) })
		checkDisposed(c, gone.Sub(sent))
		res, _ := httpGet(c, httpClient, playlist)
		c.CheckEqual(res.StatusCode, 404, "status of the session's playlist")
		res, _ = httpGet(c, httpClient, flvURL(srs, stream+"-0.ts"))
		c.CheckEqual(res.StatusCode, 404, "status of segment 0")
	})

	// A publisher that stays connected but sends nothing is disposed too:
	// the files go while the stream is still published.
	check.Case(t, "IdlePublisherDisposed", check.Timer, func(c *check.C) {
		_, stream, _, sent := publish(c)
		gone := untilNoFiles(c, func() []string { return hlsFiles(srs, stream) })
		checkDisposed(c, gone.Sub(sent))
		c.Check(streamPublishing(c, srs, stream), "%v is not published after the dispose", stream)
	})

	// A publisher that sends media again after its stream was disposed gets
	// HLS again: the next segments are written.
	check.Case(t, "IdlePublisherResumes", check.Timer, func(c *check.C) {
		pub, stream, _, _ := publish(c)
		untilNoFiles(c, func() []string { return hlsFiles(srs, stream) })
		publishTagsOn(c, srs, pub, readTags(c, media.H264HighAAC, 3*time.Second+time.Millisecond, 6*time.Second+time.Millisecond))
		c.CheckNoError(untilFile(filepath.Join(htmlDir(srs), stream+".m3u8"), time.Second), "the playlist after the media resumed")
	})

	// A stream that keeps sending media is never disposed: in real time for
	// longer than the dispose time and a check after it, so SRS checks it at
	// least once past the dispose time, its playlist stays on disk and gets
	// new segments.
	check.Case(t, "StreamingNotDisposed", check.Timer, func(c *check.C) {
		stream := "live/" + streamName(c)
		end := time.Now().Add(segmentDispose + disposeTick + 500*time.Millisecond)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 0)
		m3u8 := filepath.Join(htmlDir(srs), stream+".m3u8")
		c.Must(untilFile(m3u8, 2*time.Second), "the playlist")
		for time.Now().Before(end) {
			_, err := os.Stat(m3u8)
			if !c.CheckNoError(err, "the playlist while streaming") {
				return
			}
			time.Sleep(50 * time.Millisecond)
		}
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), func(*hls.Playlist) bool { return true })
		c.Check(len(pl.Segments) > 0 && pl.Segments[len(pl.Segments)-1].Sequence >= 2, "playlist %+v, want segments past 2s of streaming", pl)
	})

	// A republish after the dispose writes a new playlist, which plays.
	check.Case(t, "RepublishAfterDispose", check.Timer, func(c *check.C) {
		pub, stream, _, _ := publish(c)
		pub.Close()
		untilNoFiles(c, func() []string { return hlsFiles(srs, stream) })
		publishThrough(c, srs, stream, media.H264HighAAC, 2*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), func(pl *hls.Playlist) bool { return len(pl.Segments) > 0 })
		r := segmentReport(c, httpClient, pl.Segments[0].URI)
		checkVideo(c, r, "h264")
		checkAudio(c, r, "aac", 1)
	})
}

// TestTimer_Dash_Dispose checks dash_dispose: SRS removes the MPD and the
// segments of a stream that has sent no media for 0.5s, and dash_cleanup at
// its default: segments that leave the window are removed.
func TestTimer_Dash_Dispose(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsDispose", sharedSegmentsDispose())

	// publish publishes 5s at once, not up to a keyframe, which closes 2
	// segments of 2s, and waits for the MPD. It returns the publisher, the
	// stream, the MPD's streams, and when the last media was sent.
	publish := func(c *check.C) (*rtmp.Client, string, []dash.Stream, time.Time) {
		stream := "live/" + streamName(c)
		pub := publishThrough(c, srs, stream, media.H264HighAAC, 4960*time.Millisecond)
		sent := time.Now()
		_, streams := waitMPD(c, flvURL(srs, stream+".mpd"), hasDashSegment(1))
		return pub, stream, streams, sent
	}

	// After the unpublish, the MPD and every segment are removed, like HLS,
	// and the HTTP server answers 404 for them.
	check.Case(t, "FilesRemoved", check.Timer, func(c *check.C) {
		pub, stream, streams, sent := publish(c)
		files := dashFiles(srs, stream)
		c.Check(len(files) >= 5, "files %v, want the MPD and 4 segments", files)
		pub.Close()
		gone := untilNoFiles(c, func() []string { return dashFiles(srs, stream) })
		checkDisposed(c, gone.Sub(sent))
		res, _ := httpGet(c, httpClient, flvURL(srs, stream+".mpd"))
		c.CheckEqual(res.StatusCode, 404, "status of the MPD")
		for _, s := range streams {
			res, _ = httpGet(c, httpClient, s.Segments[0].URL)
			c.CheckEqual(res.StatusCode, 404, "status of "+s.Segments[0].URL)
		}
	})

	// A publisher that stays connected but sends nothing is disposed too.
	check.Case(t, "IdlePublisherDisposed", check.Timer, func(c *check.C) {
		_, stream, _, sent := publish(c)
		gone := untilNoFiles(c, func() []string { return dashFiles(srs, stream) })
		checkDisposed(c, gone.Sub(sent))
		c.Check(streamPublishing(c, srs, stream), "%v is not published after the dispose", stream)
	})

	// A publisher that sends media again after its stream was disposed gets
	// DASH again: a new MPD whose init files and segments are served.
	check.Case(t, "IdlePublisherResumes", check.Timer, func(c *check.C) {
		pub, stream, _, _ := publish(c)
		untilNoFiles(c, func() []string { return dashFiles(srs, stream) })
		publishTagsOn(c, srs, pub, readTags(c, media.H264HighAAC, 4960*time.Millisecond+time.Millisecond, 10*time.Second+time.Millisecond))
		_, streams := waitMPD(c, flvURL(srs, stream+".mpd"), func(streams []dash.Stream) bool {
			return len(streams) == 2 && len(streams[0].Segments) > 0 && len(streams[1].Segments) > 0
		})
		for _, s := range streams {
			res, _ := httpGet(c, httpClient, s.Init)
			c.CheckEqual(res.StatusCode, 200, "status of "+s.Init)
			last := s.Segments[len(s.Segments)-1].URL
			res, _ = httpGet(c, httpClient, last)
			c.CheckEqual(res.StatusCode, 200, "status of "+last)
		}
	})

	// The dispose removes the init files too, "all DASH files" as full.conf
	// says.
	check.Case(t, "InitFilesRemoved", check.Timer, func(c *check.C) {
		pub, stream, streams, _ := publish(c)
		pub.Close()
		untilNoFiles(c, func() []string { return dashFiles(srs, stream) })
		for _, s := range streams {
			f := filepath.Join(htmlDir(srs), urlPath(c, s.Init))
			_, err := os.Stat(f)
			c.Check(errors.Is(err, os.ErrNotExist), "%v: %v, want it removed", s.Init, err)
		}
	})

	// A publish that ends on a keyframe that cuts a segment leaves no file:
	// the keyframe at 6s closes the segment from 4s and opens one of that
	// frame alone, which the MPD never lists.
	check.Case(t, "KeyframeSegmentRemoved", check.Timer, func(c *check.C) {
		stream := "live/" + streamName(c)
		pub := publishThrough(c, srs, stream, media.H264HighAAC, 6*time.Second)
		waitMPD(c, flvURL(srs, stream+".mpd"), func(streams []dash.Stream) bool {
			for _, s := range streams {
				if s.Kind == "video" && len(s.Segments) == 3 {
					return true
				}
			}
			return false
		})
		pub.Close()
		untilNoFiles(c, func() []string { return dashFiles(srs, stream) })
	})

	// dash_cleanup on, the default: 17s of 2s segments keep 10s, twice the
	// 5 segments of the window times the fragment, so segment 0 of each
	// representation is removed and answers 404, and the newest is served.
	check.Case(t, "CleanupRemovesExpired", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 16960*time.Millisecond)
		_, streams := waitMPD(c, flvURL(srs, stream+".mpd"), hasDashSegment(7))
		for _, s := range streams {
			seg0 := flvURL(srs, fmt.Sprintf("%v/%v-0.m4s", stream, s.Kind))
			_, err := os.Stat(filepath.Join(htmlDir(srs), urlPath(c, seg0)))
			c.Check(errors.Is(err, os.ErrNotExist), "%v segment 0: %v, want it removed", s.Kind, err)
			res, _ := httpGet(c, httpClient, seg0)
			c.CheckEqual(res.StatusCode, 404, "status of "+seg0)
			last := s.Segments[len(s.Segments)-1].URL
			res, _ = httpGet(c, httpClient, last)
			c.CheckEqual(res.StatusCode, 200, "status of "+last)
		}
	})
}

// TestTimer_Hls_Cleanup checks hls_cleanup off: segments that leave the
// window stay on disk and are served.
func TestTimer_Hls_Cleanup(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// With a 0.5s window the playlist lists only the newest of 4 segments,
	// and the 3 before it stay on disk and answer 200.
	check.Case(t, "ExpiredKept", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 4*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(3))
		c.Check(!hasSegment(0)(pl), "playlist %+v lists segment 0, want it out of the window", pl)
		for i := range 3 {
			f := fmt.Sprintf("%v-%v.ts", stream, i)
			_, err := os.Stat(filepath.Join(htmlDir(srs), f))
			c.CheckNoError(err, "the expired "+f)
			res, _ := httpGet(c, httpClient, flvURL(srs, f))
			c.CheckEqual(res.StatusCode, 200, "status of the expired "+f)
		}
	})
}

// TestTimer_Dash_Cleanup checks dash_cleanup off: segments that leave the
// window stay on disk and are served.
func TestTimer_Dash_Cleanup(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// 17s of 2s segments: the MPD lists the last 5, and segment 0 of each
	// representation, past the 10s that cleanup would keep, stays on disk
	// and answers 200.
	check.Case(t, "ExpiredKept", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 16960*time.Millisecond)
		_, streams := waitMPD(c, flvURL(srs, stream+".mpd"), hasDashSegment(7))
		for _, s := range streams {
			c.Check(segmentNumbers(s)[0] > 0, "%v segments %v, want segment 0 out of the window", s.Kind, segmentNumbers(s))
			seg0 := flvURL(srs, fmt.Sprintf("%v/%v-0.m4s", stream, s.Kind))
			_, err := os.Stat(filepath.Join(htmlDir(srs), urlPath(c, seg0)))
			c.CheckNoError(err, "the expired "+s.Kind+" segment 0")
			res, _ := httpGet(c, httpClient, seg0)
			c.CheckEqual(res.StatusCode, 200, "status of "+seg0)
		}
	})
}

// TestTimer_Hls_Sessions checks the end of an hls_ctx session: with
// hls_window 0.5s, a session without a request for 1s ends at SRS's next
// check, with on_stop, and leaves the API.
func TestTimer_Hls_Sessions(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// publish publishes 2s, which closes 2 segments, and waits for the
	// playlist. It returns the stream's name and its playlist's URL.
	publish := func(c *check.C) (string, string) {
		name := streamName(c)
		publishThrough(c, srs, "live/"+name, media.H264HighAAC, 2*time.Second)
		u := flvURL(srs, "live/"+name+".m3u8")
		waitPlaylist(c, httpClient, u, hasSegment(1))
		return name, u
	}

	// session starts a session and fetches its media playlist; it returns
	// the session's hls_ctx and media playlist URL.
	session := func(c *check.C, u string) (string, string) {
		master := sessionMaster(c, u)
		uri := master.Variants[0].URI
		fetchPlaylist(c, uri)
		return query(c, uri).Get("hls_ctx"), uri
	}

	// A session with no more requests ends at least twice hls_window after
	// its last one and by the next check after it: its client leaves the
	// API and on_stop is called with it.
	check.Case(t, "IdleSessionExpires", check.Timer, func(c *check.C) {
		name, u := publish(c)
		ctx, _ := session(c, u)
		last := time.Now()
		gone := untilClientGone(c, srs, ctx)
		elapsed := gone.Sub(last)
		c.Check(elapsed >= 2*sessionWindow-50*time.Millisecond, "the session ended %v after its last request, want at least %v", elapsed, 2*sessionWindow)
		c.Check(elapsed <= 2*sessionWindow+sessionTick+500*time.Millisecond, "the session ended %v after its last request, want by %v", elapsed, 2*sessionWindow+sessionTick)
		stop := waitSessionHook(c, srs, "on_stop", ctx)
		c.CheckEqual(stop.Stream(), name, "on_stop stream")
	})

	// A session that keeps requesting its playlist lives on, while one
	// started with it ends.
	check.Case(t, "ActiveSessionKept", check.Timer, func(c *check.C) {
		_, u := publish(c)
		active, uri := session(c, u)
		idle, _ := session(c, u)
		deadline := time.Now().Add(2*sessionWindow + sessionTick + time.Second)
		for apiCode(c, srs, "/api/v1/clients/"+idle) == 0 {
			if time.Now().After(deadline) {
				c.Fatalf("the idle session %v still a client after %v", idle, 2*sessionWindow+sessionTick+time.Second)
			}
			fetchPlaylist(c, uri)
			time.Sleep(200 * time.Millisecond)
		}
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+active), 0, "code of the active session's client")
		stops := srs.Hooks.Requests(mock.Match{Action: "on_stop", Func: func(r mock.Request) bool { return r.Field("client_id") == active }})
		c.CheckEqual(len(stops), 0, "on_stop of the active session")
	})

	// A request with the hls_ctx of an ended session starts a new session
	// by it: the master playlist again, on_play again, and a client again.
	check.Case(t, "EndedCtxStartsNewSession", check.Timer, func(c *check.C) {
		_, u := publish(c)
		ctx, _ := session(c, u)
		untilClientGone(c, srs, ctx)
		master := sessionMaster(c, u+"?hls_ctx="+ctx)
		c.CheckEqual(query(c, master.Variants[0].URI).Get("hls_ctx"), ctx, "hls_ctx of the new session")
		plays := srs.Hooks.Requests(mock.Match{Action: "on_play", Func: func(r mock.Request) bool { return r.Field("client_id") == ctx }})
		c.CheckEqual(len(plays), 2, "on_play of the hls_ctx")
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+ctx), 0, "code of the new session's client")
	})
}

// htmlDir is SRS's default hls_path and dash_path, the HTTP server's dir.
func htmlDir(srs *server.Server) string {
	return filepath.Join(srs.Dir, "objs", "nginx", "html")
}

// hlsFiles lists the HLS files of a stream at the default paths: its
// playlist and its TS segments, also one SRS is still writing.
func hlsFiles(srs *server.Server, stream string) []string {
	return globFiles(htmlDir(srs), stream+".m3u8", stream+"-*.ts", stream+"-*.ts.tmp")
}

// dashFiles lists the DASH files of a stream at the default paths but the
// init files: its MPD and its segments, also those SRS is still writing.
func dashFiles(srs *server.Server, stream string) []string {
	return globFiles(htmlDir(srs), stream+".mpd", stream+"/*-[0-9]*.m4s", stream+"/*-[0-9]*.m4s.tmp")
}

// globFiles lists the files under dir that match the patterns, sorted.
func globFiles(dir string, patterns ...string) []string {
	var out []string
	for _, p := range patterns {
		m, _ := filepath.Glob(filepath.Join(dir, p))
		for _, f := range m {
			rel, _ := filepath.Rel(dir, f)
			out = append(out, rel)
		}
	}
	sort.Strings(out)
	return out
}

// untilNoFiles polls the files every 20ms until none is left, for at most
// the dispose time, a check and 1s, and returns when they were gone.
func untilNoFiles(c *check.C, files func() []string) time.Time {
	c.Helper()
	wait := segmentDispose + disposeTick + time.Second
	deadline := time.Now().Add(wait)
	for {
		left := files()
		if len(left) == 0 {
			return time.Now()
		}
		if time.Now().After(deadline) {
			c.Fatalf("files %v still there after %v", left, wait)
		}
		time.Sleep(20 * time.Millisecond)
	}
}

// checkDisposed checks the files of a stream went at least the dispose time
// after its last media, and by the next check after it.
func checkDisposed(c *check.C, elapsed time.Duration) {
	c.Helper()
	c.Check(elapsed >= segmentDispose, "the files went %v after the last media, want at least %v", elapsed, segmentDispose)
	c.Check(elapsed <= segmentDispose+disposeTick+500*time.Millisecond, "the files went %v after the last media, want by %v", elapsed, segmentDispose+disposeTick)
}

// untilFile polls every 20ms until a file exists, for at most wait.
func untilFile(f string, wait time.Duration) error {
	deadline := time.Now().Add(wait)
	for {
		_, err := os.Stat(f)
		if err == nil || time.Now().After(deadline) {
			return err
		}
		time.Sleep(20 * time.Millisecond)
	}
}

// untilClientGone polls the API every 50ms until a client is gone, for at
// most twice the session window, a check and 1s, and returns when it was.
func untilClientGone(c *check.C, srs *server.Server, id string) time.Time {
	c.Helper()
	wait := 2*sessionWindow + sessionTick + time.Second
	deadline := time.Now().Add(wait)
	for apiCode(c, srs, "/api/v1/clients/"+id) == 0 {
		if time.Now().After(deadline) {
			c.Fatalf("client %v still there after %v", id, wait)
		}
		time.Sleep(50 * time.Millisecond)
	}
	return time.Now()
}

// waitSessionHook waits up to 1s for a hook of a session, by its hls_ctx.
func waitSessionHook(c *check.C, srs *server.Server, action, ctx string) mock.Request {
	c.Helper()
	wctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	r, err := srs.Hooks.Wait(wctx, mock.Match{Action: action, Func: func(r mock.Request) bool { return r.Field("client_id") == ctx }})
	c.Must(err, action+" of the session "+ctx)
	return r
}

// streamPublishing reports whether the API lists a stream, such as
// live/name, with an active publisher.
func streamPublishing(c *check.C, srs *server.Server, stream string) bool {
	c.Helper()
	var v struct {
		Streams []struct {
			URL     string `json:"url"`
			Publish struct {
				Active bool `json:"active"`
			} `json:"publish"`
		} `json:"streams"`
	}
	apiGet(c, srs, "/api/v1/streams/?count=1000", &v)
	for _, s := range v.Streams {
		if s.URL == "/"+stream {
			return s.Publish.Active
		}
	}
	return false
}
