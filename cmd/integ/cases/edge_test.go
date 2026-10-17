// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// edgeServerConfig is a server of the edge groups, an origin or an edge: RTMP,
// the API, the HTTP server with every stream as HTTP-FLV at
// /[app]/[stream].flv, which an edge pulls from with the protocol flv, every
// hook on its own mock, and the lowest play latency.
func edgeServerConfig() server.Config {
	cfg := server.PlayConfig(server.Play{})
	cfg.Listeners = append(cfg.Listeners, server.HTTPServer)
	cfg.Env["SRS_VHOST_HTTP_REMUX_ENABLED"] = "on"
	cfg.Env["SRS_VHOST_HTTP_REMUX_MOUNT"] = "[vhost]/[app]/[stream].flv"
	cfg.Hooks = true
	return cfg
}

// edgeOrigin is the origin the edge groups of a process share.
func edgeOrigin(t testing.TB) *server.Server {
	t.Helper()
	return server.Shared(t, "EdgeOrigin", edgeServerConfig())
}

// edgeGroup starts an edge with the settings e for a group.
func edgeGroup(t *testing.T, e server.Edge) *server.Server {
	t.Helper()
	return server.Group(t, server.WithEdge(edgeServerConfig(), e))
}

// edgePlayed plays a path on an edge over RTMP, which the edge pulls from its
// origin, until it plays, and checks it is the H.264/AAC test file.
func edgePlayed(c *check.C, edge *server.Server, path string) *report.Report {
	c.Helper()
	r := playUntil(c, rtmpURL(edge, path), playedUntil)
	checkPlayed(c, r, "h264", "aac")
	return r
}

// TestEdge_Rtmp checks an edge with the origin, over RTMP, the default
// protocol: each stream a player asks the edge for, published on the origin,
// plays on the edge, and each stream published to the edge goes to the
// origin.
func TestEdge_Rtmp(t *testing.T) {
	t.Parallel()
	origin := edgeOrigin(t)
	edge := edgeGroup(t, server.Edge{Origins: []string{origin.Addr(server.RTMP)}})
	edgePullCases(t, origin, edge)

	// PlayBeforePublish plays from the edge before the stream is published
	// on the origin: the edge's pull waits at the origin, and the player
	// gets the stream once it is published.
	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		stream := streamName(c)
		player := playerOf(c, rtmpURL(edge, "live/"+stream))
		waitHook(c, origin, "on_play", stream)
		startPublisher(c, rtmpURL(origin, "live/"+stream), media.Path(c, media.H264HighAAC), 0)

		var a flv.Analyzer
		c.Must(readUntil(player, &a, playedUntil, 2*time.Second), "play")
		checkPlayed(c, &a.Report, "h264", "aac")
	})

	edgePushCases(t, origin, edge)
}

// TestEdge_Flv checks an edge that pulls from the origin over HTTP-FLV
// (protocol flv), from the origin's HTTP server: the pull cases of RTMP.
func TestEdge_Flv(t *testing.T) {
	t.Parallel()
	origin := edgeOrigin(t)
	edge := edgeGroup(t, server.Edge{Origins: []string{origin.Addr(server.HTTPServer)}, Protocol: "flv"})
	edgePullCases(t, origin, edge)
}

// edgePullCases adds the cases of an edge that pulls the streams of origin.
func edgePullCases(t *testing.T, origin, edge *server.Server) {
	// RtmpPlay plays a stream published on the origin from the edge over
	// RTMP: the player gets it as it was published.
	check.Case(t, "RtmpPlay", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, origin, stream, media.H264HighAAC)
		edgePlayed(c, edge, "live/"+stream)
	})

	// HttpFlvPlay plays it from the edge over HTTP-FLV.
	check.Case(t, "HttpFlvPlay", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, origin, stream, media.H264HighAAC)
		checkPlayed(c, httpFlvPlay(c, edge, "live/"+stream+".flv", nil, playedUntil), "h264", "aac")
	})

	// Hevc pulls HEVC over enhanced RTMP: the edge's player gets HEVC.
	check.Case(t, "Hevc", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, origin, stream, media.HEVCAAC)
		r := playUntil(c, rtmpURL(edge, "live/"+stream), playedUntil)
		checkPlayed(c, r, "hevc", "aac")
	})

	// LatePlayer plays from the edge, then a second player joins while the
	// first plays: the second starts at a keyframe with both sequence
	// headers, from the edge's GOP cache, and the edge pulls the stream from
	// the origin once for both.
	check.Case(t, "LatePlayer", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, origin, stream, media.H264HighAAC)
		first := playerOf(c, rtmpURL(edge, "live/"+stream))
		var a flv.Analyzer
		c.Must(readUntil(first, &a, playedUntil, 2*time.Second), "the first player")

		time.Sleep(200 * time.Millisecond)
		r := edgePlayed(c, edge, "live/"+stream)
		c.CheckEqual(r.Video.LeadingFrames, 0, "frames before the late player's first keyframe")
		c.CheckEqual(len(origin.Hooks.Requests(mock.Match{Action: "on_play", Stream: stream})), 1, "the origin's players of the stream, the edge")
	})

	// ParamToOrigin plays with a query: the edge asks the origin for the
	// stream with it, so the origin's on_play has the player's param.
	check.Case(t, "ParamToOrigin", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, origin, stream, media.H264HighAAC)
		edgePlayed(c, edge, "live/"+stream+"?token=abc")
		h := waitHook(c, origin, "on_play", stream)
		c.CheckEqual(h.Field("param"), "?token=abc", "the origin's on_play param")
	})

	// LastPlayerStopsPull closes the edge's only player: the edge stops
	// pulling the stream, and the origin gets its on_stop.
	check.Case(t, "LastPlayerStopsPull", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, origin, stream, media.H264HighAAC)
		player := playerOf(c, rtmpURL(edge, "live/"+stream))
		var a flv.Analyzer
		c.Must(readUntil(player, &a, playedUntil, 2*time.Second), "play")
		player.Close()
		waitHook(c, origin, "on_stop", stream)
	})
}

