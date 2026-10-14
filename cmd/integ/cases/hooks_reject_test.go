// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/rtsp"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// hookWait is how long a client waits for SRS to answer it: a refusal comes
// within a few ms, and a client that gets no answer in this time is left
// waiting. It is longer on a machine with longer class limits.
func hookWait() time.Duration { return check.Scaled(300 * time.Millisecond) }

// hookDelay is how long a slow hook server takes to accept.
const hookDelay = 150 * time.Millisecond

// A hookResult is what a client sees when it publishes or plays: SRS accepts
// it, refuses it, or keeps it waiting.
type hookResult struct {
	accepted bool
	// refusal is how SRS refused, such as "status 401" or "closed", empty
	// when it didn't.
	refusal string
	// elapsed is from the request until SRS accepted or refused it.
	elapsed time.Duration
}

func (r hookResult) waiting() bool { return !r.accepted && r.refusal == "" }

func (r hookResult) String() string {
	switch {
	case r.accepted:
		return fmt.Sprintf("accepted after %v", r.elapsed)
	case r.refusal != "":
		return fmt.Sprintf("refused (%v) after %v", r.refusal, r.elapsed)
	}
	return "no answer"
}

// A hookTarget is a client of one protocol that publishes or plays and the
// hook SRS asks first.
type hookTarget struct {
	// name is the protocol and what it does, such as RtmpPublish.
	name string
	// hook is the hook SRS asks, such as on_publish.
	hook string
	// prepare, when set, publishes the stream a client plays. A player that
	// is refused or kept waiting plays before the publish, since SRS asks
	// the hook first; only an accepted player needs a stream to see.
	prepare func(c *check.C, srs *server.Server, name string)
	// always runs prepare for every result, for a player SRS refuses only
	// once the stream is there.
	always bool
	// refusal is how SRS refuses: "status 401", or "" for any refusal, such
	// as a close.
	refusal string
	// try publishes or plays the stream, waiting at most d for SRS to accept
	// or refuse it.
	try func(c *check.C, srs *server.Server, name string, d time.Duration) hookResult
}

// A hookKind is how the hook server answers.
type hookKind struct {
	name  string
	reply mock.Reply
	// accept is whether SRS accepts the client, or else refuses it, or keeps
	// it waiting for a reply that never comes.
	accept, wait bool
}

// hookKinds are the hook server's answers: a non-zero code, an HTTP error, a
// slow accept, and no answer at all.
var hookKinds = []hookKind{
	{name: "Rejected", reply: mock.Reject(403)},
	{name: "HttpError", reply: mock.HTTPError(500)},
	{name: "SlowHook", reply: mock.OK().After(hookDelay), accept: true},
	{name: "NoAnswer", reply: mock.NoAnswer(), wait: true},
}

// hooksReject adds the rejection cases of every protocol on the Common server
// to TestHooks_Common: for each client, each answer of the hook server.
func hooksReject(t *testing.T) {
	for _, target := range commonHookTargets() {
		for _, kind := range hookKinds {
			check.Case(t, target.name+kind.name, check.Normal, func(c *check.C) {
				hookRejectCase(c, server.Common(c), target, kind)
			})
		}
	}
}

// hooksRejectRemux adds the rejection cases of an HTTP remux mount's players
// on its shared server.
func hooksRejectRemux(t *testing.T, srs *server.Server, m httpHookMount) {
	target := httpHookTarget(m)
	for _, kind := range hookKinds {
		check.Case(t, target.name+kind.name, check.Normal, func(c *check.C) {
			hookRejectCase(c, srs, target, kind)
		})
	}
}

// hookRejectCase scripts the hook of a stream of the case's own, then has the
// client publish or play it: SRS accepts it once a slow hook accepts,
// refuses it with the protocol's refusal when the hook rejects it or fails,
// and keeps it waiting while the hook doesn't answer. Either way the hook is
// asked once, and SRS still serves its API.
func hookRejectCase(c *check.C, srs *server.Server, target hookTarget, kind hookKind) {
	name := streamName(c)
	srs.Hooks.Script(hookOfStream(target.hook, name), kind.reply)
	if target.prepare != nil && (kind.accept || target.always) {
		target.prepare(c, srs, name)
	}
	d := hookWait()
	if kind.accept {
		d += hookDelay
	}
	r := target.try(c, srs, name, d)
	switch {
	case kind.accept:
		c.Check(r.accepted, "%s, want accepted once the hook accepts", r)
		c.Check(r.elapsed >= hookDelay, "%s, want it after the hook's %v", r, hookDelay)
	case kind.wait:
		c.Check(r.waiting(), "%s, want no answer while the hook doesn't answer", r)
	default:
		checkRefused(c, r, target.refusal)
	}
	c.CheckEqual(len(srs.Hooks.Requests(hookOfStream(target.hook, name))), 1, target.hook+" requests")
	checkVersions(c, srs)
}

