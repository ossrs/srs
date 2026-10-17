// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"slices"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// forwardNothing is how long a case waits to see that a server doesn't
// forward a stream: a forwarder connects and publishes within a few ms of the
// publish it forwards, which the case has already seen played.
const forwardNothing = 200 * time.Millisecond

// forwardServerConfig is a server of the forward groups: RTMP and the API,
// every hook on its own mock, which records the publishes of the forwarders
// that reach it, and the lowest play latency.
func forwardServerConfig() server.Config {
	cfg := server.PlayConfig(server.Play{})
	cfg.Hooks = true
	return cfg
}

// forwardDest1 is the first destination the forward groups share. It
// forwards nothing.
func forwardDest1(t testing.TB) *server.Server {
	t.Helper()
	return server.Shared(t, "ForwardDest1", forwardServerConfig())
}

// forwardDest2 is the second destination the forward groups share. It has
// forward off with the first destination set, so a stream published to it
// shows that forward off sends nothing.
func forwardDest2(t testing.TB) *server.Server {
	t.Helper()
	d1 := forwardDest1(t)
	return server.Shared(t, "ForwardDest2", server.WithForward(forwardServerConfig(), server.Forward{
		Destinations: []string{d1.Addr(server.RTMP)}, Off: true,
	}))
}

// forwardURL is the URL a forward to a server lists, without the query SRS
// adds, such as the vhost.
func forwardURL(srs *server.Server, path string) string {
	return rtmpURL(srs, path)
}

// forwardURLIs reports whether a forward's URL is want, maybe with a query
// SRS added after want's own.
func forwardURLIs(got, want string) bool {
	sep := "?"
	if strings.Contains(want, "?") {
		sep = "&"
	}
	return got == want || strings.HasPrefix(got, want+sep)
}

// forwardsOf is the forwards of a stream, by its id.
func forwardsOf(c *check.C, srs *server.Server, streamID string) []map[string]any {
	c.Helper()
	fwds, _ := apiList(c, srs, "forwards", "?stream="+streamID)
	return fwds
}

// forwardsMatch reports whether the forwards are exactly one for each URL,
// in any order, and all in the state.
func forwardsMatch(fwds []map[string]any, urls []string, state string) bool {
	if len(fwds) != len(urls) {
		return false
	}
	used := map[int]bool{}
	for _, u := range urls {
		found := false
		for i, f := range fwds {
			got, _ := f["url"].(string)
			if !used[i] && f["state"] == state && forwardURLIs(got, u) {
				used[i], found = true, true
				break
			}
		}
		if !found {
			return false
		}
	}
	return true
}

// waitForwards waits up to d until a stream's forwards are exactly the URLs,
// all in the state, and returns them.
func waitForwards(c *check.C, srs *server.Server, streamID string, urls []string, state string, d time.Duration) []map[string]any {
	c.Helper()
	var fwds []map[string]any
	if !until(d, func() bool {
		fwds = forwardsOf(c, srs, streamID)
		return forwardsMatch(fwds, urls, state)
	}) {
		c.Fatalf("forwards %v after %v, want %v each %s", fwds, d, urls, state)
	}
	return fwds
}

// checkNoForward checks a server got no publish of a stream: the case has
// seen the stream play where it was published, and waits forwardNothing more.
func checkNoForward(c *check.C, dest *server.Server, stream, why string) {
	c.Helper()
	time.Sleep(forwardNothing)
	got := dest.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})
	c.Check(len(got) == 0, "%s: %d publishes of %s, want none", why, len(got), stream)
}

