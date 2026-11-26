// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"context"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// srtFailIdle is the peer_idle_timeout of the failure server, small so a
// dead link is found soon.
const srtFailIdle = 500 * time.Millisecond

// srtFailConnect is the connect_timeout of the failure server, its least
// useful value; it bounds no caller's handshake (TestSrt_Transport), so a
// caller cut off in the handshake fails at its own timeout, srtFailDial.
const (
	srtFailConnect = 100 * time.Millisecond
	srtFailDial    = 300 * time.Millisecond
)

// srtFailBreak is how long libsrt takes at most to break a connection whose
// peer it no longer hears from: about 5s, 17 expirations of 300ms or more,
// whatever SRTO_PEERIDLETIMEO says.
const srtFailBreak = 8 * time.Second

// srtIdleBug is F5: peer_idle_timeout under about 5s does not close a player
// SRS no longer hears from: SRS's read of a player ignores its timeout, as a
// player sends nothing, and its sends go on into libsrt's buffer, so only
// libsrt's own break, about 5s, ends it. A publisher is closed in time, by
// SRS's read timeout.
const srtIdleBug = "F5 peer_idle_timeout under 5s does not close a dead SRT player"

// TestTimer_Srt_Failure checks what SRS does when a client's network fails,
// for gosrt and libsrt on the failing link, with the relay between it and
// SRS and the other side of the stream on a clean link:
//   - the publisher's network dies: SRS closes it after peer_idle_timeout,
//     sends on_unpublish once, closes its player after the same timeout, and
//     a new publish of the stream plays;
//   - a player's network dies: SRS closes it after peer_idle_timeout (F5),
//     and the publisher and another player go on;
//   - SRS's answers are lost in the handshake: the caller fails at its own
//     timeout, not at connect_timeout, SRS keeps nothing of it, and the next
//     call over the same link publishes;
//   - the client's port changes mid-stream, as a client that changes
//     networks: SRT has no connection migration, and libsrt drops a packet
//     of a connection from another address (queue.cpp, "received from"), so
//     SRS hears no more of the client and closes it as a dead one; a player
//     still gets media sent to its old port until then.
func TestTimer_Srt_Failure(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtImpairConfig(server.SRTOptions{PeerIdleTimeout: srtFailIdle, ConnectTimeout: srtFailConnect}))

	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt} {
		check.Case(t, "PublisherBlackholed_"+cl.Name(), check.Timer, func(c *check.C) {
			run := startSRTImpair(c, srs, cl, srt.GoSRT, "Publisher", srtImpairLatency)
			run.stream()
			time.Sleep(100 * time.Millisecond)
			run.relay.Blackhole(true)
			run.checkPublisherGone(c, srs, time.Now())
		})

		check.Case(t, "PlayerBlackholed_"+cl.Name(), check.Timer, func(c *check.C) {
			run := startSRTImpair(c, srs, cl, srt.GoSRT, "Player", srtImpairLatency)
			first := waitHook(c, srs, "on_play", run.name)
			other := collectSRT(srtPlayerOther(c, srs, run.name, first))
			run.stream()
			time.Sleep(100 * time.Millisecond)
			run.relay.Blackhole(true)
			run.checkPlayerGone(c, srs, first, time.Now())
			run.checkGoesOn(c, srs, other)
		})

		check.Case(t, "HandshakeBlackholed_"+cl.Name(), check.Timer, func(c *check.C) {
			relay, err := srt.NewImpair(srs.Addr(server.SRT), time.Now().UnixNano())
			c.Must(err, "start the relay")
			c.Cleanup(func() { relay.Close() })
			stream := streamName(c)
			opts := srt.Options{StreamID: srtID("live/"+stream, "publish"), Latency: srtImpairCleanLatency, Timeout: srtFailDial}

			relay.Set(srt.Down, srt.Shape{Blackhole: true})
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			conn, err := cl.Dial(ctx, relay.Addr().String(), opts)
			cancel()
			if err == nil {
				conn.Close()
			}
			c.Check(err != nil, "the caller connected with SRS's answers lost")
			c.Check(relay.Counters(srt.Up).Sent > 0 && relay.Counters(srt.Down).Blackholed > 0,
				"SRS got and answered nothing of the handshake: %v up, %v down", relay.Counters(srt.Up), relay.Counters(srt.Down))
			c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the caller cut off")

			relay.Set(srt.Down, srt.Shape{})
			play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
			publishSRTAt(c, cl, relay.Addr().String(), opts, media.H264HighAACTS, srt.MessageSize)
			checkSRTPlayed(c, srtAnalyze(c, play))
		})

		check.Case(t, "PortChange_Publisher_"+cl.Name(), check.Timer, func(c *check.C) {
			run := startSRTImpair(c, srs, cl, srt.GoSRT, "Publisher", srtImpairLatency)
			run.stream()
			time.Sleep(100 * time.Millisecond)
			c.Must(run.relay.Rebind(), "change the port")
			moved := run.relay.Counters(srt.Up).Sent
			run.checkPublisherGone(c, srs, time.Now())
			c.Check(run.relay.Counters(srt.Up).Sent > moved, "the publisher sent nothing from its new port")
		})

		check.Case(t, "PortChange_Player_"+cl.Name(), check.Timer, func(c *check.C) {
			run := startSRTImpair(c, srs, cl, srt.GoSRT, "Player", srtImpairLatency)
			first := waitHook(c, srs, "on_play", run.name)
			run.stream()
			time.Sleep(100 * time.Millisecond)
			c.Must(run.relay.Rebind(), "change the port")
			// The media sent to the old port still reaches the player.
			time.Sleep(100 * time.Millisecond)
			msgs := run.sent.messages()
			last := msgs[len(msgs)-1]
			got := run.got.waitFor(check.Scaled(time.Second), func(got []byte) bool { return bytes.Contains(got, last) })
			c.Check(bytes.Contains(got, last), "the player got nothing after its port changed")
			run.checkPlayerGone(c, srs, first, time.Now())
		})
	}
}