// hookOfStream matches a hook of a stream.
func hookOfStream(hook, name string) mock.Match {
	return mock.Match{Action: hook, Stream: name}
}

// checkRefused checks that SRS refused a client, in the way want names, or in
// any way when want is "".
func checkRefused(c *check.C, r hookResult, want string) {
	c.Helper()
	if c.Check(r.refusal != "", "%s, want refused", r) && want != "" {
		c.CheckEqual(r.refusal, want, "refusal")
	}
}

// commonHookTargets are the clients of every protocol on the Common server.
func commonHookTargets() []hookTarget {
	return []hookTarget{
		rtmpHookTarget("RtmpConnect", "on_connect", false, true),
		rtmpHookTarget("RtmpPublish", "on_publish", false, true),
		rtmpHookTarget("RtmpPlay", "on_play", false, false),
		rtmpHookTarget("RtmpsPublish", "on_publish", true, true),
		rtmpHookTarget("RtmpsPlay", "on_play", true, false),
		srtPublishTarget("SrtConnect", "on_connect"),
		srtPublishTarget("SrtPublish", "on_publish"),
		srtPlayTarget(),
		rtcHookTarget(true),
		rtcHookTarget(false),
		httpHookTarget(flvHookMount),
		hlsHookTarget(),
		rtspHookTarget(),
		gbHookTarget(),
	}
}

// rtmpHookTarget is an RTMP or RTMPS client that publishes or plays: SRS
// accepts it with NetStream.Publish.Start or NetStream.Play.Start, and
// refuses it by closing the connection, as SRS does, or with a status of
// level error.
func rtmpHookTarget(name, hook string, tls, publish bool) hookTarget {
	return hookTarget{name: name, hook: hook, try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
		url, opts := rtmpURL(srs, "live/"+stream), (*rtmp.Options)(nil)
		if tls {
			url, opts = rtmpsURL(srs, "live/"+stream), tlsOpts(c)
		}
		return tryRTMP(c, url, opts, publish, d)
	}}
}

// tryRTMP publishes or plays an RTMP or RTMPS URL, for at most d, and returns
// whether SRS accepted it, with NetStream.Publish.Start or
// NetStream.Play.Start, refused it, by closing the connection or with a
// status of level error, or kept it waiting.
func tryRTMP(c *check.C, url string, opts *rtmp.Options, publish bool, d time.Duration) hookResult {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	start := time.Now()
	cl, err := rtmp.Dial(ctx, url, opts)
	c.Must(err, "dial")
	c.Cleanup(func() { cl.Close() })
	if publish {
		_, err = cl.Publish(ctx, rtmp.FMLE)
	} else {
		_, err = cl.Play(ctx)
	}
	r := hookResult{accepted: err == nil, elapsed: time.Since(start)}
	var se *rtmp.StatusError
	if errors.As(err, &se) {
		r.refusal = "status " + se.Code
	} else if err != nil && !isTimeout(err) {
		r.refusal = "closed"
	}
	return r
}

// srtResult reads an SRT connection, for at most d from start, until SRS
// closes it, or accepts it: a player once it reads media, a publisher, which
// reads nothing, once published reports it is.
func srtResult(conn *srt.Conn, start time.Time, d time.Duration, published func() bool) hookResult {
	read, closed := make(chan struct{}), make(chan struct{})
	go func() {
		b := make([]byte, 1500)
		n, err := conn.Read(b)
		if err != nil {
			close(closed)
		} else if n > 0 {
			close(read)
		}
	}()
	deadline := start.Add(d)
	for time.Now().Before(deadline) {
		select {
		case <-closed:
			return hookResult{refusal: "closed", elapsed: time.Since(start)}
		case <-read:
			return hookResult{accepted: true, elapsed: time.Since(start)}
		case <-time.After(5 * time.Millisecond):
		}
		if published != nil && published() {
			return hookResult{accepted: true, elapsed: time.Since(start)}
		}
	}
	return hookResult{}
}

// srtDial calls the SRT listener for at most d: a handshake SRS refuses is a
// refusal; nil when the call timed out.
func srtDial(c *check.C, srs *server.Server, opts srt.Options, start time.Time, d time.Duration) (*srt.Conn, *hookResult) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	conn, err := srt.Dial(ctx, srs.Addr(server.SRT), opts)
	var re *srt.RejectError
	if errors.As(err, &re) {
		return nil, &hookResult{refusal: "rejected", elapsed: time.Since(start)}
	} else if err != nil {
		return nil, &hookResult{}
	}
	c.Cleanup(func() { conn.Close() })
	return conn, nil
}

