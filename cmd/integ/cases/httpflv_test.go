// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	pathpkg "path"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/httpplay"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// TestHttpFlv_Common publishes over RTMP and plays HTTP-FLV on the Common
// server, whose mount is [vhost]/[app]/[stream].flv, with SRS's defaults for
// the FLV header: guessed from the first messages a player gets.
func TestHttpFlv_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlayHTTPFLV(c, media.H264HighAAC, "h264", "aac")
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlayHTTPFLV(c, media.HEVCAAC, "hevc", "aac")
	})

	httpFlvSignaling(t)
	httpFlvMedia(t)
	httpFlvLifecycle(t)
	httpFlvBadInput(t)
	httpsFlvCases(t)
}

// httpFlvSignaling adds the cases of the request, the mount and the answer.
func httpFlvSignaling(t *testing.T) {
	// ResponseHeaders checks the answer: 200, the FLV content type, and an
	// FLV header with both tracks.
	check.Case(t, "ResponseHeaders", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		res := httpResponse(c, p)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), "video/x-flv", "content type")
		checkFlvHeader(c, p, true, true)
	})

	// MultiLevelApp plays live/a/b/<stream>: the [app] of the mount spans
	// the folders, as the app of the publisher does.
	check.Case(t, "MultiLevelApp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		stream := "live/a/b/" + name
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)

		r := httpFlvPlay(c, srs, stream+".flv", nil, playedUntil)
		checkPlayed(c, r, "h264", "aac")
		h := waitHook(c, srs, "on_play", name)
		c.CheckEqual(h.Field("app"), "live/a/b", "on_play app")
	})

	// ParamInUrl plays with a query: the stream plays, and on_play has the
	// query as its param.
	check.Case(t, "ParamInUrl", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		stream := "live/" + name
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)

		r := httpFlvPlay(c, srs, stream+".flv?token=abc", nil, playedUntil)
		checkPlayed(c, r, "h264", "aac")
		h := waitHook(c, srs, "on_play", name)
		c.CheckEqual(h.Field("param"), "?token=abc", "on_play param")
		c.CheckEqual(h.Field("stream"), name, "on_play stream")
	})

	// VhostFromHost plays with a Host header naming a vhost SRS doesn't
	// have: the request goes to the default vhost, where the stream is.
	check.Case(t, "VhostFromHost", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		stream := "live/" + name
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)

		r := httpFlvPlay(c, srs, stream+".flv", &flv.HTTPOptions{Host: "example.com"}, playedUntil)
		checkPlayed(c, r, "h264", "aac")
		h := waitHook(c, srs, "on_play", name)
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "on_play vhost")
	})

	// NoAppNotFound asks for /<stream>.flv, with no app: the mount needs one,
	// so it is 404, though live/<stream> is published.
	check.Case(t, "NoAppNotFound", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		publishTags(c, rtmpURL(srs, "live/"+name), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))
		checkHTTPStatus(c, srs, name+".flv", nil, 404)
	})

	// WrongExtensionNotFound asks for a published stream as .mp3, which the
	// FLV mount doesn't serve.
	check.Case(t, "WrongExtensionNotFound", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishTags(c, rtmpURL(srs, stream), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))
		checkHTTPStatus(c, srs, stream+".mp3", nil, 404)
	})

	// OnPlayRejected has the on_play hook reject the player: the answer is
	// 401, with no stream.
	check.Case(t, "OnPlayRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		c.Must(checkHooks(srs), "hooks")
		name := streamName(c)
		stream := "live/" + name
		srs.Hooks.Script(mock.Match{Action: "on_play", Stream: name}, mock.Reject(403))
		publishTags(c, rtmpURL(srs, stream), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))
		checkHTTPStatus(c, srs, stream+".flv", nil, 401)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_play", Stream: name})), 1, "on_play requests")
	})

	// XForwardedFor plays through a proxy that sets X-Forwarded-For: the
	// first address is the client's IP, in on_play and in the API.
	check.Case(t, "XForwardedFor", check.Normal, func(c *check.C) {
		forwardedIP(c, http.Header{"X-Forwarded-For": {"10.1.2.3, 10.0.0.1"}}, "10.1.2.3")
	})

	// XRealIP plays through a proxy that sets X-Real-IP with a port: the
	// address without it is the client's IP.
	check.Case(t, "XRealIP", check.Normal, func(c *check.C) {
		forwardedIP(c, http.Header{"X-Real-IP": {"10.4.5.6:1234"}}, "10.4.5.6")
	})

	// CorsHeaders plays with an Origin header: crossdomain is on by default,
	// so the answer allows any origin.
	check.Case(t, "CorsHeaders", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishTags(c, rtmpURL(srs, stream), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))

		res := httpResponse(c, httpFlvPlayer(c, srs, stream+".flv", &flv.HTTPOptions{Header: originHeader}))
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "*", "Access-Control-Allow-Origin")
	})

	// CorsOptions sends the OPTIONS preflight of a browser: 200, allowing any
	// origin.
	check.Case(t, "CorsOptions", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		opts := &flv.HTTPOptions{Method: http.MethodOptions, Header: originHeader}
		res := checkHTTPStatus(c, srs, "live/"+streamName(c)+".flv", opts, 200)
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "*", "Access-Control-Allow-Origin")
	})
}

