// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// hooksProtocols adds the hook payload cases of WebRTC, SRT, RTSP and
// GB28181 clients to TestHooks_Common: which hooks each publish and play
// gets, in order, with every field. A WebRTC or RTSP client has no
// on_connect or on_close, as an HTTP player has none; an SRT client has them,
// like an RTMP client; a GB28181 stream's hooks are those of the RTMP client
// SRS publishes it with.
func hooksProtocols(t *testing.T) {
	hooksRtc(t)
	hooksSrt(t)
	hooksRtsp(t)
	hooksGb(t)
}

// hooksRtc adds the hook cases of WHIP, WHEP and the legacy WebRTC API.
func hooksRtc(t *testing.T) {
	// WhipPublisherEvents publishes by WHIP with a param, then deletes the
	// session: on_publish, then on_unpublish, and no other hook of the
	// client, with every field; the client_id is a client of the API, the
	// type rtc-publish, the tcUrl the webrtc:// URL of the app, as a legacy
	// client's, the param the API URL's query with a leading ?, and both
	// name the stream_id.
	check.Case(t, "WhipPublisherEvents", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream)+"&token=abc", nil)
		published := waitHook(c, srs, "on_publish", stream)
		id := published.Field("client_id")
		checkAPIClient(c, srs, id, stream)
		rtcDelete(c, &pub.Session)
		waitClientHook(c, srs, "on_unpublish", id)

		hooks := clientHooks(srs, id)
		c.CheckEqual(hookActions(hooks), []string{"on_publish", "on_unpublish"}, "publisher hooks")
		checkHookFields(c, srs, hooks, "127.0.0.1", "rtc-publish")
		for _, h := range hooks {
			c.CheckEqual(h.Field("stream"), stream, h.Action()+" stream")
			c.CheckEqual(h.Field("tcUrl"), "webrtc://127.0.0.1/live", h.Action()+" tcUrl")
			checkParams(c, h.Field("param"), "token=abc")
			c.CheckEqual(h.Field("stream_url"), "/live/"+stream, h.Action()+" stream_url")
			c.CheckEqual(h.Field("stream_id"), published.Field("stream_id"), h.Action()+" stream_id, the publisher's")
		}
	})

	// WhepPlayerEvents plays by WHEP with a param, then closes the peer
	// connection without a DELETE: on_play, then on_stop, and no other hook
	// of the client, with every field; the type is rtc-play, the tcUrl and
	// param as WhipPublisherEvents', stream_id the publisher's.
	check.Case(t, "WhepPlayerEvents", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		published := waitHook(c, srs, "on_publish", stream)
		player := rtcPlayer(c, whepURL(srs, "live", stream)+"&k=v", nil)
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkAPIClient(c, srs, id, stream)
		player.Close()
		waitClientHook(c, srs, "on_stop", id)

		hooks := clientHooks(srs, id)
		c.CheckEqual(hookActions(hooks), []string{"on_play", "on_stop"}, "player hooks")
		checkHookFields(c, srs, hooks, "127.0.0.1", "rtc-play")
		for _, h := range hooks {
			c.CheckEqual(h.Field("stream"), stream, h.Action()+" stream")
			c.CheckEqual(h.Field("tcUrl"), "webrtc://127.0.0.1/live", h.Action()+" tcUrl")
			checkParams(c, h.Field("param"), "k=v")
			c.CheckEqual(h.Field("stream_url"), "/live/"+stream, h.Action()+" stream_url")
			c.CheckEqual(h.Field("stream_id"), published.Field("stream_id"), h.Action()+" stream_id, the publisher's")
		}
	})

	// WhepOnPlayClients plays a published stream by WHEP: like an RTMP, HTTP
	// or SRT player, the player counts in the clients of its on_play.
	check.Case(t, "WhepOnPlayClients", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
		waitHook(c, srs, "on_publish", stream)
		rtcPlayer(c, whepURL(srs, "live", stream), nil)
		c.CheckEqual(waitHook(c, srs, "on_play", stream).Field("clients"), "2", "on_play clients: the publisher and the player")
	})

	// WhipForwardedFor publishes and plays through a proxy that sets
	// X-Forwarded-For: its first address is the client's IP in every hook
	// and in the API.
	check.Case(t, "WhipForwardedFor", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{Header: forwardedFor})
		player := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Header: http.Header{"X-Forwarded-For": {"10.4.5.6, 10.0.0.1"}}})
		pubID := waitHook(c, srs, "on_publish", stream).Field("client_id")
		playID := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkClientIP(c, srs, pubID, "10.1.2.3")
		checkClientIP(c, srs, playID, "10.4.5.6")
		player.Close()
		waitClientHook(c, srs, "on_stop", playID)
		rtcDelete(c, &pub.Session)
		waitClientHook(c, srs, "on_unpublish", pubID)
		for id, ip := range map[string]string{pubID: "10.1.2.3", playID: "10.4.5.6"} {
			for _, h := range clientHooks(srs, id) {
				c.CheckEqual(h.Field("ip"), ip, h.Action()+" ip")
			}
		}
	})

	// LegacyEvents publishes and plays by the legacy API, each with a param
	// on its stream URL, then closes both: on_publish and on_unpublish, and
	// on_play and on_stop, with every field; the tcUrl is the stream URL's,
	// the types rtc-publish and rtc-play, as by WHIP and WHEP.
	check.Case(t, "LegacyEvents", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		u := "webrtc://127.0.0.1/live/" + stream
		pub := rtcPublisher(c, srs.APIURL("/rtc/v1/publish/"), &rtc.Options{Legacy: u + "?token=abc"})
		published := waitHook(c, srs, "on_publish", stream)
		player := rtcPlayer(c, srs.APIURL("/rtc/v1/play/"), &rtc.Options{Legacy: u + "?k=v"})
		playID := waitHook(c, srs, "on_play", stream).Field("client_id")
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)
		player.Close()
		waitClientHook(c, srs, "on_stop", playID)
		pub.Close()
		waitClientHook(c, srs, "on_unpublish", published.Field("client_id"))

		for _, s := range []struct {
			id, param, typ string
			actions        []string
		}{
			{published.Field("client_id"), "token=abc", "rtc-publish", []string{"on_publish", "on_unpublish"}},
			{playID, "k=v", "rtc-play", []string{"on_play", "on_stop"}},
		} {
			hooks := clientHooks(srs, s.id)
			c.CheckEqual(hookActions(hooks), s.actions, "hooks of client "+s.id)
			checkHookFields(c, srs, hooks, "127.0.0.1", s.typ)
			for _, h := range hooks {
				c.CheckEqual(h.Field("stream"), stream, h.Action()+" stream")
				c.CheckEqual(h.Field("tcUrl"), "webrtc://127.0.0.1/live", h.Action()+" tcUrl")
				checkParams(c, h.Field("param"), s.param)
				c.CheckEqual(h.Field("stream_url"), "/live/"+stream, h.Action()+" stream_url")
				c.CheckEqual(h.Field("stream_id"), published.Field("stream_id"), h.Action()+" stream_id, the publisher's")
			}
		}
	})

	// WhipUnpublishQueued deletes a WHIP publisher while SRS still waits on
	// another stream's slow on_unpublish, so its on_unpublish waits in SRS's
	// queue of WebRTC hooks until SRS has dropped the client and the stream
	// from its stats: the hook still names the stream_id of its on_publish,
	// and the type rtc-publish.
	check.Case(t, "WhipUnpublishQueued", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		blocker := newRtcHookBlocker(c, srs, stream+"-block")
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		published := waitHook(c, srs, "on_publish", stream)
		id := published.Field("client_id")
		blocker.hold(c, srs)
		rtcDelete(c, &pub.Session)
		c.Must(untilGone(c, srs, "/api/v1/clients/"+id), "the publisher in the API")
		blocker.release(c, srs, id)

		h := waitClientHook(c, srs, "on_unpublish", id)
		c.CheckEqual(h.Field("stream_id"), published.Field("stream_id"), "on_unpublish stream_id, the on_publish's")
		c.CheckEqual(h.Field("type"), "rtc-publish", "on_unpublish type")
	})

	// WhepStopQueued deletes a WHEP player, then its publisher, while SRS
	// still waits on another stream's slow on_unpublish, so their hooks wait
	// in SRS's queue of WebRTC hooks until SRS has dropped both clients and
	// the stream from its stats: on_stop still names the stream_id and counts
	// the clients when the player stopped, the publisher and the player, as
	// an RTMP player's on_stop does; on_unpublish names the stream_id. Each
	// has its client's type, rtc-play and rtc-publish.
	check.Case(t, "WhepStopQueued", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		blocker := newRtcHookBlocker(c, srs, stream+"-block")
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		published := waitHook(c, srs, "on_publish", stream)
		pubID := published.Field("client_id")
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		playID := waitHook(c, srs, "on_play", stream).Field("client_id")
		blocker.hold(c, srs)
		rtcDelete(c, &player.Session)
		c.Must(untilGone(c, srs, "/api/v1/clients/"+playID), "the player in the API")
		rtcDelete(c, &pub.Session)
		c.Must(untilGone(c, srs, "/api/v1/clients/"+pubID), "the publisher in the API")
		blocker.release(c, srs, playID, pubID)

		stop := waitClientHook(c, srs, "on_stop", playID)
		c.CheckEqual(stop.Field("stream_id"), published.Field("stream_id"), "on_stop stream_id, the publisher's")
		c.CheckEqual(stop.Field("clients"), "2", "on_stop clients: the publisher and the player")
		c.CheckEqual(stop.Field("type"), "rtc-play", "on_stop type")
		unpublish := waitClientHook(c, srs, "on_unpublish", pubID)
		c.CheckEqual(unpublish.Field("stream_id"), published.Field("stream_id"), "on_unpublish stream_id, the on_publish's")
		c.CheckEqual(unpublish.Field("type"), "rtc-publish", "on_unpublish type")
	})
}

