// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"net/url"
	"os"
	"slices"
	"strconv"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// hookScheme is RTMP or RTMPS for the hook payload cases: the same cases run
// over both, since RTMPS is RTMP inside TLS on a listener of its own.
type hookScheme struct {
	name string
	// url is the URL of a path on the scheme's listener.
	url func(srs *server.Server, path string) string
	// opts is a new client options for the scheme.
	opts func(c *check.C) *rtmp.Options
}

var hookSchemes = []hookScheme{
	{"Rtmp", rtmpURL, func(*check.C) *rtmp.Options { return &rtmp.Options{} }},
	{"Rtmps", rtmpsURL, tlsOpts},
}

// rtmpHookActions are the hooks of an RTMP client's session.
var rtmpHookActions = []string{"on_connect", "on_close", "on_publish", "on_unpublish", "on_play", "on_stop"}

// rtmpHookFields are the fields each hook of an RTMP session carries.
var rtmpHookFields = map[string][]string{
	"on_connect":   {"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "stream", "param", "tcUrl", "pageUrl"},
	"on_close":     {"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "send_bytes", "recv_bytes"},
	"on_publish":   {"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "tcUrl", "stream", "param", "stream_url", "stream_id"},
	"on_unpublish": {"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "tcUrl", "stream", "param", "stream_url", "stream_id"},
	"on_play":      {"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "tcUrl", "stream", "param", "pageUrl", "stream_url", "stream_id", "clients"},
	"on_stop":      {"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "tcUrl", "stream", "param", "stream_url", "stream_id", "clients"},
}

// hooksRtmp adds the hook payload cases of RTMP and RTMPS clients to
// TestHooks_Common: every field of on_connect, on_close, on_publish,
// on_unpublish, on_play and on_stop, the params from the tcUrl and the stream
// name, the client IP, and the order of the events of a session. A case finds
// its client's hooks by the client_id of its first hook, since on_close has no
// stream.
func hooksRtmp(t *testing.T) {
	for _, s := range hookSchemes {
		// PublisherEvents publishes 1s of media at once with a param and a
		// pageUrl, then closes: on_connect, on_publish, on_unpublish and
		// on_close, each once and in that order, with every field; the ids are
		// those of the API, and on_close counts each byte of the connection.
		check.Case(t, s.name+"PublisherEvents", check.Normal, func(c *check.C) {
			publisherEventsCase(c, s, rtmp.FMLE)
		})

		// PublisherEventsFlash is PublisherEvents with the Flash-style publish
		// commands: the same events and fields.
		check.Case(t, s.name+"PublisherEventsFlash", check.Normal, func(c *check.C) {
			publisherEventsCase(c, s, rtmp.Flash)
		})

		// PlayerEvents plays a stream with a param and a pageUrl, then closes
		// while the stream goes on: on_connect, on_play, on_stop and on_close,
		// each once and in that order, with every field. The player counts in
		// the clients of on_play and still in those of on_stop.
		check.Case(t, s.name+"PlayerEvents", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			pubOpts := s.opts(c)
			startPublisherWith(c, s.url(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 500*time.Millisecond, pubOpts, rtmp.FMLE)
			pub := waitHook(c, srs, "on_publish", stream)

			opts := s.opts(c)
			opts.Connect = map[string]any{"pageUrl": "http://player.example/page"}
			player, _ := playRTMPClient(c, s.url(srs, "live/"+stream+"?k=v"), opts)
			play := waitHook(c, srs, "on_play", stream)
			id := play.Field("client_id")
			checkAPIClient(c, srs, id, stream)
			before := closeCounted(player)
			closed := waitClientHook(c, srs, "on_close", id)

			hooks := clientHooks(srs, id)
			c.CheckEqual(hookActions(hooks), []string{"on_connect", "on_play", "on_stop", "on_close"}, "player hooks")
			checkCommonFields(c, srs, hooks)
			tcURL := player.URL.TcURL
			for _, h := range hooks {
				if h.Action() == "on_close" {
					continue
				}
				c.CheckEqual(h.Field("stream"), stream, h.Action()+" stream")
				c.CheckEqual(h.Field("param"), "?k=v", h.Action()+" param")
				c.CheckEqual(h.Field("tcUrl"), tcURL, h.Action()+" tcUrl")
			}
			for _, h := range hookOf(hooks, "on_play", "on_stop") {
				c.CheckEqual(h.Field("stream_url"), "/live/"+stream, h.Action()+" stream_url")
				c.CheckEqual(h.Field("stream_id"), pub.Field("stream_id"), h.Action()+" stream_id, the publisher's")
				c.CheckEqual(h.Field("clients"), "2", h.Action()+" clients: the publisher and the player")
			}
			for _, h := range hookOf(hooks, "on_connect", "on_play") {
				c.CheckEqual(h.Field("pageUrl"), "http://player.example/page", h.Action()+" pageUrl")
			}
			checkCloseBytes(c, closed, player, before)
		})

		// PlayClients plays a stream with two players, which close one after
		// the other: on_play counts the publisher and the players so far, and
		// on_stop the clients before the player leaves, as the API counts
		// them.
		check.Case(t, s.name+"PlayClients", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			url := s.url(srs, "live/"+stream)
			startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 500*time.Millisecond, s.opts(c), rtmp.FMLE)
			first, _ := playRTMPClient(c, url, s.opts(c))
			firstID := waitHook(c, srs, "on_play", stream).Field("client_id")
			second, _ := playRTMPClient(c, url, s.opts(c))
			secondID := waitHookCountOf(c, srs, mock.Match{Action: "on_play", Stream: stream}, 2)[1].Field("client_id")

			first.Close()
			firstStop := waitClientHook(c, srs, "on_stop", firstID)
			// SRS drops a client from its stats after its on_close, so the
			// second player closes once the API counts the first one gone.
			path := "/api/v1/streams/" + firstStop.Field("stream_id")
			deadline := time.Now().Add(time.Second)
			for streamClients(c, srs, path) != 2 && time.Now().Before(deadline) {
				time.Sleep(5 * time.Millisecond)
			}
			second.Close()
			secondStop := waitClientHook(c, srs, "on_stop", secondID)
			c.CheckEqual(waitClientHook(c, srs, "on_play", firstID).Field("clients"), "2", "first on_play clients")
			c.CheckEqual(waitClientHook(c, srs, "on_play", secondID).Field("clients"), "3", "second on_play clients")
			c.CheckEqual(firstStop.Field("clients"), "3", "first on_stop clients")
			c.CheckEqual(secondStop.Field("clients"), "2", "second on_stop clients")
		})

		// PlayBeforePublish plays a stream before it is published: on_play
		// counts the player alone, and on_publish then names the stream id
		// the player's on_play named.
		check.Case(t, s.name+"PlayBeforePublish", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			url := s.url(srs, "live/"+stream)
			player := dialRTMP(c, url, s.opts(c))
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			_, err := player.Play(ctx)
			c.Must(err, "play")
			play := waitHook(c, srs, "on_play", stream)
			c.CheckEqual(play.Field("clients"), "1", "on_play clients: the player alone")

			startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, s.opts(c), rtmp.FMLE)
			pub := waitHook(c, srs, "on_publish", stream)
			c.Check(play.Field("stream_id") != "", "no stream_id in on_play: %s", play.Body)
			c.CheckEqual(pub.Field("stream_id"), play.Field("stream_id"), "on_publish stream_id, the player's")
			c.Check(pub.Time.After(play.Time), "on_publish at %v, before on_play at %v", pub.Time, play.Time)
		})

		// ParamFromTcUrl puts the param on the tcUrl, as some encoders do:
		// every hook of the publisher and the player has it as its param, and
		// the tcUrl as sent.
		check.Case(t, s.name+"ParamFromTcUrl", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			url := s.url(srs, "live/"+stream)
			tcURL := s.url(srs, "live?token=abc")
			pubOpts := s.opts(c)
			pubOpts.TcURL = tcURL
			startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 500*time.Millisecond, pubOpts, rtmp.FMLE)
			pubID := waitHook(c, srs, "on_publish", stream).Field("client_id")

			opts := s.opts(c)
			opts.TcURL = tcURL
			playRTMPClient(c, url, opts)
			playID := waitHook(c, srs, "on_play", stream).Field("client_id")
			for _, id := range []string{pubID, playID} {
				for _, h := range hookOf(clientHooks(srs, id), "on_connect", "on_publish", "on_play") {
					c.CheckEqual(h.Field("stream"), stream, h.Action()+" stream")
					c.CheckEqual(h.Field("param"), "?token=abc", h.Action()+" param")
					c.CheckEqual(h.Field("tcUrl"), tcURL, h.Action()+" tcUrl")
				}
			}
		})

		// ParamFromTcUrlAndStream puts one param on the tcUrl and another on
		// the stream name: the hooks' param must hold both, as a query a hook
		// server can parse.
		check.Case(t, s.name+"ParamFromTcUrlAndStream", check.Normal, func(c *check.C) {
			check.KnownBug(c, "a param on both the tcUrl and the stream name is joined with a second '?', so the tcUrl's key is lost in the stream's value")
			srs := server.Common(c)
			stream := streamName(c)
			opts := s.opts(c)
			opts.TcURL = s.url(srs, "live?k1=v1")
			startPublisherWith(c, s.url(srs, "live/"+stream+"?k2=v2"), media.Path(c, media.H264HighAAC), 0, opts, rtmp.FMLE)
			h := waitHook(c, srs, "on_publish", stream)
			q, err := url.ParseQuery(trimQuery(h.Field("param")))
			c.Must(err, "parse the param "+h.Field("param"))
			c.CheckEqual(q, url.Values{"k1": {"v1"}, "k2": {"v2"}}, "the keys of the param "+h.Field("param"))
		})

		// UnpublishBeforeClose unpublishes, then closes a moment later:
		// on_unpublish comes at the unpublish, on_close only at the close.
		check.Case(t, s.name+"UnpublishBeforeClose", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			pub := publishFor(c, s.url(srs, "live/"+stream), s.opts(c), rtmp.FMLE, 500*time.Millisecond)
			id := waitHook(c, srs, "on_publish", stream).Field("client_id")
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			_, err := pub.Unpublish(ctx)
			c.Must(err, "unpublish")
			waitClientHook(c, srs, "on_unpublish", id)
			time.Sleep(50 * time.Millisecond)
			c.CheckEqual(hookActions(clientHooks(srs, id)), []string{"on_connect", "on_publish", "on_unpublish"}, "hooks before the close")

			pub.Close()
			waitClientHook(c, srs, "on_close", id)
			c.CheckEqual(hookActions(clientHooks(srs, id)), []string{"on_connect", "on_publish", "on_unpublish", "on_close"}, "hooks after the close")
		})

		// UnpublishKeepsPlayer unpublishes a stream with a player: the
		// player, which stays connected for a republish, gets no on_stop, nor
		// a new on_play at the republish, until it closes.
		check.Case(t, s.name+"UnpublishKeepsPlayer", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			url := s.url(srs, "live/"+stream)
			pub := startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 500*time.Millisecond, s.opts(c), rtmp.FMLE)
			player, _ := playRTMPClient(c, url, s.opts(c))
			id := waitHook(c, srs, "on_play", stream).Field("client_id")
			pub.Close()
			waitHook(c, srs, "on_unpublish", stream)
			time.Sleep(50 * time.Millisecond)
			c.CheckEqual(hookActions(clientHooks(srs, id)), []string{"on_connect", "on_play"}, "player hooks after the unpublish")

			// The republish, over RTMP for both schemes, sends media in real
			// time, which also makes SRS see the player's close at once: with
			// no media, it sees it only at its next check, up to 350ms later.
			streamFile(c, republish(c, rtmpURL(srs, "live/"+stream), 300*time.Millisecond), media.Path(c, media.H264HighAAC), 0)
			waitHookCount(c, srs, mock.Match{Action: "on_publish", Stream: stream}, 2)
			player.Close()
			waitClientHook(c, srs, "on_close", id)
			c.CheckEqual(hookActions(clientHooks(srs, id)), []string{"on_connect", "on_play", "on_stop", "on_close"}, "player hooks after its close")
		})

		// ConnectOnlyClose connects and closes without publishing or playing.
		// A hook server must see an on_close only for a client it saw the
		// on_connect of, both with the same vhost, or neither.
		check.Case(t, s.name+"ConnectOnlyClose", check.Normal, func(c *check.C) {
			check.KnownBug(c, "a client that only connects gets on_close, with the tcUrl's host as its vhost, and no on_connect")
			srs := server.Common(c)
			// on_close names no stream, so the client connects to an app of
			// its own, which every hook names.
			app := "app-" + streamName(c)
			cl := dialRTMP(c, s.url(srs, app+"/stream"), s.opts(c))
			cl.Close()
			ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
			defer cancel()
			closed, err := srs.Hooks.Wait(ctx, mock.Match{Action: "on_close", Func: func(r mock.Request) bool { return r.Field("app") == app }})
			if err != nil {
				return
			}
			hooks := clientHooks(srs, closed.Field("client_id"))
			c.CheckEqual(hookActions(hooks), []string{"on_connect", "on_close"}, "hooks of a client that only connects")
			c.CheckEqual(closed.Field("vhost"), "__defaultVhost__", "on_close vhost")
		})
	}

	// RtmpClientIpv6 publishes over IPv6: every hook of the session has the
	// client's IPv6 address. RTMPS listens on IPv4 only.
	check.Case(t, "RtmpClientIpv6", check.Normal, func(c *check.C) {
		if !server.HasIPv6() {
			c.Skip("the host has no IPv6 loopback")
		}
		srs := server.Common(c)
		stream := streamName(c)
		pub := publishFor(c, "rtmp://[::1]:"+port(srs)+"/live/"+stream, nil, rtmp.FMLE, 500*time.Millisecond)
		id := waitHook(c, srs, "on_publish", stream).Field("client_id")
		pub.Close()
		waitClientHook(c, srs, "on_close", id)
		hooks := clientHooks(srs, id)
		c.CheckEqual(hookActions(hooks), []string{"on_connect", "on_publish", "on_unpublish", "on_close"}, "hooks")
		for _, h := range hooks {
			c.CheckEqual(h.Field("ip"), "::1", h.Action()+" ip")
		}
	})
}

