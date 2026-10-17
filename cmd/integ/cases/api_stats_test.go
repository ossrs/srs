// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"context"
	"io"
	"net/http"
	"slices"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// The publish timeouts of TestApi_Stats: SRS adds an RTMP publisher's frames to
// its stream's counts each time it checks the publisher, the first time after
// the first-packet timeout, then after each normal timeout. SRS closes a
// publisher that sends nothing for as long, so a runner with its limits scaled
// gets them scaled too.
func statsPublishTimeout() time.Duration { return check.Scaled(300 * time.Millisecond) }

// statsWait is how long a TestApi_Stats case waits for the counts to follow a
// client that came or left: SRS updates them after the client's hooks.
func statsWait() time.Duration { return check.Scaled(300 * time.Millisecond) }

// statsConfig is the server of TestApi_Stats: RTMP, HTTP-FLV and the exporter
// with a label and a tag, with every hook on the mock and the mock as the forward backend, and short
// publish timeouts. It plays at the lowest latency, like Common, so SRS sees a
// player's close at once.
func statsConfig() server.Config {
	ms := strconv.FormatInt(statsPublishTimeout().Milliseconds(), 10)
	return server.Config{
		Listeners:      []server.Listener{server.RTMP, server.HTTPAPI, server.HTTPServer, server.Exporter},
		Hooks:          true,
		ForwardBackend: true,
		Env: map[string]string{
			"SRS_VHOST_PUBLISH_FIRSTPKT_TIMEOUT": ms,
			"SRS_VHOST_PUBLISH_NORMAL_TIMEOUT":   ms,
			"SRS_VHOST_MIN_LATENCY":              "on",
			"SRS_VHOST_PLAY_MW_LATENCY":          "0",
			"SRS_VHOST_HTTP_REMUX_ENABLED":       "on",
			"SRS_VHOST_HTTP_REMUX_MOUNT":         "[vhost]/[app]/[stream].flv",
			"SRS_EXPORTER_LABEL":                 statsExporterLabel,
			"SRS_EXPORTER_TAG":                   statsExporterTag,
		},
	}
}

// forwardTo is the answer of the forward backend: the URLs to forward to.
func forwardTo(urls ...string) mock.Reply {
	if urls == nil {
		urls = []string{}
	}
	return mock.JSON(map[string]any{"code": 0, "data": map[string]any{"urls": urls}})
}

