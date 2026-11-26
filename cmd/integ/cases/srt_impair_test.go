// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"sync"
	"syscall"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
	"srs-integ/internal/ts"
)

// The latencies of the caller on the impaired link: srtImpairLatency in the
// real-time cases, srtImpairBurstLatency in the burst cases, short enough to
// keep them Normal, and srtImpairOutageLatency in the
// outage cases, shorter than the outage. srtImpairCleanLatency is that of
// the caller on the clean link, the other side of SRS, which only adds to
// the time a message takes.
//
// A message lost in an outage of the reports is dropped only when its time
// to play passes before the outage ends: with the impaired player, SRS sends
// it the clean publisher's latency after the outage starts, so that latency
// plus srtImpairOutageLatency must stay well under srtImpairOutage (at 80ms,
// 20ms under it, a loaded run recovered every message).
const (
	srtImpairLatency       = 120 * time.Millisecond
	srtImpairBurstLatency  = 100 * time.Millisecond
	srtImpairOutageLatency = 50 * time.Millisecond
	srtImpairCleanLatency  = 20 * time.Millisecond
)

// srtImpairWarm is the media sent over a clean link before the impairment:
// libsrt measures the RTT only from its first ACKs, and before it assumes
// 100ms, so it would wait longer than the latency to repeat a lost NAK.
const srtImpairWarm = 100 * time.Millisecond

// srtImpairBurst is the media sent at once, as an encoder sends a key frame:
// about 76 packets. srtImpairRealTime is the media sent in real time, about
// 50 packets.
const (
	srtImpairBurst    = 2 * time.Second
	srtImpairRealTime = time.Second
)

// srtImpairOutage is how long an outage lasts, longer than
// srtImpairOutageLatency: an outage in which nothing arrives, or one in which
// the receiver's reports do not reach the sender. srtImpairLateBurst is the
// media sent at once at the start of an outage of the reports, so its loss
// loses some messages for sure.
const (
	srtImpairOutage    = 120 * time.Millisecond
	srtImpairLateBurst = time.Second
)

// srtImpairLegs are the sides of SRS the impaired link is on: the
// publisher's, where SRS receives, and the player's, where it sends.
var srtImpairLegs = []string{"Publisher", "Player"}

// srtImpairLosses are the loss rates of each way of the loss cases.
var srtImpairLosses = []float64{2, 5, 10}

// srtImpairJitter is the delay and jitter of the jitter cases, at most 35ms
// each way, so a lost packet, its NAK and the packet again take at most
// 105ms, within srtImpairLatency.
var srtImpairJitter = srt.Shape{Delay: 20 * time.Millisecond, Jitter: 15 * time.Millisecond}

// srtImpairConfig is the config of the impaired cases' servers: SRT with
// hooks, and a peerlatency of srtImpairCleanLatency, so each player's latency
// is its own: libsrt's default, 120ms, would raise any lower one to it, as a
// receive latency of 0 lets each publisher's be its own.
func srtImpairConfig(s server.SRTOptions) server.Config {
	s.PeerLatency = srtImpairCleanLatency
	return server.WithSRT(server.Config{Listeners: []server.Listener{server.HTTPAPI}, Hooks: true}, s)
}

// srtLossBug is F4: over a link of 2% to 10% loss each way between SRS and a
// caller, now and then a lost packet is not sent again within the latency,
// 100ms or 120ms, on loopback (an RTT under 1ms), so the receiver drops it:
// SRS's libsrt 1.5.6 logs RCV-DROPPED, the next packet late by 4 to 41ms.
// Most often in a burst, such as a key frame (a third of the cases with a
// libsrt peer), now and then in real time; with a libsrt peer even 250ms of
// latency drops in a burst; least with a gosrt player. It is a race, so a
// case marks it only when it sees it.
const srtLossBug = "F4 SRT over a 2-10% loss link drops a packet that ARQ should recover"

// checkSRTWhole checks what a player got, and marks F4 when a message is
// missing.
func checkSRTWhole(c *check.C, w *srtWhole) {
	c.Helper()
	if !w.whole() {
		check.KnownBug(c, srtLossBug)
	}
	w.check(c)
}

