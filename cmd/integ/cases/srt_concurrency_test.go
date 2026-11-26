// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"context"
	"fmt"
	"net/http"
	"os"
	"strconv"
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

// The sizes of the concurrency cases: the players of one stream and the
// streams at once. The players that stay through a republish or join
// mid-GOP are one of each library.
const (
	srtLoadPlayers = 20
	srtLoadStreams = 10
)

// srtLoadLoss is the loss of each way of every link in the lossy variants.
const srtLoadLoss = 2

// srtLoadProcessStart is about when the test process started, and
// srtLoadSettle how long after it the lossy variants start.
var srtLoadProcessStart = time.Now()

const srtLoadSettle = 3 * time.Second

// srtLoadMidGop is the media a publisher sends before players join mid-GOP:
// half the first GOP of the test file, whose GOP is 1s.
const srtLoadMidGop = 500 * time.Millisecond

// srtLoadBurst is the media a publisher of a case with no loss sends at
// once, about 25 messages: after srtLoadMidGop, past the next keyframe. Ten
// publishers of 2s at once, as TestSrt_Impair sends, take SRS over 1s under
// the load of the whole suite.
const srtLoadBurst = 600 * time.Millisecond

// TestSrt_Concurrency checks SRS with many SRT clients at once, each
// straight to SRS, and players of gosrt, zsiec and libsrt in turn:
//   - 20 players of one stream each get every message, in order;
//   - 10 streams at once, each publisher and player of another library;
//   - the publisher leaves and another comes back while the players stay:
//     each gets the second publish whole, after the first;
//   - players join mid-GOP: SRS has no GOP cache for SRT, so each gets the
//     stream from the first message after it joined, the rest of the GOP
//     before the next keyframe.
//
// TestTimer_Srt_Concurrency is the same at 2% loss.
func TestSrt_Concurrency(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtImpairConfig(server.SRTOptions{}))
	srtConcurrencyCases(t, srs, 0, check.Normal)
}

// TestTimer_Srt_Concurrency is TestSrt_Concurrency with every link of every
// client over a relay at 2% loss each way once the stream is warm, the media
// sent in real time at 120ms of latency as in TestTimer_Srt_Impair; a
// message lost for good is F4. Its cases run one at a time, from
// srtLoadSettle after the process starts: any of them at the start, beside
// the timer cases of peer_idle_timeout, which start then too, delays the
// close of their idle publisher by 0.5-0.7s, out of its window.
func TestTimer_Srt_Concurrency(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtImpairConfig(server.SRTOptions{}))
	srtConcurrencyCases(t, srs, srtLoadLoss, check.Timer)
}