// publishForwarded publishes a file on a server's path, such as live/s or
// live/s?k=v, once SRS forwards it to each URL: the publisher sends no media
// until every forward is connected, since a forwarder starts its destination
// at a keyframe that comes after it connected, and a test file has one per
// second. It returns the ids of the on_publish: the client and the stream.
func publishForwarded(c *check.C, srs *server.Server, path, file string, urls []string) (string, string) {
	c.Helper()
	stream := strings.TrimPrefix(path, "live/")
	stream, _, _ = strings.Cut(stream, "?")
	pubs := srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})
	pub := dialRTMP(c, rtmpURL(srs, path), nil)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := pub.Publish(ctx, rtmp.FMLE)
	c.Must(err, "publish")
	h := waitHookCountOf(c, srs, mock.Match{Action: "on_publish", Stream: stream}, len(pubs)+1)[len(pubs)]
	waitForwards(c, srs, h.Field("stream_id"), urls, "forwarding", time.Second)
	streamFile(c, pub, media.Path(c, file), publishLead)
	return h.Field("client_id"), h.Field("stream_id")
}

// playForwarded plays a stream a forwarder publishes on a destination, of
// the H.264/AAC test file, and checks it.
func playForwarded(c *check.C, dest *server.Server, path string) {
	c.Helper()
	r := playUntil(c, rtmpURL(dest, path), playedUntil)
	checkPlayed(c, r, "h264", "aac")
}

// The forward groups are timer groups: a forwarder sends what it queued once
// per receive timeout of its connection to the destination, 500ms, so a
// forwarded stream reaches its destination in batches up to 500ms after the
// publisher sent it, and a case that plays it there takes longer than a
// Normal case may. Their cases that play nothing forwarded stay Normal.

// TestTimer_Forward_Destination checks forward by destination: an origin that
// forwards every stream to two destinations, by its app and stream name.
func TestTimer_Forward_Destination(t *testing.T) {
	t.Parallel()
	d1, d2 := forwardDest1(t), forwardDest2(t)
	srs := server.Group(t, server.WithForward(forwardServerConfig(), server.Forward{
		Destinations: []string{d1.Addr(server.RTMP), d2.Addr(server.RTMP)},
	}))
	urlsOf := func(stream string) []string {
		return []string{forwardURL(d1, "live/"+stream), forwardURL(d2, "live/"+stream)}
	}

	// TwoDestinations publishes H.264/AAC: each destination gets the stream
	// as it was published, under the same app and stream.
	check.Case(t, "TwoDestinations", check.Timer, func(c *check.C) {
		stream := streamName(c)
		publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, urlsOf(stream))
		playForwarded(c, d1, "live/"+stream)
		playForwarded(c, d2, "live/"+stream)
	})

	// AudioOnly and VideoOnly forward a stream of one track: each
	// destination gets that track and no other.
	check.Case(t, "AudioOnly", check.Timer, func(c *check.C) {
		stream := streamName(c)
		publishForwarded(c, srs, "live/"+stream, media.AACOnly, urlsOf(stream))
		for _, d := range []*server.Server{d1, d2} {
			r := playUntil(c, rtmpURL(d, "live/"+stream), report.Until{Metadata: true, AudioHeader: true, AudioFrames: 5})
			c.Check(r.Video == nil, "video in a forwarded audio-only stream: %v", r)
			checkAudio(c, r, "aac", 1)
			c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
		}
	})

	check.Case(t, "VideoOnly", check.Timer, func(c *check.C) {
		stream := streamName(c)
		publishForwarded(c, srs, "live/"+stream, media.H264Only, urlsOf(stream))
		for _, d := range []*server.Server{d1, d2} {
			r := playUntil(c, rtmpURL(d, "live/"+stream), report.Until{Metadata: true, VideoHeader: true, Keyframes: 1, VideoFrames: 5})
			c.Check(r.Audio == nil, "audio in a forwarded video-only stream: %v", r)
			checkVideo(c, r, "h264")
			c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
		}
	})

	// Hevc forwards HEVC over enhanced RTMP: the destination's player gets
	// it in enhanced RTMP too.
	check.Case(t, "Hevc", check.Timer, func(c *check.C) {
		stream := streamName(c)
		publishForwarded(c, srs, "live/"+stream, media.HEVCAAC, urlsOf(stream))
		r := playUntil(c, rtmpURL(d1, "live/"+stream), playedUntil)
		checkPlayed(c, r, "hevc", "aac")
	})

	// QueryPassedThrough publishes with a query: each destination's
	// on_publish has the same app and stream, and the query in its param.
	check.Case(t, "QueryPassedThrough", check.Normal, func(c *check.C) {
		stream := streamName(c)
		publishForwarded(c, srs, "live/"+stream+"?token=abc&k=v", media.H264HighAAC, urlsOf(stream))
		for _, d := range []*server.Server{d1, d2} {
			h := waitHook(c, d, "on_publish", stream)
			c.CheckEqual(h.Field("app"), "live", "the destination's app")
			c.CheckEqual(h.Field("param"), "?token=abc&k=v", "the destination's param")
		}
	})

	// Status lists the stream's two forwards with their destination URLs,
	// forwarding, each with the same id by its own path and on a later read.
	check.Case(t, "Status", check.Timer, func(c *check.C) {
		stream := streamName(c)
		_, streamID := publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, urlsOf(stream))
		fwds := forwardsOf(c, srs, streamID)
		for _, f := range fwds {
			id, _ := f["id"].(string)
			byID := object(c, apiObject(c, srs, "/api/v1/forwards/"+id), "forward")
			c.CheckEqual(byID["url"], f["url"], "the forward's url by its id")
			c.CheckEqual(byID["state"], "forwarding", "the forward's state by its id")
		}
		playForwarded(c, d1, "live/"+stream)
		c.CheckEqual(ids(forwardsOf(c, srs, streamID)), ids(fwds), "the forward ids while it forwards")
	})

	// KickRepublishNewIds deletes the publisher by the API and publishes
	// again: the forwards go with the publisher, and the republish makes the
	// same forwards with new ids, which the destinations play again.
	check.Case(t, "KickRepublishNewIds", check.Timer, func(c *check.C) {
		stream := streamName(c)
		pubID, streamID := publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, urlsOf(stream))
		before := forwardsOf(c, srs, streamID)

		checkKicked(c, apiKick(c, srs, pubID))
		for _, d := range []*server.Server{srs, d1, d2} {
			waitHook(c, d, "on_unpublish", stream)
		}
		c.Check(len(forwardsOf(c, srs, streamID)) == 0, "forwards after the kick, want none")

		_, streamID = publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, urlsOf(stream))
		for _, id := range ids(forwardsOf(c, srs, streamID)) {
			c.Check(!slices.Contains(ids(before), id), "forward id %s of the republish, want a new one of %v", id, ids(before))
		}
		playForwarded(c, d1, "live/"+stream)
		playForwarded(c, d2, "live/"+stream)
	})
}