// publisherEventsCase publishes 1s of media at once in a publish style, then
// closes, and checks every hook of the session.
func publisherEventsCase(c *check.C, s hookScheme, mode rtmp.PublishMode) {
	srs := server.Common(c)
	stream := streamName(c)
	opts := s.opts(c)
	opts.Connect = map[string]any{"pageUrl": "http://publisher.example/page"}
	pub := publishFor(c, s.url(srs, "live/"+stream+"?token=abc"), opts, mode, time.Second)
	id := waitHook(c, srs, "on_publish", stream).Field("client_id")
	checkAPIClient(c, srs, id, stream)
	before := closeCounted(pub)
	closed := waitClientHook(c, srs, "on_close", id)

	hooks := clientHooks(srs, id)
	c.CheckEqual(hookActions(hooks), []string{"on_connect", "on_publish", "on_unpublish", "on_close"}, "publisher hooks")
	checkCommonFields(c, srs, hooks)
	for _, h := range hooks {
		if h.Action() == "on_close" {
			continue
		}
		c.CheckEqual(h.Field("stream"), stream, h.Action()+" stream")
		c.CheckEqual(h.Field("param"), "?token=abc", h.Action()+" param")
		c.CheckEqual(h.Field("tcUrl"), pub.URL.TcURL, h.Action()+" tcUrl")
	}
	c.CheckEqual(waitClientHook(c, srs, "on_connect", id).Field("pageUrl"), "http://publisher.example/page", "on_connect pageUrl")
	published := waitClientHook(c, srs, "on_publish", id)
	for _, h := range hookOf(hooks, "on_publish", "on_unpublish") {
		c.CheckEqual(h.Field("stream_url"), "/live/"+stream, h.Action()+" stream_url")
		c.CheckEqual(h.Field("stream_id"), published.Field("stream_id"), h.Action()+" stream_id")
	}
	checkCloseBytes(c, closed, pub, before)
}

