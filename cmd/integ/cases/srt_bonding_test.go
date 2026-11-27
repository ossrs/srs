// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// srtBondClients are the clients that bond with SRS. Only libsrt does:
// zsiec's public Group leaves the group extension out of each link's
// handshake, so libsrt, SRS's too, takes each of its links as a plain
// caller (TestGroupCrossCheck keeps that a known failure).
var srtBondClients = []srt.GroupClient{srt.Libsrt.(srt.GroupClient)}

// srtBondModes are the group modes a caller picks; SRS takes either.
var srtBondModes = []srt.GroupMode{srt.GroupBroadcast, srt.GroupBackup}

// srtBondLatency is the latency of a group publisher, long enough for a
// backup group to send again over the standby link what the dead one lost.
const srtBondLatency = 120 * time.Millisecond

// srtBondOutage is how long a link is blackholed, and srtBondGap the longest
// the player may wait for media meanwhile, shorter than the outage, so a
// stream that waited for the link to come back fails.
const (
	srtBondOutage = 500 * time.Millisecond
	srtBondGap    = 250 * time.Millisecond
)

// srtBackupBug is F14: now and then, under load, a backup group whose active
// link is blackholed and restored hands SRS earlier messages again in the
// place of later ones, which are lost: a player got messages 20 to 23 twice,
// where 25 to 28 should be, and SRS's TS demuxer logged continuity errors
// for the publisher. Whether the libsrt v1.5.7 sender or SRS's libsrt
// 1.5.6 receiver is at fault is not known. It is a race, so the case marks
// it only when it sees it.
const srtBackupBug = "F14 a backup SRT group that loses and gets back its active link hands SRS old messages for new ones"

// srtRejectGroup is libsrt's SRT_REJ_GROUP, the reason a listener without
// group connections gives a group caller.
const srtRejectGroup = 1015

// srtBondConfig is the config of the bonding servers: the impaired cases'
// (SRT with hooks, peerlatency 20ms), srt_server.bonding on, and the port on
// IPv6 too, where the host has it.
func srtBondConfig(s server.SRTOptions) server.Config {
	s.Bonding = true
	s.IPv6 = true
	return srtImpairConfig(s)
}