// srtPublishTarget is an SRT publisher: SRS accepts it once the API lists the
// stream as published, and refuses it by closing the connection after the
// handshake, as SRS does, or in the handshake.
func srtPublishTarget(name, hook string) hookTarget {
	return hookTarget{name: name, hook: hook, try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
		start := time.Now()
		conn, r := srtDial(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, start, d)
		if r != nil {
			return *r
		}
		f, err := os.Open(media.Path(c, media.H264HighAACTS))
		c.Must(err, "open the media")
		c.Cleanup(func() { f.Close() })
		// The lead fits in the connection's buffers, so it is sent whether
		// SRS reads it or not.
		srt.NewSender(f).SendThrough(conn, srtLead)
		return srtResult(conn, start, d, func() bool { return streamPublishing(c, srs, "live/"+stream) })
	}}
}

// srtPlayTarget is an SRT player: SRS accepts it once media comes, from an
// SRT publisher of the stream, and refuses it as it refuses a publisher.
func srtPlayTarget() hookTarget {
	return hookTarget{
		name: "SrtPlay", hook: "on_play",
		prepare: func(c *check.C, srs *server.Server, stream string) {
			startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		},
		try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
			start := time.Now()
			conn, r := srtDial(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, start, d)
			if r != nil {
				return *r
			}
			return srtResult(conn, start, d, nil)
		},
	}
}

// rtcHookTarget is a WHIP publisher or a WHEP player, which only posts its
// offer: SRS accepts it with 201 and refuses it with 401.
func rtcHookTarget(publish bool) hookTarget {
	name, hook := "WhepPlay", "on_play"
	if publish {
		name, hook = "WhipPublish", "on_publish"
	}
	return hookTarget{name: name, hook: hook, refusal: "status 401", try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
		url := whepURL(srs, "live", stream)
		if publish {
			url = whipURL(srs, "live", stream)
		}
		offer := rtcOffer(c, publish, []string{"H264"}, []string{"opus"})
		ctx, cancel := context.WithTimeout(context.Background(), d)
		defer cancel()
		start := time.Now()
		s, err := rtc.Post(ctx, url, offer, rtcOptions(nil))
		r := hookResult{accepted: err == nil, elapsed: time.Since(start)}
		rtcDeleteLater(c, s, err)
		var se *rtc.StatusError
		if errors.As(err, &se) {
			r.refusal = fmt.Sprintf("status %d", se.Code)
		}
		return r
	}}
}

// httpHookTarget is an HTTP player of a remux mount: SRS accepts it with 200
// once the stream is published, and refuses it with 401.
func httpHookTarget(m httpHookMount) hookTarget {
	return hookTarget{
		name: m.name + "Play", hook: "on_play", refusal: "status 401",
		prepare: func(c *check.C, srs *server.Server, stream string) {
			startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, m.file), 200*time.Millisecond)
		},
		try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
			start := time.Now()
			p := remuxPlayerOf(c, srs, "live/"+stream+"."+m.ext, nil)
			return httpResult(p, start, d)
		},
	}
}

// httpResult waits at most d from start for the answer of an HTTP player.
func httpResult(p responder, start time.Time, d time.Duration) hookResult {
	p.SetDeadline(start.Add(d))
	defer p.SetDeadline(time.Time{})
	res, err := p.Response()
	r := hookResult{elapsed: time.Since(start)}
	if err == nil {
		r.accepted = res.StatusCode == http.StatusOK
		if !r.accepted {
			r.refusal = fmt.Sprintf("status %d", res.StatusCode)
		}
	} else if !isTimeout(err) {
		r.refusal = "closed"
	}
	return r
}

// hlsHookTarget is an HLS player that starts a session: SRS asks on_play
// only for a stream it has a playlist of, so the stream is published first,
// one segment at once. SRS accepts the session with its master playlist and
// refuses it with 401.
func hlsHookTarget() hookTarget {
	return hookTarget{
		name: "HlsPlay", hook: "on_play", refusal: "status 401", always: true,
		prepare: func(c *check.C, srs *server.Server, stream string) {
			publishThrough(c, srs, "live/"+stream, media.H264HighAAC, 11*time.Second)
			c.Must(untilFile(filepath.Join(htmlDir(srs), "live", stream+".m3u8"), time.Second), "the playlist")
		},
		try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
			ctx, cancel := context.WithTimeout(context.Background(), d)
			defer cancel()
			req, err := http.NewRequestWithContext(ctx, http.MethodGet, flvURL(srs, "live/"+stream+".m3u8"), nil)
			c.Must(err, "request")
			start := time.Now()
			res, err := httpClient.Do(req)
			r := hookResult{elapsed: time.Since(start)}
			if err == nil {
				res.Body.Close()
				r.accepted = res.StatusCode == http.StatusOK
				if !r.accepted {
					r.refusal = fmt.Sprintf("status %d", res.StatusCode)
				}
			} else if !isTimeout(err) {
				r.refusal = "closed"
			}
			return r
		},
	}
}

