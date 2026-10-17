// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// TestCluster_Origins checks an origin cluster of two origins, A and B, which
// know each other's HTTP API as coworkers: a player of an origin that doesn't
// publish its stream gets an RTMP 302 redirect to the coworker that does,
// which /api/v1/clusters names. A asks itself first, then B; B asks a
// coworker that is down first, then A, then itself. An edge whose origin is
// B follows B's redirect too.
func TestCluster_Origins(t *testing.T) {
	t.Parallel()
	apiA, apiB := server.ReservePort(t), server.ReservePort(t)
	addrA, addrB := "127.0.0.1:"+strconv.Itoa(apiA), "127.0.0.1:"+strconv.Itoa(apiB)
	origin := func(api int, coworkers ...string) *server.Server {
		cfg := server.WithOriginCluster(edgeServerConfig(), server.OriginCluster{Coworkers: coworkers})
		cfg.APIPort = api
		return server.Group(t, cfg)
	}
	a := origin(apiA, addrA, addrB)
	b := origin(apiB, server.DownAddr(t), addrA, addrB)
	edge := edgeGroup(t, server.Edge{Origins: []string{b.Addr(server.RTMP)}})

	// ClustersPublished asks A's /api/v1/clusters for a stream A publishes:
	// the query as it was asked, and the origin: the coworker's host, A's
	// RTMP port, the stream's vhost, and A's API as the api and the only
	// router.
	check.Case(t, "ClustersPublished", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		v := apiObject(c, a, clustersPath("__defaultVhost__", stream, a.Addr(server.HTTPAPI)))
		data := object(c, v, "data")
		c.CheckEqual(data["query"], map[string]any{"ip": "10.0.0.1", "vhost": "__defaultVhost__", "app": "live", "stream": stream}, "query")
		c.CheckEqual(data["origin"], clusterOrigin(a), "origin")
	})

	// ClustersNotPublished asks B for a stream it doesn't publish: no origin.
	check.Case(t, "ClustersNotPublished", check.Fast, func(c *check.C) {
		v := apiObject(c, b, clustersPath("__defaultVhost__", streamName(c), b.Addr(server.HTTPAPI)))
		c.CheckEqual(object(c, v, "data")["origin"], nil, "origin")
	})

	// ClustersAfterUnpublish asks A for a stream it published, after the
	// publisher left: no origin.
	check.Case(t, "ClustersAfterUnpublish", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub, _, _ := publishRTMP(c, a, stream, media.H264HighAAC)
		pub.Close()
		waitHook(c, a, "on_unpublish", stream)
		v := apiObject(c, a, clustersPath("__defaultVhost__", stream, a.Addr(server.HTTPAPI)))
		c.CheckEqual(object(c, v, "data")["origin"], nil, "origin")
	})

	// ClustersOtherVhost asks with a vhost the server doesn't have: it looks
	// in the default vhost, and names the stream's.
	check.Case(t, "ClustersOtherVhost", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		v := apiObject(c, a, clustersPath("other.example.com", stream, a.Addr(server.HTTPAPI)))
		c.CheckEqual(object(c, v, "data")["origin"], clusterOrigin(a), "origin")
	})

	// ClustersByPost asks with a POST, as an origin asks its coworkers: the
	// same answer.
	check.Case(t, "ClustersByPost", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		req, err := http.NewRequest(http.MethodPost, a.APIURL(clustersPath("__defaultVhost__", stream, a.Addr(server.HTTPAPI))), nil)
		c.Must(err, "request")
		r := apiDo(c, httpClient, req)
		c.CheckEqual(r.status, http.StatusOK, "status")
		var v map[string]any
		c.Must(json.Unmarshal(r.body, &v), "decode "+string(r.body))
		c.CheckEqual(v["code"], 0.0, "code")
		c.CheckEqual(object(c, v, "data")["origin"], clusterOrigin(a), "origin")
	})

	// PlayRedirected plays on B a stream A publishes: B starts the play,
	// then redirects the player to A, past its coworker that is down, and
	// the player gets the stream from A.
	check.Case(t, "PlayRedirected", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		_, st := redirectOf(c, rtmpURL(b, "live/"+stream))
		c.CheckEqual(st, redirectStatus(a, stream), "redirect")
		checkPlayed(c, playUntil(c, st.Ex.Redirect2, playedUntil), "h264", "aac")
	})

	// RedirectAcknowledged answers the redirect with a call, as a client
	// that follows it does: B closes the connection at once.
	check.Case(t, "RedirectAcknowledged", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		player, _ := redirectOf(c, rtmpURL(b, "live/"+stream))
		c.Must(player.Send("redirected", "ok"), "answer the redirect")
		c.CheckNoError(untilEOF(player, 300*time.Millisecond), "closed after the answer")
	})

	// SelfCoworkerSkipped plays on A a stream B publishes: A asks itself
	// first, which has no origin, then B, and redirects the player to B.
	check.Case(t, "SelfCoworkerSkipped", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, b, stream, media.H264HighAAC)
		_, st := redirectOf(c, rtmpURL(a, "live/"+stream))
		c.CheckEqual(st, redirectStatus(b, stream), "redirect")
		checkPlayed(c, playUntil(c, st.Ex.Redirect2, playedUntil), "h264", "aac")
	})

	// ParamInRedirect plays on B with a query: the redirect keeps it, and
	// A's on_play has it.
	check.Case(t, "ParamInRedirect", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		_, st := redirectOf(c, rtmpURL(b, "live/"+stream+"?token=abc"))
		c.CheckEqual(st.Ex.Redirect2, rtmpURL(a, "live/"+stream+"?token=abc"), "redirect2")
		checkPlayed(c, playUntil(c, st.Ex.Redirect2, playedUntil), "h264", "aac")
		c.CheckEqual(waitHook(c, a, "on_play", stream).Field("param"), "?token=abc", "A's on_play param")
	})

	// PlayOnPublisher plays on A a stream A publishes: it plays, with no
	// redirect.
	check.Case(t, "PlayOnPublisher", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		player, r := playRTMPClient(c, rtmpURL(a, "live/"+stream), nil)
		checkPlayed(c, r, "h264", "aac")
		for _, st := range player.Statuses() {
			c.Check(st.Level != "error", "status %+v, want no error", st)
		}
	})

	// NoOriginCloses plays on B a stream no origin publishes: B starts the
	// play, then, with no coworker to redirect to, closes the connection.
	check.Case(t, "NoOriginCloses", check.Normal, func(c *check.C) {
		checkNoOriginCloses(c, rtmpURL(b, "live/"+streamName(c)), nil)
	})

	// NoOriginClosesAfterBufferLength plays as NoOriginCloses, with the
	// buffer length 1ms after the play, so it comes after B read the play:
	// B reads it before the close, which is then no reset that drops
	// NetStream.Play.Start.
	check.Case(t, "NoOriginClosesAfterBufferLength", check.Normal, func(c *check.C) {
		checkNoOriginCloses(c, rtmpURL(b, "live/"+streamName(c)), &rtmp.Options{PlayGap: time.Millisecond})
	})

	// RepublishOnOtherOrigin publishes a stream on A, then, once it left,
	// on B: a player of A is redirected to B.
	check.Case(t, "RepublishOnOtherOrigin", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub, _, _ := publishRTMP(c, a, stream, media.H264HighAAC)
		pub.Close()
		waitHook(c, a, "on_unpublish", stream)
		publishRTMP(c, b, stream, media.H264HighAAC)
		_, st := redirectOf(c, rtmpURL(a, "live/"+stream))
		c.CheckEqual(st, redirectStatus(b, stream), "redirect")
	})

	// EdgePlaysFromCluster plays from an edge whose origin is B a stream A
	// publishes: the edge follows B's redirect and pulls from A.
	check.Case(t, "EdgePlaysFromCluster", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishRTMP(c, a, stream, media.H264HighAAC)
		player := playerOf(c, rtmpURL(edge, "live/"+stream))
		var an flv.Analyzer
		c.Must(readUntil(player, &an, playedUntil, 2*time.Second), "play")
		checkPlayed(c, &an.Report, "h264", "aac")
		waitHook(c, a, "on_play", stream)
	})
}