// publishFor publishes d of a test file at once, and returns the publisher,
// which sends nothing more; it is closed when the case ends.
func publishFor(c *check.C, url string, opts *rtmp.Options, mode rtmp.PublishMode, d time.Duration) *rtmp.Client {
	c.Helper()
	pub := dialRTMP(c, url, opts)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := pub.Publish(ctx, mode)
	c.Must(err, "publish")
	f, err := os.Open(media.Path(c, media.H264HighAAC))
	c.Must(err, "open the media")
	defer f.Close()
	r, err := media.NewFLVReader(f)
	c.Must(err, "read the media")
	for {
		tag, err := r.ReadTag()
		c.Must(err, "read the media")
		if time.Duration(tag.Timestamp)*time.Millisecond >= d {
			return pub
		}
		c.Must(pub.WriteTag(tag), "publish")
	}
}

// clientHooks are the session hooks of a client, in the order they came.
func clientHooks(srs *server.Server, id string) []mock.Request {
	return srs.Hooks.Requests(mock.Match{Func: func(r mock.Request) bool {
		return r.Field("client_id") == id && slices.Contains(rtmpHookActions, r.Action())
	}})
}

// waitClientHook waits for a hook of a client.
func waitClientHook(c *check.C, srs *server.Server, action, id string) mock.Request {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	h, err := srs.Hooks.Wait(ctx, mock.Match{Action: action, Func: func(r mock.Request) bool { return r.Field("client_id") == id }})
	c.Must(err, action+" of client "+id)
	return h
}