// TestSrt_Bonding checks SRS with srt_server.bonding, from outside, for each
// client that bonds:
//   - bonding off, as by default: a group caller is refused in the
//     handshake, and a plain caller is served;
//   - bonding on: a group of broadcast or backup links is one publisher, with
//     one on_connect and one on_publish, one client in the API, and every
//     message once to a player;
//   - plain publishers and players of every library on the same port;
//   - a group in play mode is refused after the handshake, with no hook;
//   - the group's IP in the hooks and the API is its first link's, the
//     IPv6 or the IPv4 one.
func TestSrt_Bonding(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtBondConfig(server.SRTOptions{}))

	for _, cl := range srtBondClients {
		check.Case(t, "BondingOff_Refused_"+cl.Name(), check.Normal, func(c *check.C) {
			off := server.Common(c)
			stream := streamName(c)
			addr := off.Addr(server.SRT)
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			conn, err := cl.DialGroup(ctx, srt.GroupBroadcast, []string{addr, addr}, srt.Options{StreamID: srtID("live/"+stream, "publish")})
			if conn != nil {
				conn.Close()
			}
			var rej *srt.RejectError
			if c.Check(errors.As(err, &rej), "dial: %v, want the group refused", err) {
				c.CheckEqual(rej.Reason, srtRejectGroup, "reject reason")
			}
			c.CheckEqual(len(off.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the refused group")
			checkSRTServes(c, off, srt.Options{})
		})

		for _, mode := range srtBondModes {
			check.Case(t, "Publish_"+string(mode)+"_"+cl.Name(), check.Normal, func(c *check.C) {
				run := startSRTBond(c, srs, cl, mode, srt.GoSRT, nil)
				run.send(c, srtLead)
				run.waitWhole("the group's media", run.sent.messages()).check(c)

				h := waitHook(c, srs, "on_publish", run.name)
				c.CheckEqual(srtHooksOf(srs, h.Field("client_id")), "on_connect,on_publish", "the group's hooks")
				c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: run.name})), 1, "on_publish hooks")
				checkClient(c, apiClientOf(c, srs, h.Field("client_id")),
					clientWant{typ: "srt-publish", name: run.name, url: "/live/" + run.name, publish: true})
				c.CheckEqual(streamClients(c, srs, "/api/v1/streams/"+h.Field("stream_id")), 2, "stream clients, the group and the player")
			})
		}

		// A group that closes its links ends its publish at once, with each
		// hook of the group once.
		for _, mode := range srtBondModes {
			check.Case(t, "Closes_"+string(mode)+"_"+cl.Name(), check.Normal, func(c *check.C) {
				run := startSRTBond(c, srs, cl, mode, srt.GoSRT, nil)
				run.send(c, srtImpairWarm)
				id := waitHook(c, srs, "on_publish", run.name).Field("client_id")
				closed := time.Now()
				run.pub.Close()
				h := waitSRTHook(c, srs, "on_close", "", time.Second, func(got string) bool { return got == id })
				c.Check(h.Time.Sub(closed) < 400*time.Millisecond, "the group's publish ended %v after it closed", h.Time.Sub(closed))
				c.CheckEqual(srtHooksOf(srs, id), "on_connect,on_publish,on_unpublish,on_close", "the group's hooks")
			})
		}

		// A plain publisher and player of each library on the port the group
		// publishes to: the player of the group's stream gets every message,
		// and the plain publisher's stream plays.
		for _, plain := range []srt.Client{srt.GoSRT, srt.Zsiec, srt.Libsrt} {
			check.Case(t, "PlainCallersBeside_"+cl.Name()+"_"+plain.Name(), check.Normal, func(c *check.C) {
				run := startSRTBond(c, srs, cl, srt.GroupBroadcast, plain, nil)
				run.send(c, srtLead)
				run.waitWhole("the group's media", run.sent.messages()).check(c)

				stream := streamName(c) + "-plain"
				addr := srs.Addr(server.SRT)
				play := srtPlayerAt(c, plain, srs, addr, srt.Options{StreamID: srtID("live/"+stream, "request"), Latency: srtImpairCleanLatency}, stream)
				publishSRTAt(c, plain, addr, srt.Options{StreamID: srtID("live/"+stream, "publish"), Latency: srtImpairCleanLatency}, media.H264HighAACTS, srt.MessageSize)
				checkSRTPlayed(c, srtAnalyze(c, play))
			})
		}

		// SRS takes a group only to publish: in play mode it closes the group
		// once the stream ID tells the mode, before any hook, and a plain
		// player of the stream is served.
		check.Case(t, "PlayModeRefused_"+cl.Name(), check.Normal, func(c *check.C) {
			stream := streamName(c)
			addr := srs.Addr(server.SRT)
			id := srtID("live/"+stream, "request")
			// SRS may close the group before its second link joins, so the
			// dial's own result tells nothing.
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			if conn, err := cl.DialGroup(ctx, srt.GroupBroadcast, []string{addr, addr}, srt.Options{StreamID: id}); err == nil {
				c.Cleanup(func() { conn.Close() })
			}
			c.Check(waitSRTLog(c, srs, "srt bonding is publish only, refuse group to play, streamid="+id, time.Second),
				"no log of the group refused in play mode")
			c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the group player")
			srtPlayer(c, srs, srt.Options{StreamID: id}, stream)
		})

		// A group's first link is the IPv6 one or the IPv4 one: the other
		// goes through a relay that delays its handshake, so it joins later.
		for _, first := range []string{"::1", "127.0.0.1"} {
			check.Case(t, "FirstLinksIp_"+srtBondFamily(first)+"First_"+cl.Name(), check.Normal, func(c *check.C) {
				if !server.HasIPv6() {
					c.Skip("the host has no IPv6")
				}
				_, port, err := net.SplitHostPort(srs.Addr(server.SRT))
				c.Must(err, "the SRT port")
				later := "127.0.0.1"
				if first == later {
					later = "::1"
				}
				relay, err := srt.NewImpair(net.JoinHostPort(later, port), time.Now().UnixNano())
				c.Must(err, "start the relay")
				c.Cleanup(func() { relay.Close() })
				relay.Set(srt.Up, srt.Shape{Delay: 30 * time.Millisecond})

				stream := streamName(c)
				links := []string{net.JoinHostPort(first, port), relay.Addr().String()}
				dialSRTGroup(c, cl, srt.GroupBroadcast, links, srt.Options{StreamID: srtID("live/"+stream, "publish")})
				c.CheckEqual(waitHook(c, srs, "on_connect", stream).Field("ip"), first, "on_connect ip")
				h := waitHook(c, srs, "on_publish", stream)
				c.CheckEqual(h.Field("ip"), first, "on_publish ip")
				checkClientIP(c, srs, h.Field("client_id"), first)
			})
		}
	}
}