// rtcHookBlocker is a WHIP publisher whose on_unpublish the hook server
// answers only when the case releases it. SRS sends the on_unpublish and
// on_stop of WebRTC clients from one queue, so the hooks queued while it
// waits wait too. A release by the case, not a fixed hold, since the deletes
// and API polls the hold must outlast take longer on a slow host.
type rtcHookBlocker struct {
	pub  *rtc.Publisher
	id   string
	gate chan struct{}
	once sync.Once
}

// newRtcHookBlocker publishes the blocker's stream by WHIP. Its on_unpublish
// is released when the case ends at the latest, so a failed case does not
// hold the queue of a shared server.
func newRtcHookBlocker(c *check.C, srs *server.Server, name string) *rtcHookBlocker {
	c.Helper()
	b := &rtcHookBlocker{gate: make(chan struct{})}
	c.Cleanup(b.answer)
	srs.Hooks.Script(hookOfStream("on_unpublish", name), mock.OK().Until(b.gate))
	b.pub = rtcPublisher(c, whipURL(srs, "live", name), nil)
	b.id = waitHook(c, srs, "on_publish", name).Field("client_id")
	return b
}

// hold deletes the blocker and waits for its on_unpublish, which then holds
// the queue until release.
func (b *rtcHookBlocker) hold(c *check.C, srs *server.Server) {
	c.Helper()
	rtcDelete(c, &b.pub.Session)
	waitClientHook(c, srs, "on_unpublish", b.id)
}