// srtConcurrencyCases adds the concurrency cases at loss, as class; with
// loss they run one at a time.
func srtConcurrencyCases(t *testing.T, srs *server.Server, loss float64, class check.Class) {
	suffix := ""
	var mu sync.Mutex
	lock := func(c *check.C) {}
	if loss > 0 {
		suffix = fmt.Sprintf("_Loss%v", loss)
		lock = func(c *check.C) {
			c.Untimed(func() {
				time.Sleep(time.Until(srtLoadProcessStart.Add(srtLoadSettle)))
				mu.Lock()
			})
			c.Cleanup(mu.Unlock)
		}
	}

	check.Case(t, "ManyPlayers"+suffix, class, func(c *check.C) {
		lock(c)
		l := &srtLoad{srs: srs, loss: loss}
		stream := streamName(c)
		players := make([]*srtLoadPlayer, srtLoadPlayers)
		for i := range players {
			players[i] = l.player(c, srtLoadClient(i), stream)
		}
		waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: stream}, len(players))

		pub := l.publish(c, srt.GoSRT, stream)
		pub.send(c, srtImpairWarm)
		l.warmUp()
		msgs := pub.media(c)
		for _, p := range players {
			l.checkWhole(c, p, msgs)
		}
		checkSRTPublishing(c, srs, stream)
	})

	check.Case(t, "ManyStreams"+suffix, class, func(c *check.C) {
		lock(c)
		l := &srtLoad{srs: srs, loss: loss}
		streams := make([]string, srtLoadStreams)
		players := make([]*srtLoadPlayer, srtLoadStreams)
		for i := range streams {
			streams[i] = fmt.Sprintf("%v-%v", streamName(c), i)
			players[i] = l.player(c, srtLoadClient(i+1), streams[i])
		}
		for _, stream := range streams {
			waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: stream}, 1)
		}

		pubs := make([]*srtLoadPub, srtLoadStreams)
		for i, stream := range streams {
			pubs[i] = l.publish(c, srtLoadClient(i), stream)
			pubs[i].send(c, srtImpairWarm)
		}
		l.warmUp()
		msgs := srtLoadMedia(c, pubs)
		for i, p := range players {
			l.checkWhole(c, p, msgs[i])
			checkSRTPublishing(c, srs, streams[i])
		}
	})

	// The first publisher is gosrt and the second zsiec, which both close at
	// once: libsrt's close waits for the peer to acknowledge what it sent.
	check.Case(t, "RepublishPlayersStay"+suffix, class, func(c *check.C) {
		lock(c)
		l := &srtLoad{srs: srs, loss: loss}
		stream := streamName(c)
		players := make([]*srtLoadPlayer, len(srt.Clients()))
		for i := range players {
			players[i] = l.player(c, srtLoadClient(i), stream)
		}
		waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: stream}, len(players))

		first := l.publish(c, srt.GoSRT, stream)
		published := waitHook(c, srs, "on_publish", stream)
		first.send(c, srtImpairWarm)
		l.warmUp()
		firstMsgs := first.media(c)
		for _, p := range players {
			l.checkWhole(c, p, firstMsgs)
		}
		first.close()
		waitHook(c, srs, "on_unpublish", stream)
		c.Must(untilGone(c, srs, "/api/v1/clients/"+published.Field("client_id")), "the first publisher leaves")

		second := l.publishAgain(c, srt.Zsiec, stream)
		second.send(c, srtImpairWarm)
		secondMsgs := second.media(c)
		for _, p := range players {
			l.checkWhole(c, p.since(secondMsgs[0]), secondMsgs)
		}
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_stop", Stream: stream})), 0, "on_stop hooks")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_play", Stream: stream})), len(players), "on_play hooks")
	})

	check.Case(t, "PlayersJoinMidGop"+suffix, class, func(c *check.C) {
		lock(c)
		l := &srtLoad{srs: srs, loss: loss}
		stream := streamName(c)
		pub := l.publish(c, srt.GoSRT, stream)
		pub.send(c, srtLoadMidGop)
		// SRS holds what it got for the publisher's latency, so it has
		// delivered all of it to the stream by then.
		time.Sleep(l.latency() + 30*time.Millisecond)
		before := len(pub.sent.messages())

		players := make([]*srtLoadPlayer, len(srt.Clients()))
		for i := range players {
			players[i] = l.player(c, srtLoadClient(i), stream)
		}
		waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: stream}, len(players))
		l.warmUp()
		msgs := pub.media(c)
		for _, p := range players {
			r := l.checkWhole(c, p, msgs[before:])
			if c.Check(r.Video != nil, "%v: no video: %v", p.what, r) {
				c.Check(r.Video.Keyframes >= 1, "%v: no keyframe: %v", p.what, r)
				c.Check(r.Video.LeadingFrames > 0 && r.Video.LeadingFrames < media.GOP,
					"%v: %v frames before the first keyframe, want the rest of the GOP, 1 to %v", p.what, r.Video.LeadingFrames, media.GOP-1)
			}
		}
	})
}