// waitHookCountOf waits until n requests match, and returns them.
func waitHookCountOf(c *check.C, srs *server.Server, m mock.Match, n int) []mock.Request {
	c.Helper()
	waitHookCount(c, srs, m, n)
	return srs.Hooks.Requests(m)
}

// hookActions is the action of each hook.
func hookActions(hooks []mock.Request) []string {
	var actions []string
	for _, h := range hooks {
		actions = append(actions, h.Action())
	}
	return actions
}

// hookOf is the hooks of the actions.
func hookOf(hooks []mock.Request, actions ...string) []mock.Request {
	var out []mock.Request
	for _, h := range hooks {
		if slices.Contains(actions, h.Action()) {
			out = append(out, h)
		}
	}
	return out
}

// checkCommonFields checks every hook has its fields, and the fields every
// hook of an RTMP session shares: the server's ids, the client's id and IP,
// the default vhost and the app.
func checkCommonFields(c *check.C, srs *server.Server, hooks []mock.Request) {
	c.Helper()
	checkHookFields(c, srs, hooks, "127.0.0.1")
}

// checkAPIClient checks the API lists a client by the id of its hooks, as a
// client of the stream.
func checkAPIClient(c *check.C, srs *server.Server, id, stream string) {
	c.Helper()
	var v struct {
		Code   int `json:"code"`
		Client struct {
			ID   string `json:"id"`
			Name string `json:"name"`
		} `json:"client"`
	}
	apiGet(c, srs, "/api/v1/clients/"+id, &v)
	c.CheckEqual(v.Code, 0, "the API code of client "+id)
	c.CheckEqual(v.Client.ID, id, "the API client id")
	c.CheckEqual(v.Client.Name, stream, "the API client's stream")
}