// TestTimer_Forward_OneDestination checks forward to one destination: the
// destination gets the stream, and the other shared destination, not
// configured here, gets nothing.
func TestTimer_Forward_OneDestination(t *testing.T) {
	t.Parallel()
	d1, d2 := forwardDest1(t), forwardDest2(t)
	srs := server.Group(t, server.WithForward(forwardServerConfig(), server.Forward{
		Destinations: []string{d1.Addr(server.RTMP)},
	}))

	check.Case(t, "OneDestination", check.Timer, func(c *check.C) {
		stream := streamName(c)
		publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, []string{forwardURL(d1, "live/"+stream)})
		playForwarded(c, d1, "live/"+stream)
		checkNoForward(c, d2, stream, "a destination not configured")
	})
}

// TestTimer_Forward_Off checks forward off with a destination set, on the second
// shared destination, which has its own forward off: a stream published there
// plays, and nothing reaches the destination or the forwards list.
func TestTimer_Forward_Off(t *testing.T) {
	t.Parallel()
	d1, d2 := forwardDest1(t), forwardDest2(t)

	check.Case(t, "OffSendsNothing", check.Normal, func(c *check.C) {
		stream := streamName(c)
		_, _, streamID := publishRTMP(c, d2, stream, media.H264HighAAC)
		playForwarded(c, d2, "live/"+stream)
		checkNoForward(c, d1, stream, "forward off")
		c.Check(len(forwardsOf(c, d2, streamID)) == 0, "forwards of a stream with forward off, want none")
	})
}