// srtMaxConnections is the max_connections of TestSrt_MaxConnections, and
// srtMaxConnectionsIdle its peer_idle_timeout: a client whose shutdown is
// lost on a lossy link holds its place that long, longer than a case.
const (
	srtMaxConnections     = 3
	srtMaxConnectionsIdle = time.Second
)

// TestSrt_MaxConnections checks max_connections for SRT, on a server of its
// own whose cases run one at a time, since every connection counts: SRS
// takes the handshake of a caller past the limit, then closes it with no
// hook (F2 when the caller sees no shutdown), and serves the ones it holds;
// a connection that closes frees its place for the next caller. Each also
// with every link at 2% loss after its handshake.
func TestSrt_MaxConnections(t *testing.T) {
	t.Parallel()
	cfg := server.Config{
		Listeners: []server.Listener{server.HTTPAPI},
		Hooks:     true,
		Env:       map[string]string{"SRS_MAX_CONNECTIONS": strconv.Itoa(srtMaxConnections)},
	}
	srs := server.Group(t, server.WithSRT(cfg, server.SRTOptions{PeerLatency: srtImpairCleanLatency, PeerIdleTimeout: srtMaxConnectionsIdle}))
	var mu sync.Mutex
	lock := func(c *check.C) {
		c.Untimed(func() { mu.Lock() })
		c.Cleanup(mu.Unlock)
	}

	for _, loss := range []float64{0, srtLoadLoss} {
		suffix := ""
		if loss > 0 {
			suffix = fmt.Sprintf("_Loss%v", loss)
		}

		check.Case(t, "Refused"+suffix, check.Normal, func(c *check.C) {
			lock(c)
			l := &srtLoad{srs: srs, loss: loss}
			held := l.hold(c, srtMaxConnections)
			l.checkRefused(c, srt.GoSRT, srt.Libsrt)
			for _, id := range held {
				c.CheckEqual(len(srs.Hooks.Requests(srtClosed(id))), 0, "on_close of a held publisher")
			}
		})

		check.Case(t, "Freed"+suffix, check.Normal, func(c *check.C) {
			lock(c)
			l := &srtLoad{srs: srs, loss: loss}
			held := l.hold(c, srtMaxConnections)
			// A shutdown lost on the way leaves the close to
			// peer_idle_timeout, which TestTimer_Srt_Failure checks.
			if relay := l.pubs[0].relay; relay != nil {
				relay.Set(srt.Up, srt.Shape{})
			}
			l.pubs[0].close()
			c.Check(until(time.Second, func() bool { return len(srs.Hooks.Requests(srtClosed(held[0]))) > 0 }), "no on_close of the closed publisher in 1s")
			l.holdOne(c, srt.GoSRT, fmt.Sprintf("%v-next", streamName(c)))
			l.checkRefused(c, srt.GoSRT)
		})
	}
}

// srtLoadClient is the library of the i-th client of a case, each in turn.
func srtLoadClient(i int) srt.Client {
	cls := srt.Clients()
	return cls[i%len(cls)]
}

// srtLoadSeed makes the seeds of the relays of one run differ.
var srtLoadSeed atomic.Int64

// srtLoad is the links of a concurrency case's clients: each client over a
// relay of its own, at loss once lossy, or straight to SRS when the case
// has no loss.
type srtLoad struct {
	srs  *server.Server
	loss float64
	// lossy is whether the loss is on: a relay made then is lossy from its
	// first packet.
	lossy  bool
	relays []*srt.Impair
	pubs   []*srtLoadPub
}

// latency is the latency of every client: the least SRS lets a player
// hold with no loss, and that of the impaired cases with loss.
func (l *srtLoad) latency() time.Duration {
	if l.loss == 0 {
		return srtImpairCleanLatency
	}
	return srtImpairLatency
}

