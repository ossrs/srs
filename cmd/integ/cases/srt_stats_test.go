// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// srtStatsPithy is the interval of the stats cases' pithy print, short
// enough that each connection prints several times in a case, and after
// the impairment ends.
const srtStatsPithy = 100 * time.Millisecond

// srtStatsLoss is the loss of each way of the impaired link, for
// srtStatsLossTime of media in real time, about 60 data packets, and on
// until the relay has lost a data packet of the media's way, for at most
// srtStatsLossMax: at 5%, 60 packets lose none now and then (5%), 160
// almost never.
const (
	srtStatsLoss     = 5
	srtStatsLossTime = 1500 * time.Millisecond
	srtStatsLossMax  = 4 * time.Second
)

// srtStatsSample is how long a case waits for SRS to sample its clients'
// bytes for the API, every 3s from the start of the server, and a margin.
const srtStatsSample = 4 * time.Second

// srtStatsConfig is srtImpairConfig with the pithy print every
// srtStatsPithy.
func srtStatsConfig() server.Config {
	cfg := srtImpairConfig(server.SRTOptions{})
	cfg.Env["SRS_PITHY_PRINT_MS"] = strconv.FormatInt(srtStatsPithy.Milliseconds(), 10)
	return cfg
}

// TestTimer_Srt_Stats checks what SRS reports of an SRT stream over a link
// of srtStatsLoss each way, with the relay on the publisher's link or the
// player's, for gosrt and libsrt, against what the relay did:
//   - the pithy print's transport stats of the connection on the relay:
//     the loss and the retransmissions SRS saw or sent, summed over its
//     prints, as many as the relay lost and passed;
//   - those of the connection on the clean link: no loss and no
//     retransmission;
//   - the API's bytes of the publisher and the player: what SRS read and
//     wrote of the media, never the retransmissions.
//
// Each case has a server of its own: the pithy print is shared by every
// connection of a kind, which would then print in turn, and the API's
// sample is every 3s from the server's start.
func TestTimer_Srt_Stats(t *testing.T) {
	t.Parallel()

	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt} {
		for _, leg := range srtImpairLegs {
			check.Case(t, "Loss5_"+leg+"_"+cl.Name(), check.Timer, func(c *check.C) {
				srs := startSRTStats(c)
				run := runSRTStats(c, srs, cl, leg)

				pub := srtTransportStats(c, srs, run.pubID, "<- SRT_CPB Transport Stats #")
				play := srtTransportStats(c, srs, run.playID, "-> SRT_PLA Transport Stats #")
				up, down := run.relay.DataCounters(srt.Up), run.relay.DataCounters(srt.Down)
				c.Logf("publisher %v, player %v, relay up %+v, down %+v", pub, play, up, down)
				if leg == "Publisher" {
					c.CheckEqual(pub["pktRcvLoss"], int64(up.Lost-up.RetransLost), "the publisher's pktRcvLoss, the first sends the relay lost")
					c.CheckEqual(pub["pktRcvRetrans"], int64(up.Retrans-up.RetransLost), "the publisher's pktRcvRetrans, the retransmissions the relay passed")
					c.CheckEqual(play["pktSndLoss"], int64(0), "the clean player's pktSndLoss")
					c.CheckEqual(play["pktRetrans"], int64(0), "the clean player's pktRetrans")
				} else {
					c.CheckEqual(play["pktRetrans"], int64(down.Retrans), "the player's pktRetrans, the retransmissions SRS sent")
					least := int64(down.Lost - down.RetransLost)
					if !run.whole.whole() {
						least -= srtLossDropped
					}
					c.Check(play["pktSndLoss"] >= least && play["pktSndLoss"] <= int64(down.Retrans),
						"the player's pktSndLoss %v, want from the %v first sends the relay lost to the %v retransmissions",
						play["pktSndLoss"], down.Lost-down.RetransLost, down.Retrans)
					c.CheckEqual(pub["pktRcvLoss"], int64(0), "the clean publisher's pktRcvLoss")
					c.CheckEqual(pub["pktRcvRetrans"], int64(0), "the clean publisher's pktRcvRetrans")
				}
				drops := pub["pktRcvDrop"] + play["pktSndDrop"]
				// A player's libsrt drops a message it got too late on its own,
				// which SRS's stats do not count; on the publisher's link, SRS
				// is the receiver that drops it.
				if run.whole.whole() {
					c.CheckEqual(drops, int64(0), "drops with every message played")
				} else if leg == "Publisher" {
					c.Check(drops > 0, "messages missing with no drop in the stats")
				}
				checkSRTWhole(c, run.whole)
				run.checkAPIBytes(c, srs)
			})
		}
	}
}

// startSRTStats starts a server of srtStatsConfig for one case.
func startSRTStats(c *check.C) *server.Server {
	c.Helper()
	var srs *server.Server
	var err error
	c.Untimed(func() { srs, err = server.Start(c.Name(), srtStatsConfig()) })
	c.Must(err, "start SRS")
	c.Cleanup(func() { c.CheckNoError(srs.Stop(), "stop SRS") })
	return srs
}

// srtStatsRun is a stream of a stats case, and the client IDs of its
// publisher and player.
type srtStatsRun struct {
	*srtImpairRun
	start         time.Time
	pubID, playID string
	whole         *srtWhole
}

