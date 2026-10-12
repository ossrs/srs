// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/hls"
	"srs-integ/internal/httpplay"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// An httpHookMount is an HTTP remux mount whose players the hook cases check:
// the prefix of its case names, its extension, the test file that has its
// codecs, and when a player has seen it play.
type httpHookMount struct {
	name, ext string
	file      string
	until     report.Until
}

var (
	flvHookMount = httpHookMount{"HttpFlv", "flv", media.H264HighAAC, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1}}
	tsHookMount  = httpHookMount{"HttpTs", "ts", tsMount.file, tsMount.until}
	mp3HookMount = httpHookMount{"HttpMp3", "mp3", mp3Mount.file, mp3Mount.until}
	aacHookMount = httpHookMount{"HttpAac", "aac", aacMount.file, aacMount.until}
)

// forwardedFor is the header of a proxy in front of SRS; its first address
// is the client's.
var forwardedFor = http.Header{"X-Forwarded-For": {"10.1.2.3, 10.0.0.1"}}

// playerReferer is the page of a player, which an HTTP player sends as its
// Referer and SRS reports as the pageUrl.
const playerReferer = "http://player.example/page"

// hooksHTTP adds the hook payload cases of HTTP players to TestHooks_Common:
// on_play and on_stop of HTTP-FLV, over HTTP and HTTPS, the on_play of an HLS
// session and of a DASH player, the client IP from X-Forwarded-For, and the
// on_hls and on_hls_notify of each HLS segment.
func hooksHTTP(t *testing.T) {
	httpPlayerHooks(t, func(c *check.C) *server.Server { return server.Common(c) }, flvHookMount, true)

	// HlsOnPlay starts an HLS session with a param and a Referer: one
	// on_play, with every field; the session's hls_ctx is its client_id and a
	// client of the API, stream_id the publisher's, clients 2.
	check.Case(t, "HlsOnPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		pub := publishHLSHooked(c, srs, name, "")
		ctx := hlsSession(c, srs, name, "?k=v", http.Header{"Referer": {playerReferer}})
		play := waitClientHook(c, srs, "on_play", ctx)
		checkHookFields(c, srs, []mock.Request{play}, "127.0.0.1")
		c.CheckEqual(play.Field("param"), "?k=v", "on_play param")
		c.CheckEqual(play.Field("pageUrl"), playerReferer, "on_play pageUrl")
		c.CheckEqual(play.Field("stream_url"), "/live/"+name, "on_play stream_url")
		c.CheckEqual(play.Field("stream_id"), pub.Field("stream_id"), "on_play stream_id, the publisher's")
		c.CheckEqual(play.Field("clients"), "2", "on_play clients: the publisher and the session")
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+ctx), 0, "the API code of the session's client")
	})

	// HlsOnPlayStreamName starts an HLS session: its on_play names the
	// stream as every other player's does, without the playlist's extension.
	check.Case(t, "HlsOnPlayStreamName", check.Normal, func(c *check.C) {
		check.KnownBug(c, "the on_play of an HLS session names the stream with the playlist's .m3u8 extension")
		srs := server.Common(c)
		name := streamName(c)
		publishHLSHooked(c, srs, name, "")
		ctx := hlsSession(c, srs, name, "", nil)
		c.CheckEqual(waitClientHook(c, srs, "on_play", ctx).Field("stream"), name, "on_play stream")
	})

	// HlsForwardedFor starts an HLS session through a proxy that sets
	// X-Forwarded-For: the first address is the session's IP, in on_play
	// and in the API.
	check.Case(t, "HlsForwardedFor", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		publishHLSHooked(c, srs, name, "")
		ctx := hlsSession(c, srs, name, "", forwardedFor)
		c.CheckEqual(waitClientHook(c, srs, "on_play", ctx).Field("ip"), "10.1.2.3", "on_play ip")
		checkClientIP(c, srs, ctx, "10.1.2.3")
	})

	// OnHls publishes 20s with a param: on_hls for each of the 2 segments
	// it closes, in order, with every field; the publisher's client and
	// stream ids, its param and tcUrl; the segment's duration, sequence
	// number and file, which is on disk under cwd and served at url; the
	// playlist's file and URL.
	check.Case(t, "OnHls", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		pub := publishHLSHooked(c, srs, name, "?token=abc")
		hooks := waitHookCountOf(c, srs, mock.Match{Action: "on_hls", Stream: name}, 2)
		ids := apiIDs(c, srs)
		for i, h := range hooks {
			what := "on_hls " + strconv.Itoa(i)
			for _, k := range []string{"server_id", "service_id", "action", "client_id", "ip", "vhost", "app", "tcUrl", "stream", "param",
				"duration", "cwd", "file", "url", "m3u8", "m3u8_url", "seq_no", "stream_url", "stream_id"} {
				_, ok := h.JSON[k]
				c.Check(ok, "%s has no %s: %s", what, k, h.Body)
			}
			c.CheckEqual(h.Field("server_id"), ids.Server, what+" server_id, the API's server")
			c.CheckEqual(h.Field("service_id"), ids.Service, what+" service_id, the API's service")
			c.CheckEqual(h.Field("client_id"), pub.Field("client_id"), what+" client_id, the publisher's")
			c.CheckEqual(h.Field("ip"), "127.0.0.1", what+" ip")
			c.CheckEqual(h.Field("vhost"), "__defaultVhost__", what+" vhost")
			c.CheckEqual(h.Field("app"), "live", what+" app")
			c.CheckEqual(h.Field("tcUrl"), pub.Field("tcUrl"), what+" tcUrl, the publisher's")
			c.CheckEqual(h.Field("param"), "?token=abc", what+" param, the publisher's")
			c.CheckEqual(h.Field("stream_url"), "/live/"+name, what+" stream_url")
			c.CheckEqual(h.Field("stream_id"), pub.Field("stream_id"), what+" stream_id, the publisher's")
			c.CheckEqual(h.Field("seq_no"), strconv.Itoa(i), what+" seq_no")
			d, err := strconv.ParseFloat(h.Field("duration"), 64)
			c.CheckNoError(err, what+" duration")
			c.Check(d >= 9.95 && d <= 10.05, "%s duration %v, want the 10s of a segment", what, d)
			c.CheckEqual(h.Field("m3u8_url"), "live/"+name+".m3u8", what+" m3u8_url")
			for _, k := range []string{"file", "m3u8"} {
				_, err := os.Stat(filepath.Join(h.Field("cwd"), h.Field(k)))
				c.CheckNoError(err, what+" "+k+" under cwd")
			}
			res, body := httpGet(c, httpClient, flvURL(srs, h.Field("url")))
			c.CheckEqual(res.StatusCode, 200, what+" status of url "+h.Field("url"))
			c.Check(isTS(body), "%s url %v is not a TS segment", what, h.Field("url"))
		}
	})

	// OnHlsNotify publishes 20s with a param: a GET of on_hls_notify for
	// each of the 2 segments it closes, in order, with every variable filled
	// in: the server's ids, the app, the stream, the publisher's param, and
	// the segment's URL, which is served beside the playlist.
	check.Case(t, "OnHlsNotify", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		publishHLSHooked(c, srs, name, "?token=abc")
		hooks := waitHookCountOf(c, srs, mock.Match{Action: "on_hls_notify", Stream: name}, 2)
		ids := apiIDs(c, srs)
		for i, h := range hooks {
			what := "on_hls_notify " + strconv.Itoa(i)
			c.CheckEqual(h.Method, http.MethodGet, what+" method")
			c.CheckEqual(h.Field("server_id"), ids.Server, what+" server_id, the API's server")
			c.CheckEqual(h.Field("service_id"), ids.Service, what+" service_id, the API's service")
			c.CheckEqual(h.Field("app"), "live", what+" app")
			c.CheckEqual(h.Field("param"), "?token=abc", what+" param, the publisher's")
			c.CheckEqual(h.Field("ts_url"), name+"-"+strconv.Itoa(i)+".ts", what+" ts_url")
			res, body := httpGet(c, httpClient, flvURL(srs, "live/"+h.Field("ts_url")))
			c.CheckEqual(res.StatusCode, 200, what+" status of ts_url "+h.Field("ts_url"))
			c.Check(isTS(body), "%s ts_url %v is not a TS segment", what, h.Field("ts_url"))
		}
	})

	// DashOnPlay plays a stream's MPD with a param: like an HLS session, a
	// DASH player must get an on_play, so a hook server can authorize and
	// count it.
	check.Case(t, "DashOnPlay", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a DASH player gets no on_play: SRS serves the MPD and segments as static files")
		srs := server.Common(c)
		name := streamName(c)
		publishThrough(c, srs, "live/"+name, media.H264HighAAC, 20*time.Second)
		c.Must(untilFile(filepath.Join(htmlDir(srs), "live", name+".mpd"), time.Second), "the MPD")
		res, _ := httpGet(c, httpClient, flvURL(srs, "live/"+name+".mpd?k=v"))
		c.CheckEqual(res.StatusCode, 200, "status of the MPD")
		ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
		defer cancel()
		_, err := srs.Hooks.Wait(ctx, mock.Match{Action: "on_play", Func: func(r mock.Request) bool {
			return r.Stream() == name || r.Stream() == name+".mpd"
		}})
		c.CheckNoError(err, "on_play of the DASH player")
	})
}