// release fails the case if a hook of the clients came while the queue was
// held, then answers the blocker's on_unpublish.
func (b *rtcHookBlocker) release(c *check.C, srs *server.Server, ids ...string) {
	c.Helper()
	for _, id := range ids {
		queued := srs.Hooks.Requests(mock.Match{Func: func(r mock.Request) bool {
			return r.Field("client_id") == id && (r.Action() == "on_unpublish" || r.Action() == "on_stop")
		}})
		c.CheckEqual(hookActions(queued), []string(nil), "hooks of client "+id+" while the queue is held")
	}
	b.answer()
}

func (b *rtcHookBlocker) answer() {
	b.once.Do(func() { close(b.gate) })
}

// hooksSrt adds the hook cases of SRT publishers and players.
func hooksSrt(t *testing.T) {
	// SrtPublisherEvents publishes with a param in the streamid, then
	// closes: on_connect, on_publish, on_unpublish and on_close, each once
	// and in that order, with every field; the client_id is a client of the
	// API, the type srt-publish, and on_close counts the bytes it got.
	check.Case(t, "SrtPublisherEvents", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish") + ",token=abc"}, media.H264HighAACTS)
		published := waitHook(c, srs, "on_publish", stream)
		id := published.Field("client_id")
		checkAPIClient(c, srs, id, stream)
		// An RTMP player of the converted stream shows SRS has read media.
		playUntilWithin(c, rtmpURL(srs, "live/"+stream), report.Until{VideoHeader: true, AudioHeader: true}, 300*time.Millisecond)
		pub.Close()
		closed := waitClientHook(c, srs, "on_close", id)

		hooks := clientHooks(srs, id)
		c.CheckEqual(hookActions(hooks), []string{"on_connect", "on_publish", "on_unpublish", "on_close"}, "publisher hooks")
		checkHookFields(c, srs, hooks, "127.0.0.1", "srt-publish")
		checkSrtHooks(c, hooks, stream, "token=abc", published.Field("stream_id"))
		recv, err := strconv.ParseUint(closed.Field("recv_bytes"), 10, 64)
		c.CheckNoError(err, "on_close recv_bytes")
		c.Check(recv > 0, "on_close recv_bytes %d, want the bytes of the media", recv)
	})

	// SrtPlayerEvents plays a published stream with a param, then closes
	// while the stream goes on: on_connect, on_play, on_stop and on_close,
	// each once and in that order, with every field, of the type srt-play;
	// the player counts in the clients of on_play and still in those of
	// on_stop, and on_close counts at least the bytes it read.
	check.Case(t, "SrtPlayerEvents", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		published := waitHook(c, srs, "on_publish", stream)
		player := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request") + ",k=v"}, stream)
		read := srtReadSome(c, player)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkAPIClient(c, srs, id, stream)
		player.Close()
		closed := waitClientHook(c, srs, "on_close", id)

		hooks := clientHooks(srs, id)
		c.CheckEqual(hookActions(hooks), []string{"on_connect", "on_play", "on_stop", "on_close"}, "player hooks")
		checkHookFields(c, srs, hooks, "127.0.0.1", "srt-play")
		checkSrtHooks(c, hooks, stream, "k=v", published.Field("stream_id"))
		for _, h := range hookOf(hooks, "on_play", "on_stop") {
			c.CheckEqual(h.Field("clients"), "2", h.Action()+" clients: the publisher and the player")
		}
		sent, err := strconv.ParseUint(closed.Field("send_bytes"), 10, 64)
		c.CheckNoError(err, "on_close send_bytes")
		c.Check(sent >= uint64(read), "on_close send_bytes %d, the player read %d", sent, read)
	})

	// SrtPlayBeforePublish plays a stream before it is published: on_play
	// counts the player alone, and on_publish names the stream id the
	// player's on_play named.
	check.Case(t, "SrtPlayBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		play := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(play.Field("clients"), "1", "on_play clients: the player alone")
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		pub := waitHook(c, srs, "on_publish", stream)
		c.Check(play.Field("stream_id") != "", "no stream_id in on_play: %s", play.Body)
		c.CheckEqual(pub.Field("stream_id"), play.Field("stream_id"), "on_publish stream_id, the player's")
	})
}