// TestTimer_Srt_Bonding checks a bonded publish whose links fail, from
// outside, for each client that bonds and each group mode, every link
// through a relay:
//   - one link blackholed mid-stream, the one a backup group sends over:
//     the player gets every message once, in order, with no wait longer than
//     srtBondGap (a backup group, F14); the publish goes on, with no
//     unpublish;
//   - every link blackholed: SRS closes the group after peer_idle_timeout,
//     with one on_unpublish, and a new group publishes the stream again;
//   - the first again with srt-test-live, libsrt's own bonded caller.
//
// libsrt's own break of the dead links never comes first: SRS sets each
// link's SRTO_PEERIDLETIMEO to peer_idle_timeout too, and libsrt breaks a
// link only after that and at least about 5s.
func TestTimer_Srt_Bonding(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtBondConfig(server.SRTOptions{PeerIdleTimeout: srtFailIdle}))

	// libsrt's own bonded caller, srt-test-live in Docker, as an outside
	// check of the same: a container takes about a second, so it runs only
	// with SRT_INTEROP on and the interop image.
	for _, mode := range srtBondModes {
		check.Case(t, "LinkBlackholed_"+string(mode)+"_"+srt.TestLive.Name(), check.Timer, func(c *check.C) {
			if err := srt.InteropReady(); err != nil {
				c.Skip(err.Error())
			}
			checkSRTBondLinkBlackholed(c, srs, srt.TestLive, mode)
		})
	}

	for _, cl := range srtBondClients {
		for _, mode := range srtBondModes {
			check.Case(t, "LinkBlackholed_"+string(mode)+"_"+cl.Name(), check.Timer, func(c *check.C) {
				checkSRTBondLinkBlackholed(c, srs, cl, mode)
			})

			check.Case(t, "AllLinksDown_"+string(mode)+"_"+cl.Name(), check.Timer, func(c *check.C) {
				run := startSRTBond(c, srs, cl, mode, srt.GoSRT, make([]*srt.Impair, 2))
				run.send(c, srtImpairWarm)
				run.stream()
				time.Sleep(200 * time.Millisecond)

				died := time.Now()
				for _, relay := range run.relays {
					relay.Blackhole(true)
				}
				unpublished := waitHookWithin(c, srs, "on_unpublish", run.name, 3*srtFailIdle)
				idle := unpublished.Time.Sub(died)
				c.Check(idle >= srtFailIdle-100*time.Millisecond && idle <= srtFailIdle+srtBondLatency+500*time.Millisecond,
					"the group closed %v after its links died, want about %v", idle, srtFailIdle)
				c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: run.name})), 1, "on_unpublish hooks")

				// A new group publishes the stream again, to a new player.
				addr := srs.Addr(server.SRT)
				play := collectSRT(dialSRTAt(c, srt.GoSRT, addr, srt.Options{StreamID: srtID("live/"+run.name, "request"), Latency: srtImpairCleanLatency}))
				waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: run.name}, 2)
				again := dialSRTGroup(c, cl, mode, []string{addr, addr}, srt.Options{StreamID: srtID("live/"+run.name, "publish"), Latency: srtBondLatency})
				waitHookCount(c, srs, mock.Match{Action: "on_publish", Stream: run.name}, 2)
				f, err := os.Open(media.Path(c, media.H264HighAACTS))
				c.Must(err, "open the media")
				c.Cleanup(func() { f.Close() })
				sent := &srtSent{w: again}
				c.Must(srt.NewSender(f).SendThrough(sent, srtLead), "publish again")
				msgs := sent.messages()
				n := len(bytes.Join(msgs, nil))
				got := play.waitFor(check.Scaled(time.Second), func(got []byte) bool { return len(got) >= n })
				(&srtWhole{what: "the new group's media", msgs: msgs, got: got}).check(c)
			})
		}
	}
}

// checkSRTBondLinkBlackholed publishes over a group of cl in mode, each link
// through a relay, and blackholes the link that carries the most media for
// srtBondOutage: the player gets every message once, in order, with no wait
// longer than srtBondGap, and the group stays the one publisher.
func checkSRTBondLinkBlackholed(c *check.C, srs *server.Server, cl srt.GroupClient, mode srt.GroupMode) {
	c.Helper()
	run := startSRTBond(c, srs, cl, mode, srt.GoSRT, make([]*srt.Impair, 2))
	checkSRTBondOutage(c, srs, run, mode)
}