// TestHooks_TsMount checks on_play and on_stop of HTTP-TS players, and their rejection, over HTTP
// and HTTPS, on the shared TsMount server, which has the hook mock.
func TestHooks_TsMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsMount", sharedTsMountConfig())
	httpPlayerHooks(t, func(*check.C) *server.Server { return srs }, tsHookMount, true)
	hooksRejectRemux(t, srs, tsHookMount)
}

// TestHooks_Mp3Mount checks on_play and on_stop of HTTP MP3 players, on the
// shared Mp3Mount server, which has the hook mock.
func TestHooks_Mp3Mount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Mp3Mount", sharedMp3MountConfig())
	httpPlayerHooks(t, func(*check.C) *server.Server { return srs }, mp3HookMount, false)
	hooksRejectRemux(t, srs, mp3HookMount)
}

// TestHooks_AacMount checks on_play and on_stop of HTTP AAC players, on the
// shared AacMount server, which has the hook mock.
func TestHooks_AacMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "AacMount", sharedAacMountConfig())
	httpPlayerHooks(t, func(*check.C) *server.Server { return srs }, aacHookMount, false)
	hooksRejectRemux(t, srs, aacHookMount)
}

// httpPlayerHooks adds the hook cases of an HTTP remux mount's players, each
// named after the mount: PlayerEvents, ForwardedFor and UnpublishStops, and,
// for a server with an HTTPS server, HttpsPlayerEvents. Each publishes the
// mount's file in real time, joining 200ms in, because the audio mounts have
// no cache for a stream that is held.
func httpPlayerHooks(t *testing.T, srsOf func(*check.C) *server.Server, m httpHookMount, https bool) {
	// PlayerEvents plays with a param and a Referer, then closes while the
	// stream goes on: on_play, then on_stop, and no other hook of the
	// client, with every field; the client_id is a client of the API, the
	// pageUrl the Referer, stream_id the publisher's, and clients 2 in both,
	// as for an RTMP player.
	check.Case(t, m.name+"PlayerEvents", check.Normal, func(c *check.C) {
		httpPlayerEvents(c, srsOf(c), m, &httpplay.Options{Header: http.Header{"Referer": {playerReferer}}})
	})

	if https {
		// HttpsPlayerEvents is PlayerEvents from the HTTPS server.
		check.Case(t, "Https"+m.name[len("Http"):]+"PlayerEvents", check.Normal, func(c *check.C) {
			opts := httpsOpts(c)
			opts.Header = http.Header{"Referer": {playerReferer}}
			httpPlayerEvents(c, srsOf(c), m, opts)
		})
	}

	// ForwardedFor plays through a proxy that sets X-Forwarded-For: the
	// first address is the client's IP in on_play and in on_stop.
	check.Case(t, m.name+"ForwardedFor", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		name := streamName(c)
		startPublisher(c, rtmpURL(srs, "live/"+name), media.Path(c, m.file), 200*time.Millisecond)
		p := remuxPlayerOf(c, srs, "live/"+name+"."+m.ext, &httpplay.Options{Header: forwardedFor})
		c.Must(p.readUntil(m.until, time.Second), "play")
		id := waitHook(c, srs, "on_play", name).Field("client_id")
		p.close()
		for _, h := range []mock.Request{waitClientHook(c, srs, "on_play", id), waitClientHook(c, srs, "on_stop", id)} {
			c.CheckEqual(h.Field("ip"), "10.1.2.3", h.Action()+" ip")
		}
	})

	// UnpublishStops unpublishes a stream with a player: SRS ends the
	// player's stream, and its on_stop comes, while the player is still open;
	// SRS sends it and the on_unpublish at the same time, in no fixed order.
	check.Case(t, m.name+"UnpublishStops", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		name := streamName(c)
		pub := startPublisher(c, rtmpURL(srs, "live/"+name), media.Path(c, m.file), 200*time.Millisecond)
		p := remuxPlayerOf(c, srs, "live/"+name+"."+m.ext, nil)
		c.Must(p.readUntil(m.until, time.Second), "play")
		id := waitHook(c, srs, "on_play", name).Field("client_id")
		closed := time.Now()
		pub.Close()
		waitHook(c, srs, "on_unpublish", name)
		c.CheckNoError(p.untilEnds(time.Second), "the player's stream ends")
		stop := waitClientHook(c, srs, "on_stop", id)
		c.Check(stop.Time.After(closed), "on_stop at %v, before the unpublish at %v", stop.Time, closed)
	})
}