// rtspHookTarget is an RTSP player: SRS asks on_play at DESCRIBE, before it
// looks for the stream, accepts the player with the stream's tracks once it
// is published, and refuses it with 401 to DESCRIBE.
func rtspHookTarget() hookTarget {
	return hookTarget{
		name: "RtspPlay", hook: "on_play", refusal: "status 401",
		prepare: func(c *check.C, srs *server.Server, stream string) {
			rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		},
		try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
			ctx, cancel := context.WithTimeout(context.Background(), d)
			defer cancel()
			start := time.Now()
			p, err := rtsp.Play(ctx, rtspURL(srs, "live/"+stream), nil)
			r := hookResult{accepted: err == nil, elapsed: time.Since(start)}
			if p != nil {
				c.Cleanup(p.Close)
			}
			var se *rtsp.StatusError
			if errors.As(err, &se) {
				r.refusal = fmt.Sprintf("status %d", se.Code)
			}
			return r
		},
	}
}

// gbHookTarget is a GB28181 device that sends media of a session: SRS asks
// the on_publish of the RTMP client it publishes the stream with. The device
// sees nothing either way; SRS accepts the stream once the API lists it as
// published, and refuses it when the API no longer lists it, while a stream
// whose hook doesn't answer stays listed, not published.
func gbHookTarget() hookTarget {
	return hookTarget{name: "GbPublish", hook: "on_publish", refusal: "not published", try: func(c *check.C, srs *server.Server, stream string, d time.Duration) hookResult {
		s := gbStartID(c, srs, media.H264BaselineAAC, stream, gbSSRC())
		start := time.Now()
		s.sendThrough(c, gbLead)
		// SRS lists the stream before it asks the hook, so a stream it no
		// longer lists once the mock was asked is refused; a hook that is
		// down never reaches the mock, so a stream not listed for a while is
		// refused too.
		asked := func() bool {
			return len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})) > 0
		}
		deadline := start.Add(d)
		for {
			published, listed := streamState(c, srs, "live/"+stream)
			switch {
			case published:
				return hookResult{accepted: true, elapsed: time.Since(start)}
			case !listed && (asked() || time.Since(start) > d/2):
				return hookResult{refusal: "not published", elapsed: time.Since(start)}
			case time.Now().After(deadline):
				return hookResult{}
			}
			time.Sleep(5 * time.Millisecond)
		}
	}}
}

// streamState is whether the API lists a stream path, and whether as
// published.
func streamState(c *check.C, srs *server.Server, stream string) (published, listed bool) {
	c.Helper()
	var v struct {
		Streams []struct {
			URL     string `json:"url"`
			Publish struct {
				Active bool `json:"active"`
			} `json:"publish"`
		} `json:"streams"`
	}
	apiGet(c, srs, "/api/v1/streams/?count=1000", &v)
	for _, s := range v.Streams {
		if s.URL == "/"+stream {
			return s.Publish.Active, true
		}
	}
	return false, false
}

// TestHooks_Down checks what each protocol's publisher and player sees when
// the hook server of on_publish and on_play is down: SRS refuses it as it
// refuses one the hook rejects, and goes on. Nothing can publish there, so
// every player plays before a publish, which SRS asks the hook about first;
// an HLS player, which SRS asks only once a stream has a playlist, has no
// case. The other hooks call the mock.
func TestHooks_Down(t *testing.T) {
	t.Parallel()
	cfg := server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI, server.HTTPServer, server.RTMPS},
		Hooks:     true,
		HookURLs:  map[string][]string{"on_publish": {server.HookDown}, "on_play": {server.HookDown}},
		Env: map[string]string{
			"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
			"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream].flv",
		},
	}
	cfg = server.WithRTC(cfg, server.RTC{})
	cfg = server.WithSRT(cfg, server.SRTOptions{})
	cfg = server.WithRTSP(cfg, server.RTSPOptions{})
	cfg = server.WithGB(cfg, server.GBOptions{})
	srs := server.Group(t, cfg)

	for _, target := range commonHookTargets() {
		if target.hook == "on_connect" || target.always {
			continue
		}
		check.Case(t, target.name, check.Normal, func(c *check.C) {
			name := streamName(c)
			checkRefused(c, target.try(c, srs, name, hookWait()), target.refusal)
			c.CheckEqual(len(srs.Hooks.Requests(hookOfStream(target.hook, name))), 0, target.hook+" requests on the mock")
			checkVersions(c, srs)
		})
	}
}