// TestTimer_Forward_Backend checks forward by a backend, the mock, which answers
// on_forward with the URLs to forward each stream to. The origin has a
// destination too, the second shared destination, which SRS ignores while
// a backend is set. The backend answers no URL but for the streams a case
// scripts.
func TestTimer_Forward_Backend(t *testing.T) {
	t.Parallel()
	d1, d2 := forwardDest1(t), forwardDest2(t)
	cfg := forwardServerConfig()
	cfg.ForwardBackend = true
	srs := server.Group(t, server.WithForward(cfg, server.Forward{Destinations: []string{d2.Addr(server.RTMP)}}))
	srs.Hooks.Script(mock.Match{Action: "on_forward"}, forwardTo())
	forwards := func(stream string, urls ...string) {
		srs.Hooks.Script(mock.Match{Action: "on_forward", Stream: stream}, forwardTo(urls...))
	}

	// OneUrl answers one URL with another app, stream and a token: SRS asks
	// the backend once, with the publish's fields, and the destination gets
	// the stream at the URL's app and stream, with the token in its param.
	check.Case(t, "OneUrl", check.Timer, func(c *check.C) {
		stream := streamName(c)
		to := rtmpURL(d1, "live2/"+stream+"-x?token=t1")
		forwards(stream, to)
		pubID, _ := publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, []string{to})
		playForwarded(c, d1, "live2/"+stream+"-x")

		asks := srs.Hooks.Requests(mock.Match{Action: "on_forward", Stream: stream})
		if c.CheckEqual(len(asks), 1, "on_forward requests") {
			h := asks[0]
			checkFields(c, h.JSON, []string{"action", "server_id", "service_id", "client_id", "ip", "vhost", "app", "tcUrl", "stream", "param"}, "on_forward")
			c.CheckEqual(h.Method, "POST", "on_forward method")
			c.CheckEqual(h.Field("client_id"), pubID, "on_forward client_id")
			c.CheckEqual(h.Field("app"), "live", "on_forward app")
			c.CheckEqual(h.Field("tcUrl"), rtmpURL(srs, "live"), "on_forward tcUrl")
			c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "on_forward vhost")
			c.CheckEqual(h.Field("ip"), "127.0.0.1", "on_forward ip")
		}
		h := waitHook(c, d1, "on_publish", stream+"-x")
		c.CheckEqual(h.Field("app"), "live2", "the destination's app")
		c.Check(strings.Contains(h.Field("param"), "token=t1"), "the destination's param %q, want token=t1", h.Field("param"))
	})

	// TwoUrls answers a URL on each destination: both get the stream.
	check.Case(t, "TwoUrls", check.Timer, func(c *check.C) {
		stream := streamName(c)
		urls := []string{rtmpURL(d1, "live/"+stream+"-a"), rtmpURL(d2, "live/"+stream+"-b")}
		forwards(stream, urls...)
		publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, urls)
		playForwarded(c, d1, "live/"+stream+"-a")
		playForwarded(c, d2, "live/"+stream+"-b")
	})

	// NoUrls answers no URL: the stream plays on the origin, has no forward,
	// and the configured destination gets nothing either.
	check.Case(t, "NoUrls", check.Normal, func(c *check.C) {
		stream := streamName(c)
		_, _, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		playForwarded(c, srs, "live/"+stream)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_forward", Stream: stream})), 1, "on_forward requests")
		c.Check(len(forwardsOf(c, srs, streamID)) == 0, "forwards of a stream the backend forwards nowhere, want none")
		checkNoForward(c, d2, stream, "the destination beside a backend")
	})

	// OnlyBackendUsed answers a URL on the first destination: the stream goes
	// there only, never to the destination the origin configures.
	check.Case(t, "OnlyBackendUsed", check.Timer, func(c *check.C) {
		stream := streamName(c)
		to := rtmpURL(d1, "live/"+stream)
		forwards(stream, to)
		publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, []string{to})
		playForwarded(c, d1, "live/"+stream)
		checkNoForward(c, d2, stream, "the destination beside a backend")
	})

	// The backend's errors each refuse the publish: SRS closes the publisher,
	// the stream is not published and nothing is forwarded.
	for _, tc := range []struct {
		name  string
		reply mock.Reply
	}{
		{"HttpErrorRejects", mock.HTTPError(500)},
		{"CodeErrorRejects", mock.Reject(100)},
		{"NoUrlsFieldRejects", mock.JSON(map[string]any{"code": 0})},
	} {
		check.Case(t, tc.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			srs.Hooks.Script(mock.Match{Action: "on_forward", Stream: stream}, tc.reply)
			r := rtmpHookTarget("rtmp", "on_forward", false, true).try(c, srs, stream, hookWait())
			checkRefused(c, r, "closed")
			waitHook(c, srs, "on_forward", stream)
			published, _ := streamState(c, srs, "live/"+stream)
			c.Check(!published, "the stream is published after the backend's error")
			checkNoForward(c, d2, stream, "a refused publish")
		})
	}

	// Status lists the backend's forward with its URL, forwarding, with the
	// same id by its own path and while it forwards.
	check.Case(t, "Status", check.Timer, func(c *check.C) {
		stream := streamName(c)
		to := rtmpURL(d1, "live/"+stream+"-s?token=t2")
		forwards(stream, to)
		_, streamID := publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, []string{to})
		fwds := forwardsOf(c, srs, streamID)
		id, _ := fwds[0]["id"].(string)
		byID := object(c, apiObject(c, srs, "/api/v1/forwards/"+id), "forward")
		c.CheckEqual(byID["url"], fwds[0]["url"], "the forward's url by its id")
		playForwarded(c, d1, "live/"+stream+"-s")
		c.CheckEqual(ids(forwardsOf(c, srs, streamID)), ids(fwds), "the forward ids while it forwards")
	})

	// KickRemovesForward forwards to both destinations, then the backend
	// stops answering the second and kicks the publisher: after the
	// republish only the first is forwarded, and the second gets nothing.
	check.Case(t, "KickRemovesForward", check.Timer, func(c *check.C) {
		forwardByKick(c, srs, d1, d2, true)
	})

	// KickAddsForward forwards to the first destination, then the backend
	// also answers the second and kicks the publisher: after the republish
	// both are forwarded and play.
	check.Case(t, "KickAddsForward", check.Timer, func(c *check.C) {
		forwardByKick(c, srs, d1, d2, false)
	})
}