// srtReadSome reads one message of a player, for at most 300ms, and returns
// its size. SRS keeps no GOP for SRT, so it is media from where it joins.
func srtReadSome(c *check.C, conn *srt.Conn) int {
	c.Helper()
	done := make(chan int, 1)
	go func() {
		b := make([]byte, 1500)
		n, _ := conn.Read(b)
		done <- n
	}()
	select {
	case n := <-done:
		c.Check(n > 0, "the player read nothing")
		return n
	case <-time.After(300 * time.Millisecond):
		c.Fatalf("the player got nothing in 300ms")
		return 0
	}
}

// checkSrtHooks checks the stream fields of an SRT client's hooks: the
// stream, the param from the streamid, the srt:// tcUrl of the app, and, but
// for on_connect and on_close, the stream URL and id.
func checkSrtHooks(c *check.C, hooks []mock.Request, stream, param, streamID string) {
	c.Helper()
	for _, h := range hooks {
		if h.Action() == "on_close" {
			continue
		}
		c.CheckEqual(h.Field("stream"), stream, h.Action()+" stream")
		checkParams(c, h.Field("param"), param)
		tcURL := h.Field("tcUrl")
		c.Check(strings.HasPrefix(tcURL, "srt://") && strings.HasSuffix(tcURL, "/live"), "%s tcUrl %q, want srt://<host>/live", h.Action(), tcURL)
		if h.Action() == "on_connect" {
			continue
		}
		c.CheckEqual(h.Field("stream_url"), "/live/"+stream, h.Action()+" stream_url")
		c.CheckEqual(h.Field("stream_id"), streamID, h.Action()+" stream_id")
	}
}