// TestSrt_Impair checks SRT over a bad network, with the relay on the
// publisher's link or on the player's, for gosrt and libsrt:
//   - a burst with loss of 2%, 5% and 10% each way: every message arrives, in
//     order, with no TS continuity error (F4);
//   - an outage of both ways longer than the latency: the sender keeps what
//     it sent (libsrt for at least 1s), and nothing later arrives before the
//     receiver asks for it, so every message arrives, late;
//   - loss while the receiver's reports are lost, longer than the latency:
//     with tlpktdrop on, the lost messages are dropped and the player plays
//     on.
//
// The publisher stays on through each.
func TestSrt_Impair(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtImpairConfig(server.SRTOptions{}))

	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt} {
		for _, leg := range srtImpairLegs {
			for _, loss := range srtImpairLosses {
				check.Case(t, fmt.Sprintf("BurstLoss%v_%v_%v", loss, leg, cl.Name()), check.Normal, func(c *check.C) {
					run := startSRTImpair(c, srs, cl, srt.GoSRT, leg, srtImpairBurstLatency)
					run.impairBoth(srt.Shape{Loss: loss})
					checkSRTWhole(c, run.burst(c))
					run.checkPublishing(c, srs)
				})
			}

			check.Case(t, "OutageRecovered_"+leg+"_"+cl.Name(), check.Normal, func(c *check.C) {
				run := startSRTImpair(c, srs, cl, srt.GoSRT, leg, srtImpairOutageLatency)
				missing, ok := run.outage(c, func() { run.relay.Blackhole(true) }, 0)
				c.Check(ok, "the player got nothing after the outage (relay seed %v)", run.relay.Seed())
				c.CheckEqual(len(missing), 0, "messages lost in the outage")
				run.checkPublishing(c, srs)
			})

			check.Case(t, "LateLossDropped_"+leg+"_"+cl.Name(), check.Normal, func(c *check.C) {
				run := startSRTImpair(c, srs, cl, srt.GoSRT, leg, srtImpairOutageLatency)
				missing, ok := run.outage(c, run.lateLoss, srtImpairLateBurst)
				c.Check(ok, "the player got nothing after the outage (relay seed %v)", run.relay.Seed())
				c.Check(len(missing) > 0, "no message dropped, want the ones lost in the outage")
				run.checkPublishing(c, srs)
			})
		}
	}
}

// TestTimer_Srt_Impair checks SRT over a bad network with the media sent in
// real time, as an encoder of a low bitrate sends it, for gosrt and libsrt,
// with the relay on the publisher's link or on the player's: loss of 2%, 5%
// and 10% each way, and delay and jitter within the latency. Every message
// arrives, in order, with no TS continuity error (F4), and the publisher stays
// on. It takes srtImpairRealTime of media, so it is in the timer process.
func TestTimer_Srt_Impair(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtImpairConfig(server.SRTOptions{}))

	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt} {
		for _, leg := range srtImpairLegs {
			for _, loss := range srtImpairLosses {
				check.Case(t, fmt.Sprintf("Loss%v_%v_%v", loss, leg, cl.Name()), check.Timer, func(c *check.C) {
					run := startSRTImpair(c, srs, cl, srt.GoSRT, leg, srtImpairLatency)
					run.impairBoth(srt.Shape{Loss: loss})
					checkSRTWhole(c, run.realTime(c))
					run.checkPublishing(c, srs)
				})
			}

			check.Case(t, "DelayJitter_"+leg+"_"+cl.Name(), check.Timer, func(c *check.C) {
				run := startSRTImpair(c, srs, cl, srt.GoSRT, leg, srtImpairLatency)
				run.impairBoth(srtImpairJitter)
				checkSRTWhole(c, run.realTime(c))
				run.checkPublishing(c, srs)
			})
		}
	}
}

// srtFreezeBug is F3: with tlpktdrop off, libsrt's receive thread spins
// while a lost packet's time to play has passed, holding the socket's
// receive lock, so SRS's one thread blocks in srt_recvmsg and the whole
// server freezes: no stream, no SIGTERM, and every SRT peer breaks after
// about 5s. Whether the lost packet comes back before its time is a race,
// so the case marks it only when it sees it.
const srtFreezeBug = "F3 with tlpktdrop off a packet lost past its time to play freezes SRS"

