// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"fmt"
	"os"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// srtOpusUntil is when an SRT player has seen a stream with Opus play: TS
// carries no Opus sequence header, so it waits for frames instead.
var srtOpusUntil = report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5, AudioFrames: 5}

// srtMedia adds the codec cases to TestSrt_Common: SRS relays the TS of each
// codec to SRT players as it is. H.264 with AAC is PublishPlay's.
func srtMedia(t *testing.T) {
	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		r := srtPublishPlay(c, media.HEVCAACTS, srtPlayedUntil)
		checkSRTPlayedAs(c, r, "hevc", "aac")
	})

	check.Case(t, "PublishPlayOpus", check.Normal, func(c *check.C) {
		r := srtPublishPlay(c, media.H264OpusTS, srtOpusUntil)
		checkSRTPlayedAs(c, r, "h264", "opus")
	})
}

// srtPublishPlay plays a stream on Common while it publishes a TS file, and
// returns what the player got until u.
func srtPublishPlay(c *check.C, file string, u report.Until) *report.Report {
	c.Helper()
	srs := server.Common(c)
	stream := streamName(c)
	play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
	startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, file)
	return srtAnalyzeUntil(c, play, u, time.Second)
}

// srtLifecycle adds the lifecycle cases to TestSrt_Common: many players, a
// second publisher, a republish, and the API after a client leaves. A player
// that waits before the publisher is every play case's way to play. SRS
// tells a player of an unpublish only by closing it after
// peer_idle_timeout, 10s on Common, so TestTimer_Srt_PeerIdle checks that.
func srtLifecycle(t *testing.T) {
	check.Case(t, "ManyPlayers", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		const n = 4
		plays := make([]*srt.Conn, n)
		for i := range plays {
			plays[i] = dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")})
		}
		waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: stream}, n)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)

		reports, errs := make([]*report.Report, n), make([]error, n)
		var wg sync.WaitGroup
		for i, play := range plays {
			wg.Add(1)
			go func() {
				defer wg.Done()
				ctx, cancel := context.WithTimeout(context.Background(), time.Second)
				defer cancel()
				reports[i], errs[i] = play.Analyze(ctx, srtPlayedUntil)
			}()
		}
		wg.Wait()
		for i := range n {
			if c.CheckNoError(errs[i], fmt.Sprintf("player %d", i)) {
				checkSRTPlayed(c, reports[i])
			}
		}
	})

	// SRS refuses a second publisher of a published stream: it closes the
	// connection, and the stream plays on from the first.
	check.Case(t, "SecondPublisherRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		waitHook(c, srs, "on_publish", stream)

		second := dialSRTEarlyClose(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")})
		checkSRTClosedEarly(c, second)
		checkSRTPlayed(c, srtAnalyze(c, play))
	})

	// The stream is published again after its publisher leaves: a new
	// player plays the new publish from its start, and a player of the
	// first publish stays connected, within peer_idle_timeout. What that
	// player gets runs on from the first publish into the second with
	// continuity counters that skip, so it is not analyzed.
	check.Case(t, "RepublishPlays", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		first := startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		checkSRTPlayed(c, srtAnalyze(c, play))

		// SRS sends on_unpublish before it frees the stream for a new
		// publisher, and it refuses one before that, as busy; the first
		// publisher leaves the API after.
		published := waitHook(c, srs, "on_publish", stream)
		first.Close()
		waitHook(c, srs, "on_unpublish", stream)
		c.Must(untilGone(c, srs, "/api/v1/clients/"+published.Field("client_id")), "the first publisher leaves")

		m := mock.Match{Action: "on_play", Stream: stream}
		second := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")})
		waitHookCount(c, srs, m, 2)
		checkSRTPlayed(c, republishSRT(c, srs, stream, second))
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_stop", Stream: stream})), 0, "on_stop requests")
	})

	// A publisher that closes leaves the API, and so does its stream, which
	// has no players.
	check.Case(t, "PublisherCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")})
		h := waitHook(c, srs, "on_publish", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")
		c.CheckEqual(apiCode(c, srs, streamPath), 0, "the stream in the API")

		pub.Close()
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after it closed")
		c.CheckNoError(untilGone(c, srs, streamPath), "the stream after its publisher closed")
	})

	// A player that closes leaves the API, and the stream counts only the
	// publisher again.
	check.Case(t, "PlayerCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		h := waitHook(c, srs, "on_play", stream)
		srtAnalyze(c, play)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the player in the API")
		c.CheckEqual(streamClients(c, srs, streamPath), 2, "stream clients with a player")

		play.Close()
		waitHook(c, srs, "on_stop", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")
		c.CheckEqual(streamClients(c, srs, streamPath), 1, "stream clients after the player closed")
	})
}

// srtLatency adds the latency cases to TestSrt_Common, whose recvlatency is
// 0 and peerlatency SRS's default, 0: each end holds what it receives for
// the larger of its receive latency and the sender's peer latency, so a
// caller without latency gets none either way, and a caller's own latency
// applies both ways.
func srtLatency(t *testing.T) {
	check.Case(t, "LatencyNone", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		checkTSBPD(c, srs, 0, 0, 0)
	})

	check.Case(t, "LatencyFromCaller", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		checkTSBPD(c, srs, 200*time.Millisecond, 200*time.Millisecond, 200*time.Millisecond)
	})
}