// httpFlvMedia adds the codec cases and the FLV header SRS guesses for each.
func httpFlvMedia(t *testing.T) {
	// PublishPlayAudioOnly plays an audio-only stream: the FLV header has
	// audio only.
	check.Case(t, "PublishPlayAudioOnly", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.AACOnly), 200*time.Millisecond)

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkFlvHeader(c, p, true, false)
		r := readHTTPFlv(c, p, report.Until{AudioHeader: true, AudioFrames: 5})
		checkAudio(c, r, "aac", 1)
		c.Check(r.Video == nil, "video %v, want none", r.Video)
	})

	// PublishPlayVideoOnly plays a video-only stream: the FLV header has
	// video only.
	check.Case(t, "PublishPlayVideoOnly", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264Only), 200*time.Millisecond)

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkFlvHeader(c, p, false, true)
		r := readHTTPFlv(c, p, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5})
		checkVideo(c, r, "h264")
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
	})

	// PublishPlayMp3 plays H.264 with MP3, which has no sequence header.
	check.Case(t, "PublishPlayMp3", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighMP3), 200*time.Millisecond)

		u := report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5, AudioFrames: 5}
		r := httpFlvPlay(c, srs, stream+".flv", nil, u)
		checkVideo(c, r, "h264")
		checkAudio(c, r, "mp3", 0)
	})

	// GuessFromFirstMessages has a player join a stream that has sent video
	// frames but no audio frame yet: SRS guesses the FLV header from the
	// first messages, so it has video only, and the audio that comes later
	// is dropped, as the guess_has_av docs say.
	check.Case(t, "GuessFromFirstMessages", check.Normal, func(c *check.C) {
		r, hasAudio, hasVideo := earlyJoin(c, server.Common(c))
		c.Check(!hasAudio && hasVideo, "FLV header audio %v video %v, want video only", hasAudio, hasVideo)
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
	})
}