// TestSrt_ImpairTlpktdropOff checks SRS with tlpktdrop off, whose receiver
// drops nothing: the messages a publisher loses while SRS's reports are
// lost arrive, late, once the reports reach it again. gosrt refuses the
// server, so libsrt is on both sides. The player's drop is the player's
// own setting, so only the publisher's link is impaired. The case has a
// server of its own, since F3 freezes it.
func TestSrt_ImpairTlpktdropOff(t *testing.T) {
	t.Parallel()

	check.Case(t, "LateLossKept_Publisher_libsrt", check.Normal, func(c *check.C) {
		var srs *server.Server
		var err error
		c.Untimed(func() { srs, err = server.Start(c.Name(), srtImpairConfig(server.SRTOptions{TLPktDropOff: true})) })
		c.Must(err, "start SRS")
		frozen := false
		c.Cleanup(func() {
			if frozen {
				// It ignores SIGTERM; the suite's stop would wait 5s.
				srs.Signal(syscall.SIGKILL)
				srs.Wait(time.Second)
				return
			}
			c.CheckNoError(srs.Stop(), "stop SRS")
		})

		// Like the server, its two libsrt processes and their warm-up are
		// the case's own setup, untimed: with F3, the wait for the player
		// that gets nothing took the case over its limit (528ms) in a full
		// run.
		var run *srtImpairRun
		c.Untimed(func() { run = startSRTImpair(c, srs, srt.Libsrt, srt.Libsrt, "Publisher", srtImpairOutageLatency) })
		missing, ok := run.outage(c, run.lateLoss, srtImpairLateBurst)
		if !ok {
			frozen = true
			check.KnownBug(c, srtFreezeBug)
		}
		c.Check(ok, "the player got nothing after the outage (relay seed %v)", run.relay.Seed())
		c.CheckEqual(len(missing), 0, "messages dropped")
		run.checkPublishing(c, srs)
	})
}

// srtImpairRun is a publisher and a player of one stream through SRS, one of
// them over the relay, and what each sent and got.
type srtImpairRun struct {
	name    string
	latency time.Duration
	relay   *srt.Impair
	// data is the relay's direction of the media, control that of the
	// receiver's reports, the ACKs and NAKs.
	data, control srt.Direction
	sender        *srt.Sender
	sent          *srtSent
	got           *srtGot
	// stop ends the real-time stream, if started.
	stop func()
}

// startSRTImpair starts a stream through SRS with cl on the relay, on leg,
// holding latency, and other on the clean side, and sends srtImpairWarm of
// the media over the clean relay, and waits for its receiver to measure the
// RTT.
func startSRTImpair(c *check.C, srs *server.Server, cl, other srt.Client, leg string, latency time.Duration) *srtImpairRun {
	c.Helper()
	return startSRTImpairAt(c, srs, srs.Addr(server.SRT), cl, other, leg, latency)
}

// startSRTImpairAt is startSRTImpair with every client calling addr, such
// as a proxy in front of SRS, which still sends the hooks.
func startSRTImpairAt(c *check.C, srs *server.Server, addr string, cl, other srt.Client, leg string, latency time.Duration) *srtImpairRun {
	c.Helper()
	relay, err := srt.NewImpair(addr, time.Now().UnixNano())
	c.Must(err, "start the relay")
	c.Cleanup(func() { relay.Close() })

	run := &srtImpairRun{name: streamName(c), latency: latency, relay: relay, data: srt.Up, control: srt.Down, stop: func() {}}
	c.Cleanup(func() { run.stop() })
	playCl, playAddr, playLatency := other, addr, srtImpairCleanLatency
	pubCl, pubAddr, pubLatency := other, addr, srtImpairCleanLatency
	if leg == "Player" {
		playCl, playAddr, playLatency = cl, relay.Addr().String(), latency
		run.data, run.control = srt.Down, srt.Up
	} else {
		pubCl, pubAddr, pubLatency = cl, relay.Addr().String(), latency
	}

	play := srtPlayerAt(c, playCl, srs, playAddr, srt.Options{StreamID: srtID("live/"+run.name, "request"), Latency: playLatency}, run.name)
	run.got = collectSRT(play)
	pub := dialSRTAt(c, pubCl, pubAddr, srt.Options{StreamID: srtID("live/"+run.name, "publish"), Latency: pubLatency})
	f, err := os.Open(media.Path(c, media.H264HighAACTS))
	c.Must(err, "open the media")
	c.Cleanup(func() { f.Close() })
	run.sender = srt.NewSender(f)
	run.sent = &srtSent{w: pub}

	run.send(c, srtImpairWarm)
	// The receiver of the impaired link ACKs every 10ms, and gets the warm-up
	// after the clean side's latency at most, so by then it has its RTT.
	time.Sleep(srtImpairCleanLatency + 30*time.Millisecond)
	return run
}