// forwardByKick changes a stream's forwards the documented way: the backend
// changes its answer and kicks the publisher, which publishes again. With
// remove, the backend forwards to both destinations, then to the first only;
// otherwise to the first, then to both.
func forwardByKick(c *check.C, srs, d1, d2 *server.Server, remove bool) {
	c.Helper()
	stream := streamName(c)
	a, b := rtmpURL(d1, "live/"+stream+"-a"), rtmpURL(d2, "live/"+stream+"-b")
	before, after := []string{a, b}, []string{a}
	if !remove {
		before, after = after, before
	}
	m := mock.Match{Action: "on_forward", Stream: stream}
	srs.Hooks.Script(m, forwardTo(before...))
	pubID, _ := publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, before)

	srs.Hooks.Script(m, forwardTo(after...))
	checkKicked(c, apiKick(c, srs, pubID))
	waitHook(c, srs, "on_unpublish", stream)
	waitHook(c, d1, "on_unpublish", stream+"-a")
	if remove {
		waitHook(c, d2, "on_unpublish", stream+"-b")
	}

	publishForwarded(c, srs, "live/"+stream, media.H264HighAAC, after)
	playForwarded(c, d1, "live/"+stream+"-a")
	if remove {
		time.Sleep(forwardNothing)
		got := d2.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream + "-b"})
		c.CheckEqual(len(got), 1, "publishes of the removed forward's stream, the one before the kick")
	} else {
		playForwarded(c, d2, "live/"+stream+"-b")
	}
}