// closeCounted closes a client, and returns how many bytes it had sent
// before: the close of an rtmps:// client sends a TLS alert, which the server
// may close the connection before it reads.
func closeCounted(cl *rtmp.Client) uint64 {
	before := cl.BytesSent()
	cl.Close()
	return before
}

// checkCloseBytes checks the bytes of on_close of a client closed with
// closeCounted: the server got every byte the client sent before its close,
// and sent at least every byte the client read.
func checkCloseBytes(c *check.C, closed mock.Request, cl *rtmp.Client, before uint64) {
	c.Helper()
	recv, err := strconv.ParseUint(closed.Field("recv_bytes"), 10, 64)
	c.CheckNoError(err, "on_close recv_bytes")
	c.Check(recv >= before && recv <= cl.BytesSent(), "on_close recv_bytes %d, the client sent %d before its close and %d with it", recv, before, cl.BytesSent())
	sent, err := strconv.ParseUint(closed.Field("send_bytes"), 10, 64)
	c.CheckNoError(err, "on_close send_bytes")
	c.Check(sent >= cl.BytesReceived(), "on_close send_bytes %d, the client read %d", sent, cl.BytesReceived())
}

// trimQuery is a param without its leading '?'.
func trimQuery(param string) string {
	if len(param) > 0 && param[0] == '?' {
		return param[1:]
	}
	return param
}