// checkSRTBondOutage streams over the group of run, whose links each have a
// relay, and blackholes the link that carries the most media for
// srtBondOutage: the player gets every message once, in order, with no wait
// longer than srtBondGap, and the group stays the one publisher of srs.
func checkSRTBondOutage(c *check.C, srs *server.Server, run *srtBondRun, mode srt.GroupMode) {
	c.Helper()
	checkSRTBondOutageOf(c, srs, run, mode, run.busiest)
}

// checkSRTBondOutageOf is checkSRTBondOutage with the link that pick names
// once the media flows blackholed, not the busiest.
func checkSRTBondOutageOf(c *check.C, srs *server.Server, run *srtBondRun, mode srt.GroupMode, pick func() *srt.Impair) {
	c.Helper()
	run.send(c, srtImpairWarm)
	run.stream()
	time.Sleep(200 * time.Millisecond)

	dead := pick()
	run.got.resetGap()
	dead.Blackhole(true)
	time.Sleep(srtBondOutage)
	dead.Blackhole(false)
	time.Sleep(200 * time.Millisecond)
	gap := run.got.longestGap()

	c.Logf("the player's longest wait for media: %v", gap)
	c.Check(gap < srtBondGap, "the player waited %v for media, want under %v of an outage of %v", gap, srtBondGap, srtBondOutage)
	w := run.waitWhole("the media", run.sent.messages())
	if !w.whole() {
		order := srtMessageOrder(w.msgs, w.got)
		c.Logf("the player got the messages %v", order)
		if mode == srt.GroupBackup && srtOrderGoesBack(order) {
			check.KnownBug(c, srtBackupBug)
		}
	}
	w.check(c)
	c.Check(dead.Counters(srt.Up).Blackholed > 0, "the link blackholed carried nothing: %v", dead.Counters(srt.Up))
	run.checkOnePublish(c, srs)
}

// srtBondFamily names the IP family of a loopback address.
func srtBondFamily(ip string) string {
	if strings.Contains(ip, ":") {
		return "IPv6"
	}
	return "IPv4"
}

// srtBondRun is a group publisher of one stream through SRS and a player of
// it, with what the group sent and the player got.
type srtBondRun struct {
	name string
	// relays are the group's links when it has relays, one each.
	relays []*srt.Impair
	pub    *srt.Conn
	sender *srt.Sender
	sent   *srtSent
	got    *srtGot
	stop   func()
}

// startSRTBond starts a group of cl in mode publishing a new stream, with
// srtBondLatency, then a player of it with player: one link per relay of
// relays, each made here, or two links straight to SRS when relays is nil.
func startSRTBond(c *check.C, srs *server.Server, cl srt.GroupClient, mode srt.GroupMode, player srt.Client, relays []*srt.Impair) *srtBondRun {
	c.Helper()
	addr := srs.Addr(server.SRT)
	run := dialSRTBond(c, []string{addr, addr}, cl, mode, relays)
	run.playFrom(c, srs, addr, player)
	return run
}

// dialSRTBond starts a group of cl in mode publishing a new stream, with
// srtBondLatency: one link to each address of addrs, straight when relays is
// nil, or through relays[i], made here, to addrs[i]. The player comes with
// playFrom.
func dialSRTBond(c *check.C, addrs []string, cl srt.GroupClient, mode srt.GroupMode, relays []*srt.Impair) *srtBondRun {
	c.Helper()
	run := &srtBondRun{name: streamName(c), relays: relays, stop: func() {}}
	c.Cleanup(func() { run.stop() })
	links := addrs
	if relays != nil {
		links = nil
		for i, addr := range addrs {
			relay, err := srt.NewImpair(addr, time.Now().UnixNano()+int64(i))
			c.Must(err, "start the relay")
			c.Cleanup(func() { relay.Close() })
			relays[i] = relay
			links = append(links, relay.Addr().String())
		}
	}

	// The group first, then the player: srt-test-live's container may take
	// longer to start than a player without media waits on a server of
	// peer_idle_timeout, and the group sends nothing until the case does.
	run.pub = dialSRTGroup(c, cl, mode, links, srt.Options{StreamID: srtID("live/"+run.name, "publish"), Latency: srtBondLatency})
	return run
}

// playFrom waits for the group's on_publish on srs, then plays the stream
// with player from addr, srs itself or a proxy of it.
func (r *srtBondRun) playFrom(c *check.C, srs *server.Server, addr string, player srt.Client) {
	c.Helper()
	waitHook(c, srs, "on_publish", r.name)
	play := srtPlayerAt(c, player, srs, addr, srt.Options{StreamID: srtID("live/"+r.name, "request"), Latency: srtImpairCleanLatency}, r.name)
	r.got = collectSRT(play)
	f, err := os.Open(media.Path(c, media.H264HighAACTS))
	c.Must(err, "open the media")
	c.Cleanup(func() { f.Close() })
	r.sender = srt.NewSender(f)
	r.sent = &srtSent{w: r.pub}
}