// checkNoOriginCloses plays a URL of an origin with no coworker that has the
// stream: the play starts, then the origin closes the connection, with no
// status and no reset.
func checkNoOriginCloses(c *check.C, u string, opts *rtmp.Options) {
	c.Helper()
	player := dialRTMP(c, u, opts)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := player.Play(ctx)
	c.Must(err, "play")
	_, err = player.WaitStatus(ctx, "NetStream.Play.UnpublishNotify")
	c.Check(errors.Is(err, io.EOF), "after the play: %v, want closed with no status", err)
}

// clustersPath is the /api/v1/clusters path that asks a server for the
// origin of live/stream, from a client at 10.0.0.1, as an origin asks its
// coworker: the values as they are, since SRS reads them without decoding.
func clustersPath(vhost, stream, coworker string) string {
	return "/api/v1/clusters?vhost=" + vhost + "&ip=10.0.0.1&app=live&stream=" + stream + "&coworker=" + coworker
}

// clusterOrigin is the origin /api/v1/clusters names for a stream an origin
// publishes, asked with the origin's API as the coworker.
func clusterOrigin(o *server.Server) map[string]any {
	p, _ := strconv.Atoi(port(o))
	api := o.Addr(server.HTTPAPI)
	return map[string]any{"ip": "127.0.0.1", "port": float64(p), "vhost": "__defaultVhost__", "api": api, "routers": []any{api}}
}

// redirectStatus is the RTMP 302 redirect of a player of live/stream to the
// origin that publishes it.
func redirectStatus(o *server.Server, stream string) rtmp.Status {
	return rtmp.Status{
		Command: "onStatus", Level: "error", Code: "NetConnection.Connect.Rejected", Description: "RTMP 302 Redirect",
		Ex: rtmp.StatusEx{Code: 302, Redirect: rtmpURL(o, "live"), Redirect2: rtmpURL(o, "live/"+stream)},
	}
}

// redirectOf plays a URL, and returns the player and the status of level
// error it gets after NetStream.Play.Start, such as a redirect.
func redirectOf(c *check.C, u string) (*rtmp.Client, rtmp.Status) {
	c.Helper()
	player := playerOf(c, u)
	ctx, cancel := context.WithTimeout(context.Background(), 400*time.Millisecond)
	defer cancel()
	_, err := player.WaitStatus(ctx, "NetStream.Play.UnpublishNotify")
	var se *rtmp.StatusError
	if !errors.As(err, &se) {
		c.Must(fmt.Errorf("after the play: %v, want a status of level error", err), "redirect")
	}
	return player, se.Status
}