// link returns the address a new client calls, and its relay, nil with no
// loss.
func (l *srtLoad) link(c *check.C) (string, *srt.Impair) {
	c.Helper()
	if l.loss == 0 {
		return l.srs.Addr(server.SRT), nil
	}
	relay, err := srt.NewImpair(l.srs.Addr(server.SRT), time.Now().UnixNano()+srtLoadSeed.Add(1))
	c.Must(err, "start a relay")
	if len(l.relays) == 0 {
		// Before any client's cleanup, so it runs after them all, and the
		// shutdown of a client that closes at once still reaches SRS.
		c.Cleanup(func() {
			time.Sleep(srtLoadDrain)
			for _, relay := range l.relays {
				relay.Close()
			}
		})
	}
	l.relays = append(l.relays, relay)
	if l.lossy {
		srtLoadImpair(relay, l.loss)
	}
	return relay.Addr().String(), relay
}

// srtLoadDrain is how long the relays of a case forward after its clients
// close.
const srtLoadDrain = 50 * time.Millisecond

// lossNow puts the loss on every link so far.
func (l *srtLoad) lossNow() {
	for _, relay := range l.relays {
		srtLoadImpair(relay, l.loss)
	}
}

// srtLoadImpair puts loss on both ways of a relay.
func srtLoadImpair(relay *srt.Impair, loss float64) {
	relay.Set(srt.Up, srt.Shape{Loss: loss})
	relay.Set(srt.Down, srt.Shape{Loss: loss})
}

// warmUp, with loss, waits for the receivers of the warm media to measure
// the RTT, as startSRTImpair does, then turns the loss on.
func (l *srtLoad) warmUp() {
	if l.loss == 0 || l.lossy {
		return
	}
	time.Sleep(srtImpairCleanLatency + 30*time.Millisecond)
	l.lossy = true
	l.lossNow()
}

// srtLoadPlayer is a player of a concurrency case and what it got.
type srtLoadPlayer struct {
	what  string
	relay *srt.Impair
	got   *srtGot
	// from is where in what it got the part a check is about starts.
	from []byte
}

// player connects a player of cl to a stream; it reads until the case ends.
func (l *srtLoad) player(c *check.C, cl srt.Client, stream string) *srtLoadPlayer {
	c.Helper()
	addr, relay := l.link(c)
	conn := dialSRTAt(c, cl, addr, srt.Options{StreamID: srtID("live/"+stream, "request"), Latency: l.latency()})
	return &srtLoadPlayer{what: cl.Name() + " player of " + stream, relay: relay, got: collectSRT(conn)}
}

// since is the player whose check starts at the last time it got first, the
// first message of a publish: the test file's first message, with the
// PCR of its start, is in no other place of one publish.
func (p *srtLoadPlayer) since(first []byte) *srtLoadPlayer {
	return &srtLoadPlayer{what: p.what + " since the republish", relay: p.relay, got: p.got, from: first}
}

// checkWhole waits for a player to get msgs, and checks it got each, in
// order, from the first it got, as a whole TS stream; with loss, a message
// lost is F4. It returns the report of the stream it got.
func (l *srtLoad) checkWhole(c *check.C, p *srtLoadPlayer, msgs [][]byte) *report.Report {
	c.Helper()
	n := len(bytes.Join(msgs, nil))
	part := func(got []byte) []byte {
		if p.from == nil {
			return got
		}
		if i := bytes.LastIndex(got, p.from); i >= 0 {
			return got[i:]
		}
		return nil
	}
	got := p.got.waitFor(check.Scaled(time.Second), func(got []byte) bool { return len(part(got)) >= n })
	w := &srtWhole{what: p.what, msgs: msgs, got: part(got), relay: p.relay}
	if l.loss > 0 && !w.whole() {
		check.KnownBug(c, srtLossBug)
	}
	return w.check(c)
}

// srtLoadPub is a publisher of a concurrency case and what it sent.
type srtLoadPub struct {
	load *srtLoad
	// relay is its link, nil with no loss.
	relay  *srt.Impair
	conn   *srt.Conn
	sender *srt.Sender
	sent   *srtSent
	// at is the media time sent so far.
	at time.Duration
	// stop ends the real-time stream, if started.
	stop func()
}