// TestApi_Stats checks what Common can't, since its other streams change it:
// the counts of the default vhost and the totals, pagination, the forwards and
// the RTMP frame counts. Its cases change the server's totals, so they run one
// at a time, each from an idle server, and each leaves the server idle.
func TestApi_Stats(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, statsConfig())
	// The backend answers no forward but for the streams a case scripts.
	srs.Hooks.Script(mock.Match{Action: "on_forward"}, forwardTo())
	var mu sync.Mutex
	lock := func(c *check.C) {
		c.Untimed(func() {
			mu.Lock()
			c.Cleanup(mu.Unlock)
			c.Must(untilIdle(c, srs), "the server is idle before the case")
		})
	}

	// CountersAfterGracefulClose publishes a stream with an RTMP and an
	// HTTP-FLV player, then each closes as it should: the vhost and stream
	// counts follow each client, the stream goes with its last client, and
	// no close counts as an error, while each counts as a client in
	// srs_clients_total.
	check.Case(t, "CountersAfterGracefulClose", check.Normal, func(c *check.C) {
		lock(c)
		errs, total := metric(c, srs, "srs_clients_errs_total"), metric(c, srs, "srs_clients_total")
		stream := streamName(c)
		pub, _, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		rtmpPlayer, _ := playRTMPOf(c, srs, stream, nil)
		flvPlayer := httpFlvPlayer(c, srs, "live/"+stream+".flv", nil)
		readHTTPFlv(c, flvPlayer, playedUntil)
		waitHookCount(c, srs, mock.Match{Action: "on_play", Stream: stream}, 2)
		checkCounts(c, srs, streamID, 3, 1)

		rtmpPlayer.Close()
		c.Check(until(statsWait(), func() bool { return countsAre(c, srs, streamID, 2, 1) }), "the counts after the RTMP player closed")
		flvPlayer.Close()
		c.Check(until(statsWait(), func() bool { return countsAre(c, srs, streamID, 1, 1) }), "the counts after the HTTP-FLV player closed")
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Unpublish(ctx)
		c.Must(err, "unpublish")
		pub.Close()
		c.CheckNoError(untilGone(c, srs, "/api/v1/streams/"+streamID), "the stream after its last client")
		c.Check(until(statsWait(), func() bool { return countsAre(c, srs, "", 0, 0) }), "the counts after every client closed")
		c.CheckEqual(metric(c, srs, "srs_clients_errs_total"), errs, "srs_clients_errs_total")
		c.CheckEqual(metric(c, srs, "srs_clients_total"), total+3, "srs_clients_total")
	})

	// KickCountsError deletes a publisher and a player by the API: unlike a
	// graceful close, each counts as an error in srs_clients_errs_total.
	check.Case(t, "KickCountsError", check.Normal, func(c *check.C) {
		lock(c)
		errs := metric(c, srs, "srs_clients_errs_total")
		stream := streamName(c)
		_, pubID, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		_, playID := playRTMPOf(c, srs, stream, nil)
		checkKicked(c, apiKick(c, srs, playID))
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+playID), "the player after its DELETE")
		c.CheckEqual(metric(c, srs, "srs_clients_errs_total"), errs+1, "srs_clients_errs_total after the player's DELETE")
		checkKicked(c, apiKick(c, srs, pubID))
		c.CheckNoError(untilGone(c, srs, "/api/v1/streams/"+streamID), "the stream after its publisher's DELETE")
		c.CheckEqual(metric(c, srs, "srs_clients_errs_total"), errs+2, "srs_clients_errs_total after the publisher's DELETE")
	})

	// RtmpFrameCounts publishes publishLead at once, then in real time: by
	// its first check, at the first-packet timeout, the stream counts the 12
	// video and 21 audio frames of the lead, and at each later check those
	// that came since; never more than were sent.
	check.Case(t, "RtmpFrameCounts", check.Normal, func(c *check.C) {
		lock(c)
		stream := streamName(c)
		start := time.Now()
		_, _, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		var st map[string]any
		c.Check(until(statsPublishTimeout()+check.Scaled(100*time.Millisecond), func() bool {
			st = apiStreamOf(c, srs, streamID)
			return num(c, st, "video_frames") >= 12 && num(c, st, "audio_frames") >= 21
		}), "stream frames video %v audio %v, want the lead's 12 and 21", st["video_frames"], st["audio_frames"])
		// The media sent by now: the lead, and real time since,
		// which the pacer may lead by a frame, with a sequence header of
		// each track.
		sent := publishLead + time.Since(start) + 100*time.Millisecond
		v, a := num(c, st, "video_frames"), num(c, st, "audio_frames")
		c.Check(v <= sent.Seconds()*25+1 && a <= sent.Seconds()*44100/1024+1, "frames video %v audio %v, more than the %v sent", v, a, sent)
		c.CheckEqual(num(c, st, "frames"), v+a, "frames")
	})

	// StreamsPagination publishes 12 streams: a page of start and count
	// lists from the start-th stream at most count streams, the default
	// count is 10, a count below 1 is 1 and a start below 0 is 0, and
	// every page has the total of 12; the pages of 5 make up the whole
	// list, in its order.
	check.Case(t, "StreamsPagination", check.Normal, func(c *check.C) {
		lock(c)
		for i := 0; i < 12; i++ {
			publishRTMP(c, srs, streamName(c)+"-"+strconv.Itoa(i), media.H264HighAAC)
		}
		checkPagination(c, srs, "streams", 12)
	})

	// ClientsPagination plays each of 4 streams with 2 RTMP players: the
	// same pagination over the 12 clients.
	check.Case(t, "ClientsPagination", check.Normal, func(c *check.C) {
		lock(c)
		for i := 0; i < 4; i++ {
			stream := streamName(c) + "-" + strconv.Itoa(i)
			publishRTMP(c, srs, stream, media.H264HighAAC)
			for range 2 {
				playOnly(c, rtmpURL(srs, "live/"+stream))
			}
		}
		c.Must(waitTotal(c, srs, "clients", 12), "12 clients")
		checkPagination(c, srs, "clients", 12)
	})

	// ForwardFields publishes a stream that the backend forwards to two
	// streams of the server: two forwards of the stream, each with every
	// field and its URL, forwarding soon after connecting, with the same id
	// in the list and by its own path.
	check.Case(t, "ForwardFields", check.Normal, func(c *check.C) {
		lock(c)
		stream := streamName(c)
		urls := []string{rtmpURL(srs, "live/"+stream+"-a"), rtmpURL(srs, "live/"+stream+"-b?k=v")}
		srs.Hooks.Script(mock.Match{Action: "on_forward", Stream: stream}, forwardTo(urls...))
		_, _, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		vid := defaultVhost(c, srs)["id"]

		var fwds []map[string]any
		c.Check(until(statsWait(), func() bool {
			fwds, _ = apiList(c, srs, "forwards", "?stream="+streamID)
			return len(fwds) == 2 && fwds[0]["state"] == "forwarding" && fwds[1]["state"] == "forwarding"
		}), "forwards %v, want 2 forwarding", fwds)
		var got []string
		for _, f := range fwds {
			checkFields(c, f, forwardFields, "forward")
			c.CheckEqual(f["stream"], streamID, "forward stream")
			c.CheckEqual(f["vhost"], vid, "forward vhost")
			c.CheckEqual(f["app"], "live", "forward app")
			c.CheckEqual(f["name"], stream, "forward name")
			nearNow(c, num(c, f, "state_at"), 2*time.Second, "forward state_at")
			u, _ := f["url"].(string)
			got = append(got, u)
			id, _ := f["id"].(string)
			byID := object(c, apiObject(c, srs, "/api/v1/forwards/"+id), "forward")
			c.CheckEqual(byID["id"], id, "the forward by its id")
			c.CheckEqual(byID["url"], f["url"], "the forward's url by its id")
		}
		// SRS may add its own query, such as the vhost, after the URL's.
		for _, u := range urls {
			c.Check(slices.ContainsFunc(got, func(g string) bool { return strings.HasPrefix(g, u) }), "forward urls %v, want one of %s", got, u)
		}
		again, _ := apiList(c, srs, "forwards", "?stream="+streamID)
		c.CheckEqual(ids(again), ids(fwds), "the forward ids while forwarding")
	})

	// ForwardsPagination publishes a stream with 3 forwards and one with 2:
	// ?stream= lists only that stream's, with its total, and start and
	// count page them; with no stream, the total is all 5.
	check.Case(t, "ForwardsPagination", check.Normal, func(c *check.C) {
		lock(c)
		streamIDs := map[int]string{}
		for _, n := range []int{3, 2} {
			stream := streamName(c) + "-" + strconv.Itoa(n)
			var urls []string
			for i := range n {
				urls = append(urls, rtmpURL(srs, "live/"+stream+"-to"+strconv.Itoa(i)))
			}
			srs.Hooks.Script(mock.Match{Action: "on_forward", Stream: stream}, forwardTo(urls...))
			_, _, streamIDs[n] = publishRTMP(c, srs, stream, media.H264HighAAC)
		}
		c.Must(waitTotal(c, srs, "forwards", 5), "5 forwards")

		all, total := apiList(c, srs, "forwards", "?stream="+streamIDs[3])
		c.Check(total == 3 && len(all) == 3, "the 3-forward stream lists %d of total %d", len(all), total)
		for _, f := range all {
			c.CheckEqual(f["stream"], streamIDs[3], "the stream of a filtered forward")
		}
		page, total := apiList(c, srs, "forwards", "?stream="+streamIDs[3]+"&start=1&count=1")
		c.CheckEqual(total, 3, "the total of a page")
		c.CheckEqual(ids(page), ids(all)[1:2], "the page at start 1 of count 1")
		page, total = apiList(c, srs, "forwards", "?stream="+streamIDs[3]+"&start=3")
		c.Check(total == 3 && len(page) == 0, "the page past the end lists %d of total %d", len(page), total)
		two, total := apiList(c, srs, "forwards", "?stream="+streamIDs[2])
		c.Check(total == 2 && len(two) == 2, "the 2-forward stream lists %d of total %d", len(two), total)
		page, total = apiList(c, srs, "forwards", "?count=2")
		c.Check(total == 5 && len(page) == 2, "no filter, count 2: %d of total %d", len(page), total)
		page, total = apiList(c, srs, "forwards", "?stream=vid-none")
		c.Check(total == 0 && len(page) == 0, "an unknown stream lists %d of total %d", len(page), total)
	})

	// ForwardsGoneAfterUnpublish unpublishes a stream with forwards: its
	// forwards leave the list and their ids are not found.
	check.Case(t, "ForwardsGoneAfterUnpublish", check.Normal, func(c *check.C) {
		lock(c)
		stream := streamName(c)
		srs.Hooks.Script(mock.Match{Action: "on_forward", Stream: stream}, forwardTo(rtmpURL(srs, "live/"+stream+"-a")))
		pub, _, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		c.Must(waitTotal(c, srs, "forwards", 1), "the forward")
		fwds, _ := apiList(c, srs, "forwards", "?stream="+streamID)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Unpublish(ctx)
		c.Must(err, "unpublish")
		c.CheckNoError(waitTotal(c, srs, "forwards", 0), "the forwards after the unpublish")
		for _, id := range ids(fwds) {
			c.CheckEqual(codeOf(apiFetch(c, srs, "/api/v1/forwards/"+id)), 2057, "forward "+id+" after the unpublish")
		}
	})

	apiStatsMetrics(t, srs, lock)
}