// httpFlvLifecycle adds the lifecycle cases of the checklist. Two are on the
// NoGuess variant: a play before the publish, which there doesn't depend on
// which track comes first, and an unpublish with a player, which on Common
// would hold up the teardown of every stream (see httpFlvPlayer).
func httpFlvLifecycle(t *testing.T) {
	// RepublishPlays unpublishes and publishes the stream again: a new player
	// plays the new publish. SRS refuses a publisher while it still tears
	// down the old HTTP mount, so the publisher retries, as an encoder does.
	check.Case(t, "RepublishPlays", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		stream := "live/" + name
		tags := readTags(c, media.H264HighAAC, 0, 500*time.Millisecond)
		pub := publishTags(c, rtmpURL(srs, stream), tags)
		pub.Close()
		waitHook(c, srs, "on_unpublish", name)

		republishTags(c, rtmpURL(srs, stream), tags, 300*time.Millisecond)
		gop := analyzeTags(c, tags)
		u := report.Until{VideoHeader: true, AudioHeader: true, VideoFrames: gop.Video.Frames, AudioFrames: gop.Audio.Frames}
		r := httpFlvPlay(c, srs, stream+".flv", nil, u)
		checkPlayed(c, r, "h264", "aac")
	})

	check.Case(t, "ManyPlayers", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)

		const n = 8
		players := make([]*flv.HTTPPlayer, n)
		for i := range n {
			players[i] = httpFlvPlayer(c, srs, stream+".flv", nil)
		}
		analyzers, errs := make([]flv.Analyzer, n), make([]error, n)
		var wg sync.WaitGroup
		for i := range n {
			wg.Add(1)
			go func() {
				defer wg.Done()
				errs[i] = readUntil(players[i], &analyzers[i], playedUntil, time.Second)
			}()
		}
		wg.Wait()
		for i := range n {
			if c.CheckNoError(errs[i], fmt.Sprintf("player %d", i)) {
				checkPlayed(c, &analyzers[i].Report, "h264", "aac")
			}
		}
	})

	// PlayerCloseCleanup closes an HTTP-FLV player: its client is gone from
	// the API, and the stream counts only the publisher again.
	check.Case(t, "PlayerCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		stream := "live/" + name
		publishTags(c, rtmpURL(srs, stream), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		readHTTPFlv(c, p, report.Until{VideoHeader: true, Keyframes: 1})
		h := waitHook(c, srs, "on_play", name)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the player in the API")
		c.CheckEqual(streamClients(c, srs, streamPath), 2, "stream clients with a player")

		p.Close()
		waitHook(c, srs, "on_stop", name)
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")
		c.CheckEqual(streamClients(c, srs, streamPath), 1, "stream clients after the player closed")
	})
}

// httpFlvBadInput adds the bad-input cases: each checks that the server
// serves the next player.
func httpFlvBadInput(t *testing.T) {
	// MalformedRequest sends a line that isn't HTTP: SRS answers with an
	// error or closes, and doesn't hang.
	check.Case(t, "MalformedRequest", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.HTTPServer))
		_, err := conn.Write([]byte("NOT HTTP AT ALL\r\n\r\n"))
		c.Must(err, "write")
		conn.SetDeadline(time.Now().Add(300 * time.Millisecond))
		res, err := http.ReadResponse(bufio.NewReader(conn), nil)
		switch {
		case err == nil:
			res.Body.Close()
			c.Check(res.StatusCode >= 400, "status %v, want an error", res.Status)
		case isTimeout(err):
			c.Errorf("no answer and no close within 300ms")
		}
		checkHTTPFlvServes(c, srs)
	})

	// CloseBeforePublish asks for a stream nobody publishes, then closes:
	// the waiting player is gone from the API, and the stream plays for the
	// next player once published.
	check.Case(t, "CloseBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		stream := "live/" + name
		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		h := waitHook(c, srs, "on_play", name)
		clientPath := "/api/v1/clients/" + h.Field("client_id")
		p.Close()
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")

		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)
		checkPlayed(c, httpFlvPlay(c, srs, stream+".flv", nil, playedUntil), "h264", "aac")
	})
}