// sharedRemuxOffSRT is the SRT setting of the RemuxOff server, for
// TestSrt_Latencies: a receive latency of 250ms and a peer latency of 150ms.
var sharedRemuxOffSRT = server.SRTOptions{RecvLatency: 250 * time.Millisecond, PeerLatency: 150 * time.Millisecond}

// TestSrt_Latencies checks recvlatency and peerlatency: SRS holds what a
// caller sends for at least its recvlatency, and asks the caller to hold
// what SRS sends for at least its peerlatency.
func TestSrt_Latencies(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	check.Case(t, "ServerLatencies", check.Normal, func(c *check.C) {
		checkTSBPD(c, srs, 0, 150*time.Millisecond, 250*time.Millisecond)
	})

	// A caller latency between the two raises only the peer latency.
	check.Case(t, "CallerBetween", check.Normal, func(c *check.C) {
		checkTSBPD(c, srs, 200*time.Millisecond, 200*time.Millisecond, 250*time.Millisecond)
	})
}

// sharedFmp4KeysSRT is the SRT setting of the Fmp4Keys server, for
// TestSrt_LatencyOnly: latency 300ms, with recvlatency and peerlatency at
// SRS's defaults.
var sharedFmp4KeysSRT = server.SRTOptions{Latency: 300 * time.Millisecond, KeepRecvLatency: true}

// TestSrt_LatencyOnly checks latency set alone, which full.conf says sets
// both recvlatency and peerlatency.
func TestSrt_LatencyOnly(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())

	check.Case(t, "LatencySetsBoth", check.Normal, func(c *check.C) {
		checkTSBPD(c, srs, 0, 300*time.Millisecond, 300*time.Millisecond)
	})
}

// checkTSBPD connects a player of a new stream with a caller latency and
// checks the TSBPD delays the handshake agreed: recv, how long the caller
// holds what SRS sends, and send, how long SRS holds what the caller sends.
func checkTSBPD(c *check.C, srs *server.Server, latency, recv, send time.Duration) {
	c.Helper()
	conn := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+streamName(c), "request"), Latency: latency})
	gotRecv, gotSend := conn.TSBPD()
	c.CheckEqual(gotRecv, recv, "the caller's receive delay")
	c.CheckEqual(gotSend, send, "SRS's receive delay")
}

// waitHookCount waits up to 1s, scaled with the limits, for n hooks that match m.
func waitHookCount(c *check.C, srs *server.Server, m mock.Match, n int) {
	c.Helper()
	c.Must(checkHooks(srs), "hooks")
	wait := check.Scaled(time.Second)
	deadline := time.Now().Add(wait)
	for len(srs.Hooks.Requests(m)) < n {
		if time.Now().After(deadline) {
			c.Fatalf("%v of %v %v after %v", len(srs.Hooks.Requests(m)), n, m, wait)
		}
		time.Sleep(5 * time.Millisecond)
	}
}

// srtPeerIdle is the peer_idle_timeout of the SegmentsKeep timer server.
const srtPeerIdle = time.Second

// sharedSegmentsKeepSRT is the SRT setting of the SegmentsKeep timer server.
var sharedSegmentsKeepSRT = server.SRTOptions{PeerIdleTimeout: srtPeerIdle}

// TestTimer_Srt_PeerIdle checks peer_idle_timeout, 1s: SRS closes a
// publisher that sends no media for that long, and a player that gets none,
// which is also how a player learns that its publisher left.
func TestTimer_Srt_PeerIdle(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// A publisher that sends its lead and then nothing, though its
	// connection is alive, is closed after the timeout.
	check.Case(t, "IdlePublisherClosed", check.Timer, func(c *check.C) {
		stream := streamName(c)
		pub := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")})
		srtSendLead(c, pub, media.H264HighAACTS)
		sent := time.Now()

		h := waitHookWithin(c, srs, "on_unpublish", stream, 3*srtPeerIdle)
		checkIdleFor(c, h.Time.Sub(sent), "publisher")
		checkSRTClosed(c, pub)
		c.Check(!streamPublishing(c, srs, "live/"+stream), "the stream still publishing after its publisher was closed")
	})

	// A player of a stream nobody publishes is closed after the timeout.
	check.Case(t, "IdlePlayerClosed", check.Timer, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		start := waitHook(c, srs, "on_play", stream)

		h := waitHookWithin(c, srs, "on_stop", stream, 3*srtPeerIdle)
		checkIdleFor(c, h.Time.Sub(start.Time), "player")
		checkSRTClosed(c, play)
	})

	// When its publisher leaves, a player is closed only after the timeout,
	// counted from the last media SRS sent it, not from the unpublish: SRS
	// drops what the publisher sent within its receive latency, and a busy
	// publisher may send its last media a while before it closes.
	check.Case(t, "UnpublishClosesPlayer", check.Timer, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		pub := startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		checkSRTPlayed(c, srtAnalyze(c, play))

		drain := drainSRT(play)
		pub.Close()
		unpublished := waitHook(c, srs, "on_unpublish", stream)
		h := waitHookWithin(c, srs, "on_stop", stream, 3*srtPeerIdle)
		last := drain.wait(c)
		c.Check(!h.Time.Before(unpublished.Time), "on_stop of the player before on_unpublish")
		// The player's last read may come a while after SRS sent it, when
		// the test process is busy: up to 150ms in a full run, so the
		// idle time may look that much shorter. A close at the unpublish
		// would be far shorter still.
		idle := h.Time.Sub(last)
		c.Check(idle >= srtPeerIdle*7/10 && idle <= srtPeerIdle+500*time.Millisecond,
			"the player closed %v after its last read, want about %v", idle, srtPeerIdle)
	})

	// A publisher and a player that stream for longer than the timeout
	// stay.
	check.Case(t, "StreamingKept", check.Timer, func(c *check.C) {
		stream := streamName(c)
		play := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		d := srtLead + srtPeerIdle*3/2
		r := srtAnalyzeUntil(c, play, report.Until{VideoHeader: true, AudioHeader: true, Duration: d}, d+time.Second)
		checkSRTPlayed(c, r)
		for _, action := range []string{"on_unpublish", "on_stop"} {
			c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: action, Stream: stream})), 0, action+" requests")
		}
	})
}