// checkPagination checks the pages of a list of n items, which the case made
// and keeps.
func checkPagination(c *check.C, srs *server.Server, kind string, n int) {
	c.Helper()
	all, total := apiList(c, srs, kind, "?count=1000")
	c.Check(total == n && len(all) == n, "%s lists %d of total %d, want %d", kind, len(all), total, n)
	var pages []string
	for start := 0; start < n; start += 5 {
		page, total := apiList(c, srs, kind, "?start="+strconv.Itoa(start)+"&count=5")
		c.CheckEqual(total, n, kind+" total of a page")
		c.CheckEqual(len(page), min(5, n-start), kind+" page at "+strconv.Itoa(start))
		pages = append(pages, ids(page)...)
	}
	c.CheckEqual(pages, ids(all), kind+" pages of 5, against the whole list")
	for q, want := range map[string][]string{
		"":                  ids(all)[:10],
		"?start=" + itoa(n): nil,
		"?count=0":          ids(all)[:1],
		"?count=x":          ids(all)[:1],
		"?start=-3&count=2": ids(all)[:2],
	} {
		page, total := apiList(c, srs, kind, q)
		c.CheckEqual(total, n, kind+q+" total")
		c.CheckEqual(ids(page), want, kind+q)
	}
}

func itoa(n int) string { return strconv.Itoa(n) }