// checkPublisherGone checks that SRS closed the publisher it stopped hearing
// from at dead after peer_idle_timeout, counted from its last media, which
// SRS reads up to the latency later; sent on_unpublish once; closed the
// player once it had no media for that long; and that a new publish of the
// stream plays.
func (r *srtImpairRun) checkPublisherGone(c *check.C, srs *server.Server, dead time.Time) {
	c.Helper()
	unpublished := waitHookWithin(c, srs, "on_unpublish", r.name, 3*srtFailIdle)
	idle := unpublished.Time.Sub(dead)
	c.Check(idle >= srtFailIdle-100*time.Millisecond && idle <= srtFailIdle+r.latency+500*time.Millisecond,
		"the publisher closed %v after its network died, want about %v", idle, srtFailIdle)

	// SRS waits for media for the player up to the timeout once more after
	// the publisher's last, so it closes the player within one timeout of the
	// unpublish.
	stopped := waitHookWithin(c, srs, "on_stop", r.name, 3*srtFailIdle)
	after := stopped.Time.Sub(unpublished.Time)
	c.Check(after >= 0 && after <= srtFailIdle+500*time.Millisecond, "the player closed %v after the unpublish, want within %v", after, srtFailIdle)
	r.got.waitFor(400*time.Millisecond, func([]byte) bool { return false })
	r.got.mu.Lock()
	err := r.got.err
	r.got.mu.Unlock()
	c.Check(err != nil, "the player still open after on_stop")

	c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: r.name})), 1, "on_unpublish hooks")

	// The new player's on_play is the stream's second.
	player := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+r.name, "request")})
	waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: r.name}, 2)
	checkSRTPlayed(c, republishSRT(c, srs, r.name, player))
}

// checkPlayerGone checks that SRS closed the player of first, whose on_play
// it is, that it stopped hearing from at dead, after peer_idle_timeout (F5);
// libsrt's own break ends it at last.
func (r *srtImpairRun) checkPlayerGone(c *check.C, srs *server.Server, first mock.Request, dead time.Time) {
	c.Helper()
	id := first.Field("client_id")
	h := waitSRTHook(c, srs, "on_stop", r.name, srtFailBreak, func(got string) bool { return got == id })
	idle := h.Time.Sub(dead)
	if idle > srtFailIdle+500*time.Millisecond {
		check.KnownBug(c, srtIdleBug)
	}
	c.Check(idle >= srtFailIdle-100*time.Millisecond && idle <= srtFailIdle+500*time.Millisecond,
		"the player closed %v after SRS stopped hearing from it, want about %v", idle, srtFailIdle)
}

// srtPlayerOther connects a second gosrt player of a stream, beside the one
// whose on_play is first, and waits for its own on_play.
func srtPlayerOther(c *check.C, srs *server.Server, stream string, first mock.Request) *srt.Conn {
	c.Helper()
	conn := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request"), Latency: srtImpairCleanLatency})
	firstID := first.Field("client_id")
	waitSRTHook(c, srs, "on_play", stream, time.Second, func(id string) bool { return id != firstID })
	return conn
}

// waitSRTHook waits up to d, scaled, for a hook of a stream whose client id
// passes ok.
func waitSRTHook(c *check.C, srs *server.Server, action, stream string, d time.Duration, ok func(id string) bool) mock.Request {
	c.Helper()
	c.Must(checkHooks(srs), "hooks")
	m := mock.Match{Action: action, Stream: stream, Func: func(r mock.Request) bool { return ok(r.Field("client_id")) }}
	ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(d))
	defer cancel()
	h, err := srs.Hooks.Wait(ctx, m)
	c.Must(err, action)
	return h
}

// checkGoesOn checks that the publisher is still on and that the other
// player gets what it sends now.
func (r *srtImpairRun) checkGoesOn(c *check.C, srs *server.Server, other *srtGot) {
	c.Helper()
	r.checkPublishing(c, srs)
	msgs := r.sent.messages()
	last := msgs[len(msgs)-1]
	got := other.waitFor(check.Scaled(time.Second), func(got []byte) bool { return bytes.Contains(got, last) })
	c.Check(bytes.Contains(got, last), "the other player did not get the media sent now")
}