// hooksRtsp adds the hook cases of RTSP players.
func hooksRtsp(t *testing.T) {
	// RtspPlayerEvents plays with a param: one on_play at DESCRIBE, with
	// every field, and no on_connect; the client_id is a client of the API,
	// the type rtsp-play, the tcUrl the rtsp:// URL of the app, stream_id the
	// publisher's.
	check.Case(t, "RtspPlayerEvents", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		published := waitHook(c, srs, "on_publish", stream)
		p := rtspPlay(c, srs, "live/"+stream+"?k=v", nil)
		start()
		rtspUntil(c, p, rtspPlayedUntil)
		play := waitHook(c, srs, "on_play", stream)
		id := play.Field("client_id")
		checkAPIClient(c, srs, id, stream)

		hooks := clientHooks(srs, id)
		c.CheckEqual(hookActions(hooks), []string{"on_play"}, "player hooks while it plays")
		checkHookFields(c, srs, hooks, "127.0.0.1", "rtsp-play")
		c.CheckEqual(play.Field("stream"), stream, "on_play stream")
		c.CheckEqual(play.Field("param"), "?k=v", "on_play param")
		c.CheckEqual(play.Field("tcUrl"), rtspURL(srs, "live"), "on_play tcUrl")
		c.CheckEqual(play.Field("stream_url"), "/live/"+stream, "on_play stream_url")
		c.CheckEqual(play.Field("stream_id"), published.Field("stream_id"), "on_play stream_id, the publisher's")
	})

	// RtspPlayerStops plays a stream held at its sequence headers, then
	// tears down and closes: like every other player, an RTSP player gets
	// on_stop when it leaves, so a hook server that counts viewers sees it
	// go; the on_stop has the type rtsp-play, as the on_play.
	check.Case(t, "RtspPlayerStops", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		p := rtspPlay(c, srs, "live/"+stream, nil)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		p.Close()
		ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
		defer cancel()
		stop, err := srs.Hooks.Wait(ctx, mock.Match{Action: "on_stop", Func: func(r mock.Request) bool { return r.Field("client_id") == id }})
		c.CheckNoError(err, "on_stop of the RTSP player")
		if err == nil {
			c.CheckEqual(stop.Field("type"), "rtsp-play", "on_stop type")
		}
	})

	// RtspOnPlayClients describes a published stream over RTSP and doesn't
	// PLAY yet: like an RTMP, HTTP or SRT player, the client counts in the
	// clients of its on_play, and it stays a client of the stream from then
	// until it leaves. After the DESCRIBE the API lists it as an RTSP player
	// and the stream counts it, as the hook did; after the close it is gone.
	check.Case(t, "RtspOnPlayClients", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		waitHook(c, srs, "on_publish", stream)
		conn := rtspDial(c, srs)
		rtspDescribe(c, conn, rtspURL(srs, "live/"+stream))
		h := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(h.Field("clients"), "2", "on_play clients: the publisher and the player")
		c.CheckEqual(apiClientOf(c, srs, h.Field("client_id"))["type"], "rtsp-play", "the described client in the API")
		c.CheckEqual(apiStreamOf(c, srs, h.Field("stream_id"))["clients"], 2.0, "stream clients after the on_play")
		conn.Close()
		c.Must(untilGone(c, srs, "/api/v1/clients/"+h.Field("client_id")), "the client in the API after the close")
	})

	// RtspPlayerCountedOnce plays a published stream over RTSP: the client
	// DESCRIBE added is the player PLAY makes, so the stream counts it once.
	check.Case(t, "RtspPlayerCountedOnce", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		waitHook(c, srs, "on_publish", stream)
		rtspPlay(c, srs, "live/"+stream, nil)
		h := waitHook(c, srs, "on_play", stream)
		c.CheckEqual(apiStreamOf(c, srs, h.Field("stream_id"))["clients"], 2.0, "stream clients while it plays")
	})
}