// waitTotal waits until a list has a total of n, for at most 1s, scaled.
func waitTotal(c *check.C, srs *server.Server, kind string, n int) error {
	c.Helper()
	var total int
	wait := check.Scaled(time.Second)
	if !until(wait, func() bool { _, total = apiList(c, srs, kind, "?count=1"); return total == n }) {
		return errorIf(true, "%s total %d after %v, want %d", kind, total, wait, n)
	}
	return nil
}

// untilIdle waits until the server has no stream, client or forward, for at
// most 2s, scaled.
func untilIdle(c *check.C, srs *server.Server) error {
	c.Helper()
	var s, cl, f int
	wait := check.Scaled(2 * time.Second)
	if !until(wait, func() bool {
		_, s = apiList(c, srs, "streams", "?count=1")
		_, cl = apiList(c, srs, "clients", "?count=1")
		_, f = apiList(c, srs, "forwards", "?count=1")
		return s == 0 && cl == 0 && f == 0
	}) {
		return errorIf(true, "%d streams, %d clients and %d forwards after %v", s, cl, f, wait)
	}
	return nil
}

// countsAre reports whether the default vhost has the clients and streams,
// the totals of the lists match them, and so does the stream, when it is
// given, for its clients.
func countsAre(c *check.C, srs *server.Server, streamID string, clients, streams int) bool {
	c.Helper()
	v := defaultVhost(c, srs)
	_, nc := apiList(c, srs, "clients", "?count=1")
	_, ns := apiList(c, srs, "streams", "?count=1")
	ok := num(c, v, "clients") == float64(clients) && num(c, v, "streams") == float64(streams) && nc == clients && ns == streams
	if streamID != "" {
		ok = ok && num(c, apiStreamOf(c, srs, streamID), "clients") == float64(clients)
	}
	return ok
}