// httpPlayerEvents plays a mount's stream with a param, then closes, and
// checks every hook of the player.
func httpPlayerEvents(c *check.C, srs *server.Server, m httpHookMount, opts *httpplay.Options) {
	name := streamName(c)
	startPublisher(c, rtmpURL(srs, "live/"+name), media.Path(c, m.file), 200*time.Millisecond)
	pub := waitHook(c, srs, "on_publish", name)
	p := remuxPlayerOf(c, srs, "live/"+name+"."+m.ext+"?k=v", opts)
	c.Must(p.readUntil(m.until, time.Second), "play")
	id := waitHook(c, srs, "on_play", name).Field("client_id")
	checkAPIClient(c, srs, id, name)
	p.close()
	waitClientHook(c, srs, "on_stop", id)

	hooks := clientHooks(srs, id)
	c.CheckEqual(hookActions(hooks), []string{"on_play", "on_stop"}, "player hooks")
	checkHookFields(c, srs, hooks, "127.0.0.1")
	for _, h := range hooks {
		c.CheckEqual(h.Field("stream"), name, h.Action()+" stream")
		c.CheckEqual(h.Field("param"), "?k=v", h.Action()+" param")
		c.CheckEqual(h.Field("stream_url"), "/live/"+name, h.Action()+" stream_url")
		c.CheckEqual(h.Field("stream_id"), pub.Field("stream_id"), h.Action()+" stream_id, the publisher's")
		c.CheckEqual(h.Field("clients"), "2", h.Action()+" clients: the publisher and the player")
	}
	c.CheckEqual(hookOf(hooks, "on_play")[0].Field("pageUrl"), playerReferer, "on_play pageUrl")
}