// TestHttpFlv_NoGuess plays on a variant with guess_has_av off, so the FLV
// header has both tracks, the has_audio and has_video defaults, and with an
// audio fast cache, which the docs say FLV ignores. It has hooks, for the
// on_stop of UnpublishEndsStream.
func TestHttpFlv_NoGuess(t *testing.T) {
	t.Parallel()
	cfg := server.RemuxVariant("flv", server.Remux{GuessOff: true, FastCache: time.Second})
	cfg.Hooks = true
	srs := server.Group(t, cfg)

	// PlayBeforePublish asks for the stream before it is published: the
	// player gets it when the publisher comes, with both tracks.
	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 0)
		checkFlvHeader(c, p, true, true)
		checkPlayed(c, readHTTPFlv(c, p, playedUntil), "h264", "aac")
	})

	// EarlyJoinKeepsAudio is GuessFromFirstMessages without the guess: the
	// header has audio, and the audio that comes later plays.
	check.Case(t, "EarlyJoinKeepsAudio", check.Normal, func(c *check.C) {
		r, hasAudio, hasVideo := earlyJoin(c, srs)
		c.Check(hasAudio && hasVideo, "FLV header audio %v video %v, want both", hasAudio, hasVideo)
		checkVideo(c, r, "h264")
		checkAudio(c, r, "aac", 1)
		c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	})

	// AudioOnlyHeaderHasBoth plays an audio-only stream: the header still
	// has video, from has_video, and only audio comes.
	check.Case(t, "AudioOnlyHeaderHasBoth", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.AACOnly), 200*time.Millisecond)

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkFlvHeader(c, p, true, true)
		r := readHTTPFlv(c, p, report.Until{AudioHeader: true, AudioFrames: 5})
		checkAudio(c, r, "aac", 1)
		c.Check(r.Video == nil, "video %v, want none", r.Video)
	})

	// UnpublishEndsStream closes the publisher: the HTTP-FLV stream of a
	// player ends, and on_stop comes.
	check.Case(t, "UnpublishEndsStream", check.Normal, func(c *check.C) {
		name := streamName(c)
		stream := "live/" + name
		tags := readTags(c, media.H264HighAAC, 0, 500*time.Millisecond)
		pub := publishTags(c, rtmpURL(srs, stream), tags)

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		readHTTPFlv(c, p, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1})
		pub.Close()
		c.CheckNoError(untilStreamEnds(p, time.Second), "the player after the unpublish")
		waitHook(c, srs, "on_stop", name)
	})

	// RepublishAfterOtherStream unpublishes a stream that has an HTTP-FLV
	// player, then another stream with none, and republishes the second at
	// once: it should be accepted, since nothing plays it.
	check.Case(t, "RepublishAfterOtherStream", check.Normal, func(c *check.C) {
		tags := readTags(c, media.H264HighAAC, 0, 500*time.Millisecond)
		watched, other := streamName(c), streamName(c)
		pubWatched := publishTags(c, rtmpURL(srs, "live/"+watched), tags)
		pubOther := publishTags(c, rtmpURL(srs, "live/"+other), tags)
		p := httpFlvPlayer(c, srs, "live/"+watched+".flv", nil)
		readHTTPFlv(c, p, report.Until{VideoHeader: true, Keyframes: 1})

		pubWatched.Close()
		waitHook(c, srs, "on_unpublish", watched)
		pubOther.Close()
		waitHook(c, srs, "on_unpublish", other)
		pub, err := tryPublish(rtmpURL(srs, "live/"+other))
		if err == nil {
			pub.Close()
		} else {
			// A race: the republish comes before or after the teardown ends.
			check.KnownBug(c, "a republish is refused while the HTTP teardown of another stream with a player holds the shared worker")
		}
		c.CheckNoError(err, "republish the stream nobody plays")
	})

	// FastCacheIgnored has a player join a held stream with a fast cache on:
	// it gets the last GOP from its keyframe, as without one.
	check.Case(t, "FastCacheIgnored", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		tags := readTags(c, media.H264HighAAC, 0, 2500*time.Millisecond)
		publishHeld(c, srs, stream, tags)

		gop := analyzeTags(c, lastGOP(tags))
		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		r := readHTTPFlv(c, p, report.Until{VideoHeader: true, AudioHeader: true, VideoFrames: gop.Video.Frames, AudioFrames: gop.Audio.Frames})
		c.CheckNoError(readNothing(p, 100*time.Millisecond), "after the cached GOP")
		c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the first keyframe")
		c.CheckEqual(r.Video.Keyframes, 1, "keyframes")
	})
}