// checkCounts checks countsAre, with the counts in the failure.
func checkCounts(c *check.C, srs *server.Server, streamID string, clients, streams int) {
	c.Helper()
	if !countsAre(c, srs, streamID, clients, streams) {
		v := defaultVhost(c, srs)
		c.Errorf("vhost clients %v streams %v, stream clients %v; want %d clients, %d streams", v["clients"], v["streams"], apiStreamOf(c, srs, streamID)["clients"], clients, streams)
	}
}

// metric is the value of a metric with no labels from the exporter.
func metric(c *check.C, srs *server.Server, name string) float64 {
	c.Helper()
	res, err := httpClient.Get("http://" + srs.Addr(server.Exporter) + "/metrics")
	c.Must(err, "GET /metrics")
	defer res.Body.Close()
	c.CheckEqual(res.StatusCode, http.StatusOK, "/metrics status")
	s := bufio.NewScanner(io.LimitReader(res.Body, 1<<20))
	for s.Scan() {
		if v, ok := strings.CutPrefix(s.Text(), name+" "); ok {
			n, err := strconv.ParseFloat(v, 64)
			c.Must(err, "parse "+s.Text())
			return n
		}
	}
	c.Fatalf("no %s in /metrics", name)
	return 0
}

// TestTimer_Api_Bytes checks the byte counts of live clients, which SRS
// samples every 3s, on the shared timer server SegmentsKeep.
func TestTimer_Api_Bytes(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// LiveClientBytes publishes and plays in real time until SRS samples
	// the clients: the publisher's recv_bytes and the player's send_bytes
	// count what they sent and got so far, and the stream at least both.
	check.Case(t, "LiveClientBytes", check.Timer, func(c *check.C) {
		stream := streamName(c)
		pub, pubID, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		player, playID := playRTMPOf(c, srs, stream, nil)
		go drainRTMP(player)
		var cl map[string]any
		c.Check(untilEvery(4*time.Second, 100*time.Millisecond, func() bool {
			cl = apiClientOf(c, srs, pubID)
			return num(c, cl, "recv_bytes") > 0
		}), "the publisher's recv_bytes %v after 4s, want SRS's sample", cl["recv_bytes"])
		recv := uint64(num(c, cl, "recv_bytes"))
		c.Check(recv <= pub.BytesSent(), "publisher recv_bytes %d, more than the %d it sent", recv, pub.BytesSent())
		c.Check(recv >= pub.BytesSent()/2, "publisher recv_bytes %d, less than half the %d it sent", recv, pub.BytesSent())
		sent := uint64(num(c, apiClientOf(c, srs, playID), "send_bytes"))
		c.Check(sent > 0 && sent <= player.BytesReceived(), "player send_bytes %d, want some of the %d it got", sent, player.BytesReceived())
		st := apiStreamOf(c, srs, streamID)
		c.Check(uint64(num(c, st, "recv_bytes")) >= recv, "stream recv_bytes %v, want at least the publisher's %d", st["recv_bytes"], recv)
		c.Check(uint64(num(c, st, "send_bytes")) >= sent, "stream send_bytes %v, want at least the player's %d", st["send_bytes"], sent)
	})
}