// checkHookFields checks every hook of a client has its fields, and the
// fields they share: the server's ids, the client's id and IP, the default
// vhost and the app.
func checkHookFields(c *check.C, srs *server.Server, hooks []mock.Request, ip string) {
	c.Helper()
	checkHookFieldsExcept(c, srs, hooks, ip, nil)
}

// checkHookFieldsExcept is checkHookFields, but a hook may lack the fields
// that skip lists for its action.
func checkHookFieldsExcept(c *check.C, srs *server.Server, hooks []mock.Request, ip string, skip map[string][]string) {
	c.Helper()
	ids := apiIDs(c, srs)
	for _, h := range hooks {
		for _, k := range rtmpHookFields[h.Action()] {
			if slices.Contains(skip[h.Action()], k) {
				continue
			}
			_, ok := h.JSON[k]
			c.Check(ok, "%s has no %s: %s", h.Action(), k, h.Body)
		}
		c.CheckEqual(h.Field("server_id"), ids.Server, h.Action()+" server_id, the API's server")
		c.CheckEqual(h.Field("service_id"), ids.Service, h.Action()+" service_id, the API's service")
		c.CheckEqual(h.Field("client_id"), hooks[0].Field("client_id"), h.Action()+" client_id")
		c.CheckEqual(h.Field("ip"), ip, h.Action()+" ip")
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", h.Action()+" vhost")
		c.CheckEqual(h.Field("app"), "live", h.Action()+" app")
	}
}