// impairBoth impairs both ways of the relay with s.
func (r *srtImpairRun) impairBoth(s srt.Shape) {
	r.relay.Set(srt.Up, s)
	r.relay.Set(srt.Down, s)
}

// send sends at once the media before media time d.
func (r *srtImpairRun) send(c *check.C, d time.Duration) {
	c.Helper()
	c.Must(r.sender.SendThrough(r.sent, d), "publish")
}

// stream sends the rest of the media in real time until the case ends, so
// a lost message is always followed by one that tells the receiver.
func (r *srtImpairRun) stream() {
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

// burst sends srtImpairBurst of the media at once, then the rest in real
// time, and waits for the player to get the messages up to the end of the
// burst. It waits srtImpairBurstWait at most: a message not there by then is
// past its time to play, so the player dropped it and the rest of the media
// takes its place, and a longer wait would only take a case that hits F4
// over its limit (seen: a loaded run held 25 messages for the whole 1s).
func (r *srtImpairRun) burst(c *check.C) *srtWhole {
	c.Helper()
	r.send(c, srtImpairWarm+srtImpairBurst)
	msgs := r.sent.messages()
	r.stream()
	return r.waitWholeFor("the burst", msgs, check.Scaled(srtImpairBurstWait))
}

// srtImpairBurstWait is how long a burst case waits for the player: the
// latency, then time for the last repeats.
const srtImpairBurstWait = srtImpairBurstLatency + 150*time.Millisecond

// realTime sends srtImpairRealTime of the media in real time, and waits for
// the player to get those messages.
func (r *srtImpairRun) realTime(c *check.C) *srtWhole {
	c.Helper()
	r.stream()
	time.Sleep(srtImpairRealTime)
	return r.waitWhole("the media in real time", r.sent.messages())
}

// srtWhole is what a player got of msgs, the messages sent from the start of
// the stream, which it must get each, in order; relay is its link, nil when
// it has none.
type srtWhole struct {
	what  string
	msgs  [][]byte
	got   []byte
	relay *srt.Impair
}

// waitWhole waits up to 1s, scaled, for the player to get as much as msgs.
func (r *srtImpairRun) waitWhole(what string, msgs [][]byte) *srtWhole {
	return r.waitWholeFor(what, msgs, check.Scaled(time.Second))
}

// waitWholeFor is waitWhole with its own wait.
func (r *srtImpairRun) waitWholeFor(what string, msgs [][]byte, wait time.Duration) *srtWhole {
	n := len(bytes.Join(msgs, nil))
	got := r.got.waitFor(wait, func(got []byte) bool { return len(got) >= n })
	return &srtWhole{what: what, msgs: msgs, got: got, relay: r.relay}
}

// whole is whether the player got every message, in order.
func (w *srtWhole) whole() bool {
	want := bytes.Join(w.msgs, nil)
	return len(w.got) >= len(want) && bytes.Equal(w.got[:len(want)], want)
}

// checkMessages checks that the player got every message, in order.
func (w *srtWhole) checkMessages(c *check.C) {
	c.Helper()
	if w.whole() {
		return
	}
	link := "no relay"
	if w.relay != nil {
		link = fmt.Sprintf("relay seed %v, %v up, %v down", w.relay.Seed(), w.relay.Counters(srt.Up), w.relay.Counters(srt.Down))
	}
	c.Fatalf("%v: got %v of %v bytes, messages missing %v of %v (%v)", w.what, len(w.got), len(bytes.Join(w.msgs, nil)),
		srtMissing(w.msgs, w.got), len(w.msgs), link)
}

// check is checkMessages, and checks that what the player got is a whole
// TS stream, with as many frames as were sent, and returns its report: the
// messages end in the middle of a frame, so what was sent is analyzed up to
// there.
func (w *srtWhole) check(c *check.C) *report.Report {
	c.Helper()
	w.checkMessages(c)
	want := bytes.Join(w.msgs, nil)
	sent, _ := ts.Analyze(ts.NewDemuxer(bytes.NewReader(want)), report.Until{})
	if !c.Check(sent.Video != nil && sent.Audio != nil, "%v: no video or audio sent: %v", w.what, sent) {
		return sent
	}
	got, err := ts.Analyze(ts.NewDemuxer(bytes.NewReader(w.got[:len(want)])), report.Until{VideoFrames: sent.Video.Frames, AudioFrames: sent.Audio.Frames})
	c.CheckNoError(err, w.what+": the TS the player got")
	return got
}

// outage impairs the relay with impair, sends burst of the media at once,
// then the rest in real time, and after srtImpairOutage clears the relay
// and waits for the player to get a message sent after it, for at most the
// time it takes plus 100ms. It returns the messages sent up to that one
// that the player did not get, and whether it got that one.
func (r *srtImpairRun) outage(c *check.C, impair func(), burst time.Duration) ([]int, bool) {
	c.Helper()
	impair()
	r.send(c, srtImpairWarm+burst)
	r.stream()
	time.Sleep(srtImpairOutage)
	r.impairBoth(srt.Shape{})
	// The message sent 10ms after the outage: the stream goes on.
	time.Sleep(10 * time.Millisecond)
	msgs := r.sent.messages()
	last := msgs[len(msgs)-1]
	got := r.got.waitFor(check.Scaled(r.latency+srtImpairCleanLatency+100*time.Millisecond), func(got []byte) bool { return bytes.Contains(got, last) })
	end := bytes.Index(got, last)
	if end < 0 {
		return nil, false
	}
	return srtMissing(msgs, got[:end+len(last)]), true
}

// lateLoss loses a fifth of the media while every report of the receiver is
// lost, so no lost message can be asked for again within the latency.
func (r *srtImpairRun) lateLoss() {
	r.relay.Set(r.control, srt.Shape{Blackhole: true})
	r.relay.Set(r.data, srt.Shape{Loss: 20})
}

// checkPublishing checks that the publisher is still on: SRS sent no
// on_unpublish.
func (r *srtImpairRun) checkPublishing(c *check.C, srs *server.Server) {
	c.Helper()
	c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: r.name})), 0, "on_unpublish hooks")
}