// publish connects a publisher of cl to a stream.
func (l *srtLoad) publish(c *check.C, cl srt.Client, stream string) *srtLoadPub {
	c.Helper()
	addr, relay := l.link(c)
	conn := dialSRTAt(c, cl, addr, srt.Options{StreamID: srtID("live/"+stream, "publish"), Latency: l.latency()})
	f, err := os.Open(media.Path(c, media.H264HighAACTS))
	c.Must(err, "open the media")
	c.Cleanup(func() { f.Close() })
	p := &srtLoadPub{load: l, relay: relay, conn: conn, sender: srt.NewSender(f), sent: &srtSent{w: conn}, stop: func() {}}
	c.Cleanup(func() { p.stop() })
	l.pubs = append(l.pubs, p)
	return p
}

// publishAgain publishes a stream whose publisher has just left: SRS marks
// the stream as disposing until it is done and closes a publisher before
// then, so it calls again for at most 300ms until SRS takes one, by its
// on_publish.
func (l *srtLoad) publishAgain(c *check.C, cl srt.Client, stream string) *srtLoadPub {
	c.Helper()
	m := mock.Match{Action: "on_publish", Stream: stream}
	want := len(l.srs.Hooks.Requests(m)) + 1
	deadline := time.Now().Add(300 * time.Millisecond)
	for tries := 1; ; tries++ {
		p := l.publish(c, cl, stream)
		if until(100*time.Millisecond, func() bool { return len(l.srs.Hooks.Requests(m)) >= want }) {
			return p
		}
		p.close()
		if time.Now().After(deadline) {
			c.Fatalf("SRS took no publisher of %v in %d tries within 300ms", stream, tries)
		}
	}
}

// send sends at once the media before media time d.
func (p *srtLoadPub) send(c *check.C, d time.Duration) {
	c.Helper()
	c.Must(p.sender.SendThrough(p.sent, d), "publish")
	p.at = d
}

// stream sends the rest of the media in real time until the case ends, or
// close.
func (p *srtLoadPub) stream() {
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		defer close(done)
		p.sender.Stream(ctx, p.sent, nil, nil)
	}()
	p.stop = func() {
		cancel()
		<-done
	}
}

// media sends the media of the case: with no loss srtLoadBurst of it at
// once, then the rest in real time; with loss srtImpairRealTime of it in
// real time. It returns the messages sent by then.
func (p *srtLoadPub) media(c *check.C) [][]byte {
	c.Helper()
	if p.load.loss == 0 {
		p.send(c, p.at+srtLoadBurst)
		msgs := p.sent.messages()
		p.stream()
		return msgs
	}
	p.stream()
	time.Sleep(srtImpairRealTime)
	return p.sent.messages()
}

// srtLoadMedia sends the media of every publisher at once, as media does,
// and returns the messages each sent by then.
func srtLoadMedia(c *check.C, pubs []*srtLoadPub) [][][]byte {
	c.Helper()
	msgs := make([][][]byte, len(pubs))
	if len(pubs) == 0 || pubs[0].load.loss == 0 {
		for i, p := range pubs {
			msgs[i] = p.media(c)
		}
		return msgs
	}
	for _, p := range pubs {
		p.stream()
	}
	time.Sleep(srtImpairRealTime)
	for i, p := range pubs {
		msgs[i] = p.sent.messages()
	}
	return msgs
}

// close stops the stream and closes the publisher.
func (p *srtLoadPub) close() {
	p.stop()
	p.stop = func() {}
	p.conn.Close()
}