// srtSendLead sends srtLead of a TS file at once, and nothing after.
func srtSendLead(c *check.C, conn *srt.Conn, file string) {
	c.Helper()
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	defer f.Close()
	c.Must(srt.NewSender(f).SendThrough(conn, srtLead), "publish the lead")
}

// waitHookWithin is waitHook for at most d.
func waitHookWithin(c *check.C, srs *server.Server, action, stream string, d time.Duration) mock.Request {
	c.Helper()
	c.Must(checkHooks(srs), "hooks")
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	h, err := srs.Hooks.Wait(ctx, mock.Match{Action: action, Stream: stream})
	c.Must(err, action)
	return h
}

// checkIdleFor checks that SRS closed a connection after it was idle for
// about srtPeerIdle: not before, and within the next half second.
func checkIdleFor(c *check.C, idle time.Duration, what string) {
	c.Helper()
	c.Check(idle >= srtPeerIdle-100*time.Millisecond && idle <= srtPeerIdle+500*time.Millisecond,
		"the %v closed after %v idle, want about %v", what, idle, srtPeerIdle)
}

// republishSRT publishes the TS test file to live/stream again, and returns
// what the player gets of it, for at most 1s. SRS may still be tearing down the
// HTTP mount of the stream's last publish, on a worker every stream shares,
// and closes a publisher of the stream as disposing until it is done (the
// known bug of TestHttpFlv_NoGuess/RepublishAfterOtherStream), so a
// publisher SRS closes publishes again, for at most 300ms, while the player
// waits for the media.
func republishSRT(c *check.C, srs *server.Server, stream string, player *srt.Conn) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	type played struct {
		r   *report.Report
		err error
	}
	done := make(chan played, 1)
	go func() {
		r, err := player.Analyze(ctx, srtPlayedUntil)
		done <- played{r, err}
	}()
	deadline := time.Now().Add(300 * time.Millisecond)
	for tries := 1; ; tries++ {
		pub, err := trySRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		// The publisher stays until the case ends, when WaitClosed closes it.
		closed := make(chan struct{})
		if err != nil {
			close(closed)
		} else {
			alive, stop := context.WithCancel(context.Background())
			c.Cleanup(stop)
			go func() {
				pub.WaitClosed(alive)
				close(closed)
			}()
		}
		select {
		case p := <-done:
			c.Must(p.err, "play")
			return p.r
		case <-closed:
			pub.Close()
			if time.Now().After(deadline) {
				c.Fatalf("the publisher closed %d times within %v, the last: %v", tries, 300*time.Millisecond, err)
			}
			time.Sleep(10 * time.Millisecond)
		}
	}
}

// srtDrain reads a player until the server closes it.
type srtDrain struct {
	last   atomic.Int64
	closed chan struct{}
}

// drainSRT reads play in the background, and keeps the time of its last read.
func drainSRT(play *srt.Conn) *srtDrain {
	d := &srtDrain{closed: make(chan struct{})}
	d.last.Store(time.Now().UnixNano())
	go func() {
		defer close(d.closed)
		b := make([]byte, 2048)
		for {
			if _, err := play.Read(b); err != nil {
				return
			}
			d.last.Store(time.Now().UnixNano())
		}
	}()
	return d
}

// wait checks that the server closes the player within 400ms, and returns
// the time of the player's last read.
func (d *srtDrain) wait(c *check.C) time.Time {
	c.Helper()
	select {
	case <-d.closed:
	case <-time.After(400 * time.Millisecond):
		c.Errorf("the server didn't close the player")
	}
	return time.Unix(0, d.last.Load())
}