// TestHttpFlv_NoAudio plays on a variant with guess_has_av and has_audio
// off, and crossdomain off.
func TestHttpFlv_NoAudio(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxVariant("flv", server.Remux{GuessOff: true, NoAudio: true, CrossdomainOff: true}))
	staticCrossdomainOffCases(t, srs)

	// AudioDropped plays an H.264/AAC stream: the header has video only, and
	// drop_if_not_match drops the audio.
	check.Case(t, "AudioDropped", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkFlvHeader(c, p, false, true)
		r := readHTTPFlv(c, p, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 10})
		checkVideo(c, r, "h264")
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
	})

	// NoCorsHeaders plays with an Origin header: with crossdomain off, the
	// answer has no CORS headers.
	check.Case(t, "NoCorsHeaders", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishTags(c, rtmpURL(srs, stream), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))

		res := httpResponse(c, httpFlvPlayer(c, srs, stream+".flv", &flv.HTTPOptions{Header: originHeader}))
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "", "Access-Control-Allow-Origin")
	})

	// OptionsNotAllowed sends the OPTIONS preflight with crossdomain off: 405.
	check.Case(t, "OptionsNotAllowed", check.Normal, func(c *check.C) {
		opts := &flv.HTTPOptions{Method: http.MethodOptions, Header: originHeader}
		checkHTTPStatus(c, srs, "live/"+streamName(c)+".flv", opts, 405)
	})

	// ApiCorsKept sends a preflight and a request with an Origin to the API,
	// whose crossdomain is on by default: the HTTP server's crossdomain off
	// doesn't turn off the API's CORS.
	check.Case(t, "ApiCorsKept", check.Fast, func(c *check.C) {
		r := apiRequest(c, httpClient, http.MethodOptions, srs.APIURL("/api/v1/versions"), "", originHeader)
		c.CheckEqual(r.status, http.StatusOK, "preflight status")
		checkCors(c, r, "preflight")
		r = apiRequest(c, httpClient, http.MethodGet, srs.APIURL("/api/v1/versions"), "", originHeader)
		c.CheckEqual(r.status, http.StatusOK, "status")
		checkCors(c, r, "versions")
	})
}

// TestHttpFlv_NoVideoKeep plays on a variant with guess_has_av and has_video
// off, and drop_if_not_match off.
func TestHttpFlv_NoVideoKeep(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxVariant("flv", server.Remux{GuessOff: true, NoVideo: true, KeepUnmatched: true}))

	// VideoKept plays an H.264/AAC stream: the header has audio only, and
	// the video still comes.
	check.Case(t, "VideoKept", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)

		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkFlvHeader(c, p, true, false)
		checkPlayed(c, readHTTPFlv(c, p, playedUntil), "h264", "aac")
	})
}

// TestHttpFlv_RemuxOff plays on a variant with the HTTP server on and the
// HTTP remux off, which it shares with TestHls_Fmp4Default and
// TestDash_Options (see sharedRemuxOff).
func TestHttpFlv_RemuxOff(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	// NotServed asks for a published stream over HTTP-FLV: 404, while it
	// plays over RTMP.
	check.Case(t, "NotServed", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		tags := readTags(c, media.H264HighAAC, 0, 500*time.Millisecond)
		publishTags(c, rtmpURL(srs, stream), tags)
		checkHTTPStatus(c, srs, stream+".flv", nil, 404)
		playUntil(c, rtmpURL(srs, stream), report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1})
	})
}