// hold connects n publishers, of each library in turn, each of a stream
// of its own, and returns their client ids. A connection the server closes
// lingers in its count a little after, as does an API request of this
// process, so each publisher calls again for at most 1s until SRS takes
// it, by its on_publish.
func (l *srtLoad) hold(c *check.C, n int) []string {
	c.Helper()
	// No idle API connection of this process may count toward the limit.
	httpClient.CloseIdleConnections()
	http.DefaultClient.CloseIdleConnections()
	ids := make([]string, n)
	for i := range ids {
		ids[i] = l.holdOne(c, srtLoadClient(i), fmt.Sprintf("%v-%v", streamName(c), i))
	}
	return ids
}

// holdOne connects a publisher of cl to a stream, calling again for at most
// 1s until SRS takes it, by its on_publish, every 100ms: SRS sends it about
// 25ms after the handshake. It returns the publisher's client id. When the
// case ends, it waits for the publisher's on_close, so the next case has
// the places.
func (l *srtLoad) holdOne(c *check.C, cl srt.Client, stream string) string {
	c.Helper()
	id := ""
	c.Cleanup(func() {
		if id != "" {
			wait := srtMaxConnectionsIdle + time.Second
			c.Check(until(wait, func() bool { return len(l.srs.Hooks.Requests(srtClosed(id))) > 0 }), "no on_close of %v in %v", stream, wait)
		}
	})
	m := mock.Match{Action: "on_publish", Stream: stream}
	deadline := time.Now().Add(time.Second)
	for tries := 1; ; tries++ {
		p := l.publish(c, cl, stream)
		l.lossNow()
		if until(100*time.Millisecond, func() bool { return len(l.srs.Hooks.Requests(m)) > 0 }) {
			id = l.srs.Hooks.Requests(m)[0].Field("client_id")
			return id
		}
		p.close()
		l.pubs = l.pubs[:len(l.pubs)-1]
		if time.Now().After(deadline) {
			c.Fatalf("SRS took no %v publisher of %v in %d tries within 1s", cl.Name(), stream, tries)
		}
	}
}

// srtClosed matches the on_close of a client, which names no stream.
func srtClosed(id string) mock.Match {
	return mock.Match{Action: "on_close", Func: func(r mock.Request) bool { return r.Field("client_id") == id }}
}

// checkRefused checks that SRS closes a publisher of each of cls past the
// limit soon after the handshake, with no hook of its stream: gosrt by its
// idle probe, as in checkSRTVhostRefuses, libsrt by the shutdown (F2). The
// publishers call at once, so their waits overlap.
func (l *srtLoad) checkRefused(c *check.C, cls ...srt.Client) {
	c.Helper()
	streams := make([]string, len(cls))
	conns := make([]*srt.Conn, len(cls))
	for i, cl := range cls {
		streams[i] = streamName(c) + "-refused-" + cl.Name()
		addr, _ := l.link(c)
		opts := srt.Options{StreamID: srtID("live/"+streams[i], "publish"), PeerIdleTimeout: srtProbeIdle, Latency: l.latency()}
		conns[i] = dialSRTAt(c, cl, addr, opts)
	}
	l.lossNow()
	for i, cl := range cls {
		if cl == srt.GoSRT {
			ctx, cancel := context.WithTimeout(context.Background(), 400*time.Millisecond)
			c.CheckNoError(conns[i].WaitClosedProbing(ctx, 50*time.Millisecond), cl.Name()+": the server closes the connection")
			cancel()
		} else {
			ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
			err := conns[i].WaitClosed(ctx)
			cancel()
			if err != nil {
				check.KnownBug(c, srtCloseBug)
			}
			c.CheckNoError(err, cl.Name()+": the server closes the connection")
		}
		c.CheckEqual(len(l.srs.Hooks.Requests(mock.Match{Stream: streams[i]})), 0, cl.Name()+": hooks for the refused stream")
	}
}

// checkSRTPublishing checks that a stream's publisher is still on: SRS sent
// no on_unpublish.
func checkSRTPublishing(c *check.C, srs *server.Server, stream string) {
	c.Helper()
	c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, stream+": on_unpublish hooks")
}