// srtMissing is the indexes of the messages not in got, walking both in
// order: each message got is there whole, after the one before it.
func srtMissing(msgs [][]byte, got []byte) []int {
	var missing []int
	for i, m := range msgs {
		if bytes.HasPrefix(got, m) {
			got = got[len(m):]
		} else {
			missing = append(missing, i)
		}
	}
	return missing
}

// srtSent writes to a connection and keeps each message it wrote.
type srtSent struct {
	w    io.Writer
	mu   sync.Mutex
	msgs [][]byte
}

func (s *srtSent) Write(p []byte) (int, error) {
	n, err := s.w.Write(p)
	if err == nil {
		s.mu.Lock()
		s.msgs = append(s.msgs, bytes.Clone(p))
		s.mu.Unlock()
	}
	return n, err
}

// messages is the messages written so far.
func (s *srtSent) messages() [][]byte {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([][]byte(nil), s.msgs...)
}

// srtGot is what a player read, as it reads.
type srtGot struct {
	mu   sync.Mutex
	buf  []byte
	err  error
	more chan struct{}
}

// collectSRT reads a player until it closes.
func collectSRT(play *srt.Conn) *srtGot {
	g := &srtGot{more: make(chan struct{}, 1)}
	go func() {
		p := make([]byte, 64*1024)
		for {
			n, err := play.Read(p)
			g.mu.Lock()
			g.buf = append(g.buf, p[:n]...)
			g.err = err
			g.mu.Unlock()
			select {
			case g.more <- struct{}{}:
			default:
			}
			if err != nil {
				return
			}
		}
	}()
	return g
}

// waitFor waits up to d for what the player got to satisfy ok, or for the
// player to end, and returns what it got.
func (g *srtGot) waitFor(d time.Duration, ok func([]byte) bool) []byte {
	deadline := time.After(d)
	for {
		g.mu.Lock()
		got, err := g.buf, g.err
		g.mu.Unlock()
		if ok(got) || err != nil {
			return got
		}
		select {
		case <-g.more:
		case <-deadline:
			return got
		}
	}
}