// frameCountsWait is how long a case waits for a stream's frame counts: SRS
// reports a WebRTC or RTMP publisher's frames each second, and a margin. The
// cases wait it scaled by the limit scale: a burst of media at once takes a
// slow machine longer to read.
const frameCountsWait = 2 * time.Second

// TestTimer_Api_Frames checks the frame counts of WebRTC and RTMP streams,
// which SRS reports each second, on the shared timer server SegmentsKeep.
func TestTimer_Api_Frames(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// WhipFrameCounts publishes 13s of WHIP at once: within a second the
	// stream counts every one of the 325 video and 650 audio frames, and no
	// more.
	check.Case(t, "WhipFrameCounts", check.Timer, func(c *check.C) {
		h, _ := whipStream(c, srs, streamName(c), 13*time.Second)
		var st map[string]any
		c.Check(untilEvery(check.Scaled(frameCountsWait), 50*time.Millisecond, func() bool {
			st = apiStreamOf(c, srs, h.stream)
			return num(c, st, "video_frames") >= 325 && num(c, st, "audio_frames") >= 650
		}), "stream frames video %v audio %v, want the 325 and 650 sent", st["video_frames"], st["audio_frames"])
		v, a := num(c, st, "video_frames"), num(c, st, "audio_frames")
		c.Check(v <= 325 && a <= 650, "frames video %v audio %v, want at most the 325 and 650 sent", v, a)
		c.CheckEqual(num(c, st, "frames"), v+a, "frames")
	})

	// WhipFrameCountsLag publishes 4s of WHIP at once: within a second the
	// stream counts its 100 video frames, far fewer than a batch of 289.
	check.Case(t, "WhipFrameCountsLag", check.Timer, func(c *check.C) {
		h, _ := whipStream(c, srs, streamName(c), 4*time.Second)
		var st map[string]any
		c.Check(untilEvery(check.Scaled(frameCountsWait), 50*time.Millisecond, func() bool {
			st = apiStreamOf(c, srs, h.stream)
			return num(c, st, "video_frames") >= 100
		}), "stream video_frames %v after 100 frames, want 100", st["video_frames"])
	})

	// RtmpFrameCountsLag publishes 2s of RTMP at once, then in real time:
	// within a second the stream counts the 50 video frames of the lead,
	// long before the first-packet timeout of 20s.
	check.Case(t, "RtmpFrameCountsLag", check.Timer, func(c *check.C) {
		stream := streamName(c)
		startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 2*time.Second)
		streamID := waitHook(c, srs, "on_publish", stream).Field("stream_id")
		var st map[string]any
		c.Check(untilEvery(check.Scaled(frameCountsWait), 50*time.Millisecond, func() bool {
			st = apiStreamOf(c, srs, streamID)
			return num(c, st, "video_frames") >= 50
		}), "stream video_frames %v after 50 frames, want 50 or more", st["video_frames"])
	})
}

// drainRTMP reads an RTMP player until it closes.
func drainRTMP(cl *rtmp.Client) {
	cl.SetDeadline(time.Time{})
	for {
		if _, err := cl.ReadTag(); err != nil {
			return
		}
	}
}

// playOnly plays a stream over RTMP without reading it, until the case ends.
func playOnly(c *check.C, url string) *rtmp.Client {
	c.Helper()
	player := dialRTMP(c, url, nil)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := player.Play(ctx)
	c.Must(err, "play")
	return player
}