// runSRTStats streams through SRS with cl on the relay, on leg: the media
// in real time at srtStatsLoss each way, for srtStatsLossTime and until the
// relay lost a data packet of the media, and then over a clean link for
// the latency and three pithy prints, so each connection prints what came
// of the loss.
func runSRTStats(c *check.C, srs *server.Server, cl srt.Client, leg string) *srtStatsRun {
	c.Helper()
	run := &srtStatsRun{start: time.Now()}
	run.srtImpairRun = startSRTImpair(c, srs, cl, srt.GoSRT, leg, srtImpairLatency)
	run.pubID = waitHook(c, srs, "on_publish", run.name).Field("client_id")
	run.playID = waitHook(c, srs, "on_play", run.name).Field("client_id")
	run.impairBoth(srt.Shape{Loss: srtStatsLoss})
	run.stream()
	time.Sleep(srtStatsLossTime)
	c.Check(untilEvery(srtStatsLossMax-srtStatsLossTime, 50*time.Millisecond, func() bool {
		return run.relay.DataCounters(run.data).Lost > 0
	}), "the relay lost no data packet in %v at %v%%: %+v", srtStatsLossMax, srtStatsLoss, run.relay.DataCounters(run.data))
	run.impairBoth(srt.Shape{})
	time.Sleep(srtImpairLatency + 3*srtStatsPithy)
	run.whole = run.waitWhole("the media in real time", run.sent.messages())
	return run
}

// sentBytes is the bytes the publisher wrote so far.
func (r *srtStatsRun) sentBytes() int64 {
	var n int64
	for _, m := range r.sent.messages() {
		n += int64(len(m))
	}
	return n
}

// checkAPIBytes waits for SRS to sample its clients for the API, and checks
// the publisher's recv_bytes and the player's send_bytes, both of one
// sample, as one list of the clients shows them: the media SRS read and
// wrote, not the retransmissions, so the player's at most the publisher's,
// and that at most what the publisher sent; and at least half of what the
// publisher sent and the player got, as the sample is up to 3s old. The
// player gets later than SRS writes, by its latency and more, so what it
// got bounds only from below.
func (r *srtStatsRun) checkAPIBytes(c *check.C, srs *server.Server) {
	c.Helper()
	var pub, play map[string]any
	c.Check(untilEvery(srtStatsSample, 100*time.Millisecond, func() bool {
		clients, _ := apiList(c, srs, "clients", "?count=100")
		pub, play = nil, nil
		for _, cl := range clients {
			switch cl["id"] {
			case r.pubID:
				pub = cl
			case r.playID:
				play = cl
			}
		}
		return pub != nil && play != nil && num(c, pub, "recv_bytes") > 0 && num(c, play, "send_bytes") > 0
	}), "the publisher %v and the player %v after %v, want both with the bytes of SRS's sample", pub, play, srtStatsSample)
	if pub == nil || play == nil {
		return
	}
	sent := r.sentBytes()
	r.got.mu.Lock()
	got := int64(len(r.got.buf))
	r.got.mu.Unlock()
	recv, wrote := int64(num(c, pub, "recv_bytes")), int64(num(c, play, "send_bytes"))
	c.Check(recv <= sent && recv >= sent/2, "the publisher's recv_bytes %v, want at most the %v it sent and at least half", recv, sent)
	c.Check(wrote <= recv && wrote >= got/2, "the player's send_bytes %v, want at most the publisher's %v and at least half the %v the player got", wrote, recv, got)
}

// srtStatsField is one name=number of a log line.
var srtStatsField = regexp.MustCompile(`(\w+)=(-?\d+)`)

// srtLogLines is the text after prefix of each line of SRS's console log of
// the connection cid.
func srtLogLines(c *check.C, srs *server.Server, cid, prefix string) []string {
	c.Helper()
	f, err := os.Open(filepath.Join(srs.Dir, "console.log"))
	c.Must(err, "open the console log")
	defer f.Close()
	var lines []string
	s := bufio.NewScanner(f)
	s.Buffer(nil, 1<<20)
	for s.Scan() {
		line := s.Text()
		if !strings.Contains(line, "]["+cid+"]") {
			continue
		}
		if _, after, ok := strings.Cut(line, prefix); ok {
			lines = append(lines, after)
		}
	}
	c.Must(s.Err(), "read the console log")
	return lines
}

// srtTransportStats sums the fields of the pithy print's transport stats of
// the connection cid, whose line starts with prefix: SRS reads them with
// srt_bstats and clears them, so each line counts from the one before. It
// needs at least two lines.
func srtTransportStats(c *check.C, srs *server.Server, cid, prefix string) map[string]int64 {
	c.Helper()
	lines := srtLogLines(c, srs, cid, prefix)
	if len(lines) < 2 {
		c.Fatalf("%v pithy prints of %v, want at least 2", len(lines), cid)
	}
	sum := map[string]int64{}
	for _, l := range lines {
		for _, m := range srtStatsField.FindAllStringSubmatch(l, -1) {
			n, err := strconv.ParseInt(m[2], 10, 64)
			c.Must(err, "parse "+m[0])
			sum[m[1]] += n
		}
	}
	return sum
}