// edgePushCases adds the cases of a publish to an edge, which pushes the
// stream to its origin. The edge sends what it queued each time its
// connection to the origin reads a message or times out, every 500ms, so a
// player of the origin gets the media at once that the publisher sends before
// the push starts, and the rest in batches.
func edgePushCases(t *testing.T, origin, edge *server.Server) {
	// PublishToOrigin publishes to the edge: the origin's player gets the
	// stream as it was published.
	check.Case(t, "PublishToOrigin", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, edge, stream, media.H264HighAAC)
		r := playUntil(c, rtmpURL(origin, "live/"+stream), playedUntil)
		checkPlayed(c, r, "h264", "aac")
	})

	// PublishPlayOnEdge publishes to the edge and plays from it: the edge
	// pulls the stream back from the origin.
	check.Case(t, "PublishPlayOnEdge", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, edge, stream, media.H264HighAAC)
		edgePlayed(c, edge, "live/"+stream)
	})

	// PublishParamToOrigin publishes with a query: the origin's on_publish
	// has it.
	check.Case(t, "PublishParamToOrigin", check.Normal, func(c *check.C) {
		stream := streamName(c)
		startPublisher(c, rtmpURL(edge, "live/"+stream+"?token=abc"), media.Path(c, media.H264HighAAC), publishLead)
		h := waitHook(c, origin, "on_publish", stream)
		c.CheckEqual(h.Field("param"), "?token=abc", "the origin's on_publish param")
	})

	// SecondPublisherRejected publishes the stream to the edge again while
	// it is published there: the edge refuses the second publisher.
	check.Case(t, "SecondPublisherRejected", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, edge, stream, media.H264HighAAC)
		r := tryRTMP(c, rtmpURL(edge, "live/"+stream), nil, true, hookWait())
		checkRefused(c, r, "")
		waitHook(c, origin, "on_publish", stream)
		c.CheckEqual(len(origin.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 1, "the origin's publishes of the stream")
	})

	// UnpublishReachesOrigin closes the edge's publisher: the origin's
	// stream is unpublished.
	check.Case(t, "UnpublishReachesOrigin", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub, _, _ := publishRTMP(c, edge, stream, media.H264HighAAC)
		waitHook(c, origin, "on_publish", stream)
		pub.Close()
		waitHook(c, origin, "on_unpublish", stream)
	})
}

// TestTimer_Edge_Failover checks an edge with two origins, the first down,
// which takes them in turn for each stream: when one fails, it takes the
// next, the pull at its next try, the push at once. Its play case is a timer
// case: the edge waits 3s before it pulls again.
func TestTimer_Edge_Failover(t *testing.T) {
	t.Parallel()
	origin := edgeOrigin(t)
	edge := edgeGroup(t, server.Edge{Origins: []string{server.DownAddr(t), origin.Addr(server.RTMP)}})

	// PlayFailover plays from the edge: its first pull fails, and the player
	// gets the stream from the second origin.
	check.Case(t, "PlayFailover", check.Timer, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, origin, stream, media.H264HighAAC)
		player := playerOf(c, rtmpURL(edge, "live/"+stream))
		var a flv.Analyzer
		c.Must(readUntil(player, &a, playedUntil, 5*time.Second), "play")
		checkPlayed(c, &a.Report, "h264", "aac")
	})

	// PublishFailover publishes to the edge: its push to the first origin
	// fails, and the edge accepts the publish and pushes the stream to the
	// second, whose player gets it.
	check.Case(t, "PublishFailover", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, edge, stream, media.H264HighAAC)
		r := playUntil(c, rtmpURL(origin, "live/"+stream), playedUntil)
		checkPlayed(c, r, "h264", "aac")
	})

	// RepublishFailover publishes to the edge, stops and publishes the stream
	// again: the edge takes the origins in turn, so each publish starts at the
	// first origin, down, and goes to the second.
	check.Case(t, "RepublishFailover", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub, _, _ := publishRTMP(c, edge, stream, media.H264HighAAC)
		waitHook(c, origin, "on_publish", stream)
		pub.Close()
		waitHook(c, origin, "on_unpublish", stream)
		startPublisher(c, rtmpURL(edge, "live/"+stream), media.Path(c, media.H264HighAAC), publishLead)
		waitHookCount(c, origin, mock.Match{Action: "on_publish", Stream: stream}, 2)
	})
}