// publishPlayHTTPFLV publishes a file over RTMP and plays it back over
// HTTP-FLV. The player joins a stream that has already sent 200ms, so its
// first frames come from the GOP cache with audio and video: SRS guesses the
// FLV header from them, and a player that joins before the first audio frame
// gets a video-only stream, which GuessFromFirstMessages covers.
func publishPlayHTTPFLV(c *check.C, file, vcodec, acodec string) {
	srs := server.Common(c)
	stream := "live/" + streamName(c)
	startPublisher(c, "rtmp://"+srs.Addr(server.RTMP)+"/"+stream, media.Path(c, file), 200*time.Millisecond)

	checkPlayed(c, httpFlvPlay(c, srs, stream+".flv", nil, playedUntil), vcodec, acodec)
}

// originHeader is the Origin of a browser page on another site.
var originHeader = http.Header{"Origin": {"https://example.com"}}

// flvURL is the http:// URL of a path on the server's HTTP server.
func flvURL(srs *server.Server, path string) string {
	return "http://" + srs.Addr(server.HTTPServer) + "/" + path
}

// streamURL is the URL a player asks for a path on the server: on its HTTP
// server, or on its HTTPS server for a player with a TLS config.
func streamURL(srs *server.Server, path string, opts *httpplay.Options) string {
	if opts != nil && opts.TLS != nil {
		return "https://" + srs.Addr(server.HTTPSServer) + "/" + path
	}
	return flvURL(srs, path)
}

// httpFlvPlayer sends an HTTP-FLV request for a path on the server and
// returns the player, closed when the case ends. On a server with hooks, the
// close waits, out of the case time, until SRS has dropped the stream's
// players, before the cleanups registered earlier, such as the publisher's,
// run: SRS tears down the HTTP mount of an unpublished stream on one worker
// for every stream, which polls every 100ms while the stream has a player,
// and refuses to publish a stream while its old mount is torn down, so a
// player left at the unpublish would make the republish of other cases fail.
func httpFlvPlayer(c *check.C, srs *server.Server, path string, opts *flv.HTTPOptions) *flv.HTTPPlayer {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	p, err := flv.DialHTTP(ctx, streamURL(srs, path, opts), opts)
	c.Must(err, "dial the HTTP-FLV player")
	c.Cleanup(func() {
		p.Close()
		if srs.Hooks != nil {
			untilPlayersGone(srs, p, path)
		}
	})
	return p
}

// streamTypes are the content types of the HTTP streams SRS serves, by the
// extension of their mount.
var streamTypes = map[string]string{".flv": "video/x-flv", ".ts": "video/MP2T", ".aac": "audio/x-aac", ".mp3": "audio/mpeg"}

// responder is an HTTP player whose answer can be waited for.
type responder interface {
	Response() (*http.Response, error)
	SetDeadline(t time.Time) error
}