// hooksGb adds the hook cases of GB28181 streams.
func hooksGb(t *testing.T) {
	// GbPublisherEvents asks for a session, sends media, then closes the
	// media connection: the stream's hooks are those of the RTMP client SRS
	// publishes it with, on_connect, on_publish, on_unpublish and on_close,
	// each once and in that order, with every field; the stream is the
	// session's id in app live, the client_id a client of the API, the type
	// that of the RTMP client, flash-publish.
	check.Case(t, "GbPublisherEvents", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.sendThrough(c, gbLead)
		published := waitHook(c, srs, "on_publish", s.id)
		id := published.Field("client_id")
		checkAPIClient(c, srs, id, s.id)
		s.conn.Close()
		waitClientHook(c, srs, "on_close", id)

		hooks := clientHooks(srs, id)
		c.CheckEqual(hookActions(hooks), []string{"on_connect", "on_publish", "on_unpublish", "on_close"}, "stream hooks")
		checkHookFields(c, srs, hooks, "127.0.0.1", "flash-publish")
		for _, h := range hookOf(hooks, "on_connect", "on_publish", "on_unpublish") {
			c.CheckEqual(h.Field("stream"), s.id, h.Action()+" stream")
		}
		for _, h := range hookOf(hooks, "on_publish", "on_unpublish") {
			c.CheckEqual(h.Field("stream_url"), "/live/"+s.id, h.Action()+" stream_url")
			c.CheckEqual(h.Field("stream_id"), published.Field("stream_id"), h.Action()+" stream_id")
		}
	})

	// GbNoHookBeforeMedia asks for a session and connects its media without
	// sending any: no hook names the stream until media comes.
	check.Case(t, "GbNoHookBeforeMedia", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		time.Sleep(100 * time.Millisecond)
		c.CheckEqual(hookActions(srs.Hooks.Requests(mock.Match{Stream: s.id})), []string(nil), "hooks before media")
		s.sendThrough(c, gbLead)
		waitHook(c, srs, "on_publish", s.id)
	})
}