// srtMessageOrder is, for each message of srt.MessageSize bytes in got, the
// index of the first message of msgs it equals, or -1, so a message got
// twice or out of order shows.
func srtMessageOrder(msgs [][]byte, got []byte) []int {
	var order []int
	for ; len(got) >= srt.MessageSize; got = got[srt.MessageSize:] {
		at := -1
		for i, m := range msgs {
			if bytes.Equal(m, got[:srt.MessageSize]) {
				at = i
				break
			}
		}
		order = append(order, at)
	}
	return order
}

// srtOrderGoesBack is whether a player got a message again, or one earlier
// than the one before it, in an order of srtMessageOrder: what F14 looks
// like, unlike a stream that stopped.
func srtOrderGoesBack(order []int) bool {
	for i := 1; i < len(order); i++ {
		if order[i] >= 0 && order[i] <= order[i-1] {
			return true
		}
	}
	return false
}

// srtHooksOf is the actions of the hooks of the client id, in order.
func srtHooksOf(srs *server.Server, id string) string {
	return strings.Join(hookActions(srs.Hooks.Requests(mock.Match{Func: func(r mock.Request) bool { return r.Field("client_id") == id }})), ",")
}

// dialSRTGroup calls links as a group of cl in mode, and checks that every
// link connected; the group is closed when the case ends.
func dialSRTGroup(c *check.C, cl srt.GroupClient, mode srt.GroupMode, links []string, opts srt.Options) *srt.Conn {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	conn, err := cl.DialGroup(ctx, mode, links, opts)
	c.Must(err, cl.Name()+" dial a "+string(mode)+" group of "+strings.Join(links, ", ")+" with streamid "+opts.StreamID)
	c.Cleanup(func() { conn.Close() })
	c.CheckEqual(conn.Links(), len(links), "links connected")
	return conn
}

// send sends at once the media before media time d.
func (r *srtBondRun) send(c *check.C, d time.Duration) {
	c.Helper()
	c.Must(r.sender.SendThrough(r.sent, d), "publish")
}

// stream sends the rest of the media in real time until the case ends.
func (r *srtBondRun) stream() {
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		defer close(done)
		r.sender.Stream(ctx, r.sent, nil, nil)
	}()
	r.stop = func() {
		cancel()
		<-done
	}
}

// waitWhole waits up to 1s, scaled, for the player to get as much as msgs.
func (r *srtBondRun) waitWhole(what string, msgs [][]byte) *srtWhole {
	n := len(bytes.Join(msgs, nil))
	got := r.got.waitFor(check.Scaled(time.Second), func(got []byte) bool { return len(got) >= n })
	return &srtWhole{what: what, msgs: msgs, got: got}
}

// busiest is the relay of the link that carried the most media: a broadcast
// group's links carry the same, and a backup group's active link all of it.
func (r *srtBondRun) busiest() *srt.Impair {
	busiest := r.relays[0]
	for _, relay := range r.relays[1:] {
		if relay.Counters(srt.Up).Sent > busiest.Counters(srt.Up).Sent {
			busiest = relay
		}
	}
	return busiest
}

// checkOnePublish checks that the group is still the stream's one publisher:
// one on_publish, no on_unpublish.
func (r *srtBondRun) checkOnePublish(c *check.C, srs *server.Server) {
	c.Helper()
	c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: r.name})), 1, "on_publish hooks")
	c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: r.name})), 0, "on_unpublish hooks")
}

// waitSRTLog waits up to d, scaled, for a line of SRS's console log that has
// text.
func waitSRTLog(c *check.C, srs *server.Server, text string, d time.Duration) bool {
	c.Helper()
	deadline := time.Now().Add(check.Scaled(d))
	for {
		if srtLogHas(c, srs, text) {
			return true
		}
		if time.Now().After(deadline) {
			return false
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// srtLogHas is whether a line of SRS's console log has text.
func srtLogHas(c *check.C, srs *server.Server, text string) bool {
	c.Helper()
	f, err := os.Open(filepath.Join(srs.Dir, "console.log"))
	c.Must(err, "open the console log")
	defer f.Close()
	s := bufio.NewScanner(f)
	s.Buffer(nil, 1<<20)
	for s.Scan() {
		if strings.Contains(s.Text(), text) {
			return true
		}
	}
	c.Must(s.Err(), "read the console log")
	return false
}