// untilPlayersGone waits, for at most 1s, after a player that got a stream
// closed, until the stream has an on_stop for each such player closed so
// far, and the API no longer has the stopped players.
func untilPlayersGone(srs *server.Server, p responder, path string) {
	p.SetDeadline(time.Now())
	base := pathpkg.Base(strings.SplitN(path, "?", 2)[0])
	ext := pathpkg.Ext(base)
	if res, err := p.Response(); err != nil || res.StatusCode != http.StatusOK || res.Header.Get("Content-Type") != streamTypes[ext] {
		return
	}
	name := strings.TrimSuffix(base, ext)
	closedPlayers.Lock()
	closedPlayers.n[name]++
	closed := closedPlayers.n[name]
	closedPlayers.Unlock()

	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		stops := srs.Hooks.Requests(mock.Match{Action: "on_stop", Stream: name})
		if len(stops) >= closed && clientsGone(srs, stops) {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
}

// closedPlayers counts the HTTP players that got a stream closed so far,
// by stream name, which is a case's own.
var closedPlayers = struct {
	sync.Mutex
	n map[string]int
}{n: map[string]int{}}

// clientsGone reports whether the API no longer has the clients of hooks.
func clientsGone(srs *server.Server, hooks []mock.Request) bool {
	for _, h := range hooks {
		res, err := httpClient.Get(srs.APIURL("/api/v1/clients/" + h.Field("client_id")))
		if err != nil {
			return false
		}
		var v struct {
			Code int `json:"code"`
		}
		err = json.NewDecoder(res.Body).Decode(&v)
		res.Body.Close()
		if err != nil || v.Code == 0 {
			return false
		}
	}
	return true
}

// httpResponse waits up to 1s for the answer of a player.
func httpResponse(c *check.C, p responder) *http.Response {
	c.Helper()
	p.SetDeadline(time.Now().Add(time.Second))
	defer p.SetDeadline(time.Time{})
	res, err := p.Response()
	c.Must(err, "the answer")
	return res
}

// checkHTTPStatus sends a request for a path on the server and checks the status of the answer.
func checkHTTPStatus(c *check.C, srs *server.Server, path string, opts *flv.HTTPOptions, status int) *http.Response {
	c.Helper()
	res := httpResponse(c, httpFlvPlayer(c, srs, path, opts))
	c.CheckEqual(res.StatusCode, status, "status")
	return res
}

// checkFlvHeader waits up to 1s for the FLV header of a player and checks
// its flags.
func checkFlvHeader(c *check.C, p *flv.HTTPPlayer, audio, video bool) {
	c.Helper()
	p.SetDeadline(time.Now().Add(time.Second))
	defer p.SetDeadline(time.Time{})
	hasAudio, hasVideo, err := p.Header()
	c.Must(err, "the FLV header")
	c.Check(hasAudio == audio && hasVideo == video, "FLV header audio %v video %v, want audio %v video %v", hasAudio, hasVideo, audio, video)
}

// readHTTPFlv reads a player until u, for at most 1s, and returns the report.
func readHTTPFlv(c *check.C, p *flv.HTTPPlayer, u report.Until) *report.Report {
	c.Helper()
	var a flv.Analyzer
	c.Must(readUntil(p, &a, u, time.Second), "play")
	return &a.Report
}

// httpFlvPlay plays a path on the server over HTTP-FLV until u, for at most
// 1s.
func httpFlvPlay(c *check.C, srs *server.Server, path string, opts *flv.HTTPOptions, u report.Until) *report.Report {
	c.Helper()
	return readHTTPFlv(c, httpFlvPlayer(c, srs, path, opts), u)
}

// untilStreamEnds reads a player until its stream ends, for at most d.
func untilStreamEnds(p *flv.HTTPPlayer, d time.Duration) error {
	p.SetDeadline(time.Now().Add(d))
	defer p.SetDeadline(time.Time{})
	for {
		_, err := p.ReadTag()
		if isTimeout(err) {
			return fmt.Errorf("the stream didn't end within %v", d)
		} else if err != nil {
			return nil
		}
	}
}

// forwardedIP plays with a proxy's headers and checks the client IP in
// on_play and in the API.
func forwardedIP(c *check.C, header http.Header, ip string) {
	srs := server.Common(c)
	name := streamName(c)
	stream := "live/" + name
	publishTags(c, rtmpURL(srs, stream), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))

	p := httpFlvPlayer(c, srs, stream+".flv", &flv.HTTPOptions{Header: header})
	readHTTPFlv(c, p, report.Until{VideoHeader: true, Keyframes: 1})
	h := waitHook(c, srs, "on_play", name)
	c.CheckEqual(h.Field("ip"), ip, "on_play ip")
	var v struct {
		Client struct {
			IP string `json:"ip"`
		} `json:"client"`
	}
	apiGet(c, srs, "/api/v1/clients/"+h.Field("client_id"), &v)
	c.CheckEqual(v.Client.IP, ip, "the client's ip in the API")
}