// serverIDs are the ids of a server in its API.
type serverIDs struct {
	Server  string `json:"server"`
	Service string `json:"service"`
}

// apiIDs gets the server's ids from its API.
func apiIDs(c *check.C, srs *server.Server) serverIDs {
	c.Helper()
	var ids serverIDs
	apiGet(c, srs, "/api/v1/versions", &ids)
	return ids
}

// checkClientIP checks the IP of a client in the API.
func checkClientIP(c *check.C, srs *server.Server, id, ip string) {
	c.Helper()
	var v struct {
		Client struct {
			IP string `json:"ip"`
		} `json:"client"`
	}
	apiGet(c, srs, "/api/v1/clients/"+id, &v)
	c.CheckEqual(v.Client.IP, ip, "the client's ip in the API")
}

// publishHLSHooked publishes a stream with a param, 10s for each of the 2
// segments it closes, at once, then waits for its playlist on disk. It
// returns the on_publish of the publisher.
func publishHLSHooked(c *check.C, srs *server.Server, name, param string) mock.Request {
	c.Helper()
	publishThrough(c, srs, "live/"+name+param, media.H264HighAAC, 20*time.Second)
	pub := waitHook(c, srs, "on_publish", name)
	c.Must(untilFile(filepath.Join(htmlDir(srs), "live", name+".m3u8"), time.Second), "the playlist")
	return pub
}

// hlsSession starts an HLS session of a stream with a query q and headers, and
// returns its hls_ctx, from the master playlist.
func hlsSession(c *check.C, srs *server.Server, name, q string, header http.Header) string {
	c.Helper()
	raw := flvURL(srs, "live/"+name+".m3u8"+q)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, raw, nil)
	c.Must(err, "request "+raw)
	for k, v := range header {
		req.Header[k] = v
	}
	res, err := httpClient.Do(req)
	c.Must(err, "GET "+raw)
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	c.Must(err, "read "+raw)
	c.CheckEqual(res.StatusCode, 200, "status of "+raw)
	base, _ := url.Parse(raw)
	pl, err := hls.Parse(body, base)
	c.Must(err, "parse "+raw)
	if !c.Check(pl.Master && len(pl.Variants) == 1, "playlist %+v, want a master playlist with one variant", pl) {
		c.FailNow()
	}
	id := query(c, pl.Variants[0].URI).Get("hls_ctx")
	c.Check(id != "", "media playlist %v, want an hls_ctx", pl.Variants[0].URI)
	return id
}

// TestTimer_Hooks_HlsSession checks the on_stop of an HLS session, which
// comes when the session ends, on the shared timer server SegmentsKeep, with
// hls_window 0.5s.
func TestTimer_Hooks_HlsSession(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// OnStopPayload starts a session with a param through a proxy, fetches
	// its media playlist once, and waits for it to end: on_play, then
	// on_stop, with every field; the hls_ctx as client_id, the proxy's
	// client IP, the param, stream_id the publisher's, and clients 2 while
	// the session still counts.
	check.Case(t, "OnStopPayload", check.Timer, func(c *check.C) {
		name := streamName(c)
		publishThrough(c, srs, "live/"+name, media.H264HighAAC, 2*time.Second)
		pub := waitHook(c, srs, "on_publish", name)
		c.Must(untilFile(filepath.Join(htmlDir(srs), "live", name+".m3u8"), time.Second), "the playlist")
		ctx := hlsSession(c, srs, name, "?k=v", forwardedFor)
		untilClientGone(c, srs, ctx)
		stop := waitSessionHook(c, srs, "on_stop", ctx)

		hooks := clientHooks(srs, ctx)
		c.CheckEqual(hookActions(hooks), []string{"on_play", "on_stop"}, "session hooks")
		checkHookFields(c, srs, hooks, "10.1.2.3")
		c.CheckEqual(stop.Field("param"), "?k=v", "on_stop param")
		c.CheckEqual(stop.Field("stream_url"), "/live/"+name, "on_stop stream_url")
		c.CheckEqual(stop.Field("stream_id"), pub.Field("stream_id"), "on_stop stream_id, the publisher's")
		c.CheckEqual(stop.Field("clients"), "2", "on_stop clients: the publisher and the session")
	})
}