// TestHooks_Several checks hooks with two URLs each, /hooks and /more on the
// mock: SRS calls them in order, with the same body, and a client is
// accepted only when both accept. Players get each message at once, as on
// Common, so SRS sees a player's close at once.
func TestHooks_Several(t *testing.T) {
	t.Parallel()
	both := []string{"/hooks", "/more"}
	srs := server.Group(t, server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI},
		Hooks:     true,
		HookURLs: map[string][]string{
			"on_connect": both, "on_close": both, "on_publish": both,
			"on_unpublish": both, "on_play": both, "on_stop": both,
		},
		Env: map[string]string{"SRS_VHOST_MIN_LATENCY": "on", "SRS_VHOST_PLAY_MW_LATENCY": "0"},
	})

	// BothInOrder publishes and plays, then closes both: each hook of each
	// client is sent to /hooks, then to /more, with the same body. The
	// stream has media, so SRS sees the player's close at its next send.
	check.Case(t, "BothInOrder", check.Normal, func(c *check.C) {
		name := streamName(c)
		pub := startPublisher(c, rtmpURL(srs, "live/"+name), media.Path(c, media.H264HighAAC), 200*time.Millisecond)
		pubID := waitHook(c, srs, "on_publish", name).Field("client_id")
		player := dialRTMP(c, rtmpURL(srs, "live/"+name), nil)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Play(ctx)
		c.Must(err, "play")
		playID := waitHook(c, srs, "on_play", name).Field("client_id")
		player.Close()
		waitHookCountOf(c, srs, mock.Match{Action: "on_close", Func: func(r mock.Request) bool { return r.Field("client_id") == playID }}, 2)
		pub.Close()
		waitHookCountOf(c, srs, mock.Match{Action: "on_close", Func: func(r mock.Request) bool { return r.Field("client_id") == pubID }}, 2)

		for id, actions := range map[string][]string{
			pubID:  {"on_connect", "on_publish", "on_unpublish", "on_close"},
			playID: {"on_connect", "on_play", "on_stop", "on_close"},
		} {
			hooks := clientHooks(srs, id)
			var got []string
			for _, h := range hooks {
				got = append(got, h.Path)
			}
			var want []string
			for _, a := range actions {
				want = append(want, "/hooks/"+a, "/more/"+a)
			}
			c.CheckEqual(got, want, "hook paths of client "+id)
			for i := 0; i+1 < len(hooks); i += 2 {
				c.CheckEqual(string(hooks[i+1].Body), string(hooks[i].Body), hooks[i].Action()+" body of /more, the same as of /hooks")
			}
		}
	})

	// refused publishes or plays a stream whose hook at path rejects it: the
	// client is refused, and SRS asks the URLs in order up to that one.
	refused := func(c *check.C, hook, path string, reply mock.Reply, publish bool) {
		name := streamName(c)
		srs.Hooks.Script(mock.Match{Path: path + "/" + hook, Stream: name}, reply)
		target := rtmpHookTarget("", hook, false, publish)
		checkRefused(c, target.try(c, srs, name, hookWait()), "")
		var got []string
		for _, h := range srs.Hooks.Requests(mock.Match{Action: hook, Stream: name}) {
			got = append(got, h.Path)
		}
		want := []string{"/hooks/" + hook}
		if path == "/more" {
			want = append(want, "/more/"+hook)
		}
		c.CheckEqual(got, want, hook+" requests")
	}

	// FirstRejects has the first on_publish URL reject: the publisher is
	// refused, and the second URL is never asked.
	check.Case(t, "FirstRejects", check.Normal, func(c *check.C) {
		refused(c, "on_publish", "/hooks", mock.Reject(403), true)
	})

	// SecondRejects has the second on_publish URL reject, after the first
	// accepted: the publisher is refused.
	check.Case(t, "SecondRejects", check.Normal, func(c *check.C) {
		refused(c, "on_publish", "/more", mock.Reject(403), true)
	})

	// SecondPlayHttpError has the second on_play URL answer 500: the player
	// is refused.
	check.Case(t, "SecondPlayHttpError", check.Normal, func(c *check.C) {
		refused(c, "on_play", "/more", mock.HTTPError(500), false)
	})
}