// earlyJoin publishes the metadata, both sequence headers and the video
// frames of the first 200ms, with no audio frame, and holds the stream; a
// player joins, and then the publisher sends the audio of those 200ms and
// the next 200ms of both tracks. It returns the player's report, once it has
// every video frame, and the flags of its FLV header.
func earlyJoin(c *check.C, srs *server.Server) (r *report.Report, hasAudio, hasVideo bool) {
	c.Helper()
	stream := "live/" + streamName(c)
	held, later := earlyJoinTags(c)
	pub := publishTags(c, rtmpURL(srs, stream), held)

	p := httpFlvPlayer(c, srs, stream+".flv", nil)
	p.SetDeadline(time.Now().Add(time.Second))
	hasAudio, hasVideo, err := p.Header()
	c.Must(err, "the FLV header")
	p.SetDeadline(time.Time{})
	c.Must(writeTags(pub, later), "publish the rest")

	all := analyzeTags(c, append(append([]*media.FLVTag(nil), held...), later...))
	var a flv.Analyzer
	c.Must(readUntil(p, &a, report.Until{VideoFrames: all.Video.Frames}, time.Second), "play")
	// The audio of the last video frame's time may come after it.
	readIdle(p, &a, report.Until{AudioFrames: all.Audio.Frames}, 50*time.Millisecond)
	return &a.Report, hasAudio, hasVideo
}

// earlyJoinTags splits the first 400ms of the H.264/AAC file: held is the
// metadata, both sequence headers and the video frames of the first 200ms,
// and later the audio frames of those 200ms and both tracks of the next
// 200ms.
func earlyJoinTags(c *check.C) (held, later []*media.FLVTag) {
	c.Helper()
	for _, tag := range readTags(c, media.H264HighAAC, 0, 200*time.Millisecond) {
		if tag.Type == media.TagAudio && !isHeader(tag) {
			later = append(later, tag)
		} else {
			held = append(held, tag)
		}
	}
	return held, append(later, readTags(c, media.H264HighAAC, 200*time.Millisecond, 400*time.Millisecond)...)
}

// checkHTTPFlvServes checks the server still serves an HTTP-FLV player.
func checkHTTPFlvServes(c *check.C, srs *server.Server) {
	c.Helper()
	stream := "live/" + streamName(c)
	publishTags(c, rtmpURL(srs, stream), readTags(c, media.H264HighAAC, 0, 500*time.Millisecond))
	httpFlvPlay(c, srs, stream+".flv", nil, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1})
}

// republishTags publishes tags at once and holds the stream, like
// publishTags, but retries every 10ms, for at most within, while SRS refuses
// the publisher.
func republishTags(c *check.C, url string, tags []*media.FLVTag, within time.Duration) *rtmp.Client {
	c.Helper()
	deadline := time.Now().Add(within)
	for tries := 1; ; tries++ {
		pub, err := tryPublish(url)
		if err == nil {
			c.Cleanup(func() { pub.Close() })
			c.Must(writeTags(pub, tags), "publish the media")
			return pub
		}
		if time.Now().After(deadline) {
			c.Fatalf("publish refused %d times within %v: %v", tries, within, err)
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// tryPublish dials and publishes, and closes the client when it fails.
func tryPublish(url string) (*rtmp.Client, error) {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	pub, err := rtmp.Dial(ctx, url, nil)
	if err != nil {
		return nil, err
	}
	if _, err := pub.Publish(ctx, rtmp.FMLE); err != nil {
		pub.Close()
		return nil, err
	}
	return pub, nil
}

// publishHeld publishes tags at once and holds the stream, like publishTags,
// and returns once SRS has them all: a player that joined before the publish
// got every one.
func publishHeld(c *check.C, srs *server.Server, stream string, tags []*media.FLVTag) {
	c.Helper()
	url := rtmpURL(srs, stream)
	witness := playerOf(c, url)
	publishTags(c, url, tags)
	all := analyzeTags(c, tags)
	var a flv.Analyzer
	c.Must(readUntil(witness, &a, report.Until{VideoFrames: all.Video.Frames, AudioFrames: all.Audio.Frames}, time.Second), "the witness player")
	witness.Close()
}
