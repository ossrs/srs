// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"

	"github.com/pion/webrtc/v4"
)

// The fields of the vhosts, streams, clients and forwards endpoints.
var (
	vhostFields  = []string{"id", "name", "enabled", "clients", "streams", "send_bytes", "recv_bytes", "kbps", "hls"}
	streamFields = []string{
		"id", "name", "vhost", "app", "tcUrl", "url", "live_ms", "alive", "clients", "frames", "audio_frames",
		"video_frames", "send_bytes", "recv_bytes", "kbps", "publish", "video", "audio",
	}
	clientFields = []string{
		"id", "vhost", "stream", "ip", "pageUrl", "swfUrl", "tcUrl", "url", "name", "type", "publish", "alive",
		"send_bytes", "recv_bytes", "kbps",
	}
	kbpsFields    = []string{"recv_30s", "send_30s"}
	forwardFields = []string{"id", "stream", "vhost", "app", "name", "url", "state", "state_at"}
)

// apiClientOf is a client of the API, by its id.
func apiClientOf(c *check.C, srs *server.Server, id string) map[string]any {
	c.Helper()
	return object(c, apiObject(c, srs, "/api/v1/clients/"+id), "client")
}

// apiStreamOf is a stream of the API, by its id.
func apiStreamOf(c *check.C, srs *server.Server, id string) map[string]any {
	c.Helper()
	return object(c, apiObject(c, srs, "/api/v1/streams/"+id), "stream")
}

// apiList is the items of a list endpoint, such as streams, with a query, and
// its total.
func apiList(c *check.C, srs *server.Server, kind, q string) ([]map[string]any, int) {
	c.Helper()
	v := apiObject(c, srs, "/api/v1/"+kind+"/"+q)
	items, ok := v[kind].([]any)
	if !ok {
		c.Fatalf("%s is %v, want a list", kind, v[kind])
	}
	var out []map[string]any
	for _, it := range items {
		o, ok := it.(map[string]any)
		if !ok {
			c.Fatalf("an item of %s is %v, want an object", kind, it)
		}
		out = append(out, o)
	}
	total := -1
	if n, ok := v["total"].(float64); ok {
		total = int(n)
	}
	return out, total
}

// ids is the id of each item.
func ids(items []map[string]any) []string {
	var out []string
	for _, it := range items {
		s, _ := it["id"].(string)
		out = append(out, s)
	}
	return out
}

// itemOf is the item of a list with an id, or nil.
func itemOf(items []map[string]any, id string) map[string]any {
	for _, it := range items {
		if it["id"] == id {
			return it
		}
	}
	return nil
}

// defaultVhost is the default vhost of the API's vhosts.
func defaultVhost(c *check.C, srs *server.Server) map[string]any {
	c.Helper()
	vhosts, _ := apiList(c, srs, "vhosts", "")
	for _, v := range vhosts {
		if v["name"] == "__defaultVhost__" {
			return v
		}
	}
	c.Fatalf("no __defaultVhost__ in %v", vhosts)
	return nil
}

// apiKick deletes a client by the API.
func apiKick(c *check.C, srs *server.Server, id string) apiReply {
	c.Helper()
	req, err := http.NewRequest(http.MethodDelete, srs.APIURL("/api/v1/clients/"+id), nil)
	c.Must(err, "request")
	return apiDo(c, httpClient, req)
}

// checkKicked checks the answer of a DELETE of a client: 200 with code 0.
func checkKicked(c *check.C, r apiReply) {
	c.Helper()
	c.CheckEqual(r.status, http.StatusOK, "DELETE status")
	c.Check(strings.Contains(string(r.body), `"code":0`), "DELETE answer %s, want code 0", r.body)
}

// codeOf is the code of an API answer, or -1 when it has none.
func codeOf(r apiReply) int {
	var v struct {
		Code *int `json:"code"`
	}
	if json.Unmarshal(r.body, &v) != nil || v.Code == nil {
		return -1
	}
	return *v.Code
}

// until polls ok every 25ms until it holds, for at most d. SRS answers each
// API request on a connection of its own, which leaves a socket in TIME_WAIT
// on an ephemeral port, so a poll of the API is not more often.
func until(d time.Duration, ok func() bool) bool {
	return untilEvery(d, 25*time.Millisecond, ok)
}

// untilEvery polls ok every interval until it holds, for at most d.
func untilEvery(d, every time.Duration, ok func() bool) bool {
	deadline := time.Now().Add(d)
	for !ok() {
		if time.Now().After(deadline) {
			return false
		}
		time.Sleep(every)
	}
	return true
}

// clientWant is what a case knows of a client.
type clientWant struct {
	typ, name, url, tcURL, ip string
	publish                   bool
}

// checkClient checks a client of the API has every field, and the values a
// case knows.
func checkClient(c *check.C, cl map[string]any, w clientWant) {
	c.Helper()
	checkFields(c, cl, clientFields, "client")
	checkFields(c, object(c, cl, "kbps"), kbpsFields, "client kbps")
	if w.typ != "" {
		c.CheckEqual(cl["type"], w.typ, "client type")
	}
	c.CheckEqual(cl["publish"], w.publish, "client publish")
	c.CheckEqual(cl["name"], w.name, "client name")
	c.CheckEqual(cl["url"], w.url, "client url")
	if w.tcURL != "" {
		c.CheckEqual(cl["tcUrl"], w.tcURL, "client tcUrl")
	}
	ip := w.ip
	if ip == "" {
		ip = "127.0.0.1"
	}
	c.CheckEqual(cl["ip"], ip, "client ip")
	alive := num(c, cl, "alive")
	c.Check(alive >= 0 && alive < 60, "client alive %v, want seconds since it came", alive)
	c.Check(num(c, cl, "send_bytes") >= 0 && num(c, cl, "recv_bytes") >= 0, "client bytes %v %v", cl["send_bytes"], cl["recv_bytes"])
}

// streamWant is what a case knows of a stream's codecs, nil for a track it
// has no.
type streamWant struct {
	video *videoWant
	audio *audioWant
}

type videoWant struct {
	codec, profile string
	width, height  int
}

type audioWant struct {
	codec, profile string
	rate, channels int
}

var (
	h264High = &videoWant{"H264", "High", 768, 320}
	hevcMain = &videoWant{"HEVC", "Main", 768, 320}
	aacLC    = &audioWant{"AAC", "LC", 44100, 2}
	mp3      = &audioWant{"MP3", "", 44100, 2}
)

// checkStream checks a stream of the API has every field, is published, and
// has the codecs a case knows; a codec's profile is checked when the case
// knows it, its level only for being there.
func checkStream(c *check.C, st map[string]any, name string, w streamWant) {
	c.Helper()
	checkFields(c, st, streamFields, "stream")
	checkFields(c, object(c, st, "kbps"), kbpsFields, "stream kbps")
	c.CheckEqual(st["name"], name, "stream name")
	c.CheckEqual(st["app"], "live", "stream app")
	c.CheckEqual(st["url"], "/live/"+name, "stream url")
	nearNow(c, num(c, st, "live_ms"), 2*time.Second, "stream live_ms")
	c.CheckEqual(num(c, st, "frames"), num(c, st, "audio_frames")+num(c, st, "video_frames"), "stream frames, audio and video")
	pub := object(c, st, "publish")
	c.CheckEqual(pub["active"], true, "stream publish active")
	c.Check(pub["cid"] != nil, "stream publish %v, want the publisher's cid", pub)

	if w.video == nil {
		c.CheckEqual(st["video"], nil, "stream video")
	} else if v, ok := st["video"].(map[string]any); c.Check(ok, "stream video %v, want an object", st["video"]) {
		checkFields(c, v, []string{"codec", "profile", "level", "width", "height"}, "stream video")
		c.CheckEqual(v["codec"], w.video.codec, "video codec")
		c.CheckEqual(v["profile"], w.video.profile, "video profile")
		level, _ := v["level"].(string)
		c.Check(level != "", "video level %v, want one", v["level"])
		c.CheckEqual(v["width"], float64(w.video.width), "video width")
		c.CheckEqual(v["height"], float64(w.video.height), "video height")
	}

	if w.audio == nil {
		c.CheckEqual(st["audio"], nil, "stream audio")
	} else if a, ok := st["audio"].(map[string]any); c.Check(ok, "stream audio %v, want an object", st["audio"]) {
		checkFields(c, a, []string{"codec", "sample_rate", "channel", "profile"}, "stream audio")
		c.CheckEqual(a["codec"], w.audio.codec, "audio codec")
		c.CheckEqual(a["sample_rate"], float64(w.audio.rate), "audio sample_rate")
		c.CheckEqual(a["channel"], float64(w.audio.channels), "audio channel")
		if w.audio.profile != "" {
			c.CheckEqual(a["profile"], w.audio.profile, "audio profile")
		}
	}
}

// publishLead is the media publishRTMP sends at once: half a GOP, so a player
// that joins next gets both tracks from the GOP cache, not a cache that the
// next keyframe has just reset to video alone.
const publishLead = 500 * time.Millisecond

// publishRTMP publishes a file on a server's stream live/name, publishLead
// of it at once, then in real time, and returns the publisher and the ids
// of its on_publish: the client and the stream.
func publishRTMP(c *check.C, srs *server.Server, name, file string) (*rtmp.Client, string, string) {
	c.Helper()
	pub := startPublisher(c, rtmpURL(srs, "live/"+name), media.Path(c, file), publishLead)
	h := waitHook(c, srs, "on_publish", name)
	return pub, h.Field("client_id"), h.Field("stream_id")
}

// playRTMPOf plays live/name over RTMP until it plays, and returns the
// player and its client id.
func playRTMPOf(c *check.C, srs *server.Server, name string, opts *rtmp.Options) (*rtmp.Client, string) {
	c.Helper()
	player, _ := playRTMPClient(c, rtmpURL(srs, "live/"+name), opts)
	return player, waitHook(c, srs, "on_play", name).Field("client_id")
}

// untilEOF reads an RTMP client until the server closes it, for at most d.
func untilEOF(cl *rtmp.Client, d time.Duration) error {
	cl.SetDeadline(time.Now().Add(d))
	for {
		if _, err := cl.ReadTag(); err != nil {
			if isTimeout(err) {
				return fmt.Errorf("not closed within %v", d)
			}
			return nil
		}
	}
}

// untilPCLeaves waits until a peer connection is no longer connected, for at
// most d.
func untilPCLeaves(pc *webrtc.PeerConnection, d time.Duration) error {
	if !until(d, func() bool { return pc.ConnectionState() != webrtc.PeerConnectionStateConnected }) {
		return fmt.Errorf("still connected after %v", d)
	}
	return nil
}

// apiStreams adds the cases of the vhosts, streams and clients endpoints on
// the Common server: the ids across them, the fields of each protocol's
// publishers and players, the codec info, the byte and frame counts, a DELETE
// of a client of each protocol, and the errors.
func apiStreams(t *testing.T) {
	apiStreamIDs(t)
	apiStreamFields(t)
	apiCounts(t)
	apiKicks(t)
}

// apiStreamIDs adds the cases of the ids across the endpoints, and the errors.
func apiStreamIDs(t *testing.T) {
	// IdsAcrossEndpoints publishes and plays a stream over RTMP: the vhost,
	// the stream and both clients name each other by the same ids in their
	// lists and by their own paths.
	check.Case(t, "IdsAcrossEndpoints", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		_, pubID, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		_, playID := playRTMPOf(c, srs, stream, nil)

		vhost := defaultVhost(c, srs)
		vid, _ := vhost["id"].(string)
		byID := object(c, apiObject(c, srs, "/api/v1/vhosts/"+vid), "vhost")
		c.CheckEqual(byID["id"], vid, "the vhost by its id")
		c.CheckEqual(byID["name"], "__defaultVhost__", "the vhost's name by its id")

		st := apiStreamOf(c, srs, streamID)
		c.CheckEqual(st["id"], streamID, "stream id")
		c.CheckEqual(st["vhost"], vid, "stream vhost")
		c.CheckEqual(object(c, st, "publish")["cid"], pubID, "stream publish cid, the publisher")
		c.CheckEqual(st["clients"], 2.0, "stream clients")
		streams, _ := apiList(c, srs, "streams", "?count=10000")
		if listed := itemOf(streams, streamID); c.Check(listed != nil, "stream %s not in the list", streamID) {
			c.CheckEqual(listed["name"], stream, "the listed stream's name")
			c.CheckEqual(listed["vhost"], vid, "the listed stream's vhost")
		}

		clients, _ := apiList(c, srs, "clients", "?count=10000")
		for _, id := range []string{pubID, playID} {
			cl := apiClientOf(c, srs, id)
			c.CheckEqual(cl["id"], id, "client id")
			c.CheckEqual(cl["vhost"], vid, "client vhost")
			c.CheckEqual(cl["stream"], streamID, "client stream")
			if listed := itemOf(clients, id); c.Check(listed != nil, "client %s not in the list", id) {
				c.CheckEqual(listed["stream"], streamID, "the listed client's stream")
			}
		}
	})

	// VhostFields checks the default vhost, which SRS lists from its first
	// client on: every field, enabled, and its HLS, on with the 10s fragment
	// of Common.
	check.Case(t, "VhostFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		publishRTMP(c, srs, streamName(c), media.H264HighAAC)
		v := defaultVhost(c, srs)
		checkFields(c, v, vhostFields, "vhost")
		checkFields(c, object(c, v, "kbps"), kbpsFields, "vhost kbps")
		c.CheckEqual(v["enabled"], true, "vhost enabled")
		hls := object(c, v, "hls")
		c.CheckEqual(hls["enabled"], true, "vhost hls enabled")
		c.CheckEqual(hls["fragment"], 10.0, "vhost hls fragment")
	})

	// UnknownIds asks for an id of each kind that doesn't exist: each is
	// answered with its not-found code, also a DELETE of a client.
	check.Case(t, "UnknownIds", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		for path, code := range map[string]int{
			"/api/v1/vhosts/vid-none":   2014,
			"/api/v1/streams/vid-none":  2048,
			"/api/v1/clients/none":      2049,
			"/api/v1/forwards/fwd-none": 2057,
		} {
			r := apiFetch(c, srs, path)
			c.CheckEqual(r.status, http.StatusOK, path+" status")
			c.CheckEqual(codeOf(r), code, path+" code")
		}
		c.CheckEqual(codeOf(apiKick(c, srs, "none")), 2049, "DELETE of an unknown client")
		c.CheckEqual(codeOf(apiKick(c, srs, "")), 2049, "DELETE of the clients list")
	})

	// MethodsNotAllowed sends the methods the lists don't take: 405.
	check.Case(t, "MethodsNotAllowed", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		for path, methods := range map[string][]string{
			"/api/v1/vhosts/":           {http.MethodPost, http.MethodPut, http.MethodDelete},
			"/api/v1/streams/":          {http.MethodPost, http.MethodPut, http.MethodDelete},
			"/api/v1/clients/":          {http.MethodPost, http.MethodPut},
			"/api/v1/forwards/":         {http.MethodPost, http.MethodPut, http.MethodDelete},
			"/api/v1/forwards/fwd-none": {http.MethodPost, http.MethodPut, http.MethodDelete},
		} {
			for _, m := range methods {
				req, err := http.NewRequest(m, srs.APIURL(path), nil)
				c.Must(err, "request")
				c.CheckEqual(apiDo(c, httpClient, req).status, http.StatusMethodNotAllowed, m+" "+path)
			}
		}
	})

	// MethodsOnIds sends the methods a stream or a client by its id doesn't
	// take: 405, and both are still there.
	check.Case(t, "MethodsOnIds", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		_, pubID, streamID := publishRTMP(c, srs, streamName(c), media.H264HighAAC)
		for _, path := range []string{"/api/v1/streams/" + streamID, "/api/v1/clients/" + pubID, "/api/v1/vhosts/" + defaultVhost(c, srs)["id"].(string)} {
			for _, m := range []string{http.MethodPost, http.MethodPut} {
				req, err := http.NewRequest(m, srs.APIURL(path), nil)
				c.Must(err, "request")
				c.CheckEqual(apiDo(c, httpClient, req).status, http.StatusMethodNotAllowed, m+" "+path)
			}
		}
		req, err := http.NewRequest(http.MethodDelete, srs.APIURL("/api/v1/streams/"+streamID), nil)
		c.Must(err, "request")
		c.CheckEqual(apiDo(c, httpClient, req).status, http.StatusMethodNotAllowed, "DELETE of a stream")
		c.CheckEqual(apiStreamOf(c, srs, streamID)["id"], streamID, "the stream after the methods")
		c.CheckEqual(apiClientOf(c, srs, pubID)["id"], pubID, "the publisher after the methods")
	})
}

// apiStreamFields adds the cases of the fields of each protocol's publishers
// and players, and of the codecs of their streams.
func apiStreamFields(t *testing.T) {
	// RtmpPublisherFields checks an FMLE publisher with a pageUrl and
	// swfUrl in its connect, and its H.264 High and AAC stream.
	check.Case(t, "RtmpPublisherFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		opts := &rtmp.Options{Connect: map[string]any{"pageUrl": "http://page.example/live.html", "swfUrl": "http://page.example/player.swf"}}
		startPublisherWith(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), time.Second, opts, rtmp.FMLE)
		h := waitHook(c, srs, "on_publish", stream)
		cl := apiClientOf(c, srs, h.Field("client_id"))
		checkClient(c, cl, clientWant{typ: "fmle-publish", name: stream, url: "/live/" + stream, tcURL: rtmpURL(srs, "live"), publish: true})
		c.CheckEqual(cl["pageUrl"], "http://page.example/live.html", "client pageUrl")
		c.CheckEqual(cl["swfUrl"], "http://page.example/player.swf", "client swfUrl")
		st := apiStreamWith(c, srs, h.Field("stream_id"), streamWant{h264High, aacLC})
		checkStream(c, st, stream, streamWant{h264High, aacLC})
		c.CheckEqual(st["tcUrl"], rtmpURL(srs, "live"), "stream tcUrl")
		c.CheckEqual(st["clients"], 1.0, "stream clients")
	})

	// FlashPublisherType checks a Flash-style publisher's type.
	check.Case(t, "FlashPublisherType", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startPublisherWith(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), time.Second, nil, rtmp.Flash)
		cl := apiClientOf(c, srs, waitHook(c, srs, "on_publish", stream).Field("client_id"))
		checkClient(c, cl, clientWant{typ: "flash-publish", name: stream, url: "/live/" + stream, publish: true})
	})

	// RtmpPlayerFields checks an RTMP player.
	check.Case(t, "RtmpPlayerFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		_, _, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		_, id := playRTMPOf(c, srs, stream, nil)
		cl := apiClientOf(c, srs, id)
		checkClient(c, cl, clientWant{typ: "rtmp-play", name: stream, url: "/live/" + stream, tcURL: rtmpURL(srs, "live")})
		c.CheckEqual(cl["stream"], streamID, "client stream")
	})

	// RtmpsClientFields checks a publisher and a player over RTMPS: the
	// types of RTMP, and the rtmps:// tcUrl each connected with.
	check.Case(t, "RtmpsClientFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		tlsConf, err := server.ClientTLS()
		c.Must(err, "TLS")
		tcURL := "rtmps://" + srs.Addr(server.RTMPS) + "/live"
		opts := &rtmp.Options{TLS: tlsConf}
		startPublisherWith(c, tcURL+"/"+stream, media.Path(c, media.H264HighAAC), time.Second, opts, rtmp.FMLE)
		pubID := waitHook(c, srs, "on_publish", stream).Field("client_id")
		playRTMPClient(c, tcURL+"/"+stream, opts)
		playID := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkClient(c, apiClientOf(c, srs, pubID), clientWant{typ: "fmle-publish", name: stream, url: "/live/" + stream, tcURL: tcURL, publish: true})
		checkClient(c, apiClientOf(c, srs, playID), clientWant{typ: "rtmp-play", name: stream, url: "/live/" + stream, tcURL: tcURL})
	})

	// Each RTMP test file's codecs in its stream: HEVC Main over enhanced
	// RTMP, MP3, and a stream with no video or no audio.
	for _, s := range []struct {
		name, file string
		want       streamWant
	}{
		{"HevcStreamFields", media.HEVCAAC, streamWant{hevcMain, aacLC}},
		{"Mp3StreamFields", media.H264HighMP3, streamWant{h264High, mp3}},
		{"AudioOnlyStreamFields", media.AACOnly, streamWant{nil, aacLC}},
		{"VideoOnlyStreamFields", media.H264Only, streamWant{h264High, nil}},
	} {
		check.Case(t, s.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			_, _, streamID := publishRTMP(c, srs, stream, s.file)
			checkStream(c, apiStreamWith(c, srs, streamID, s.want), stream, s.want)
		})
	}

	// HttpFlvPlayerFields checks an HTTP-FLV player. Its tcUrl and pageUrl
	// are HttpFlvPlayerUrls's.
	check.Case(t, "HttpFlvPlayerFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		cl, streamID := httpFlvPlayerClient(c, srs, stream)
		checkClient(c, cl, clientWant{typ: "flv-play", name: stream, url: "/live/" + stream})
		c.CheckEqual(cl["stream"], streamID, "client stream")
	})

	// HttpFlvPlayerUrls checks the tcUrl and pageUrl of an HTTP-FLV player
	// with a Referer are its own: the http:// URL of its app on the HTTP
	// server, and its Referer, as its on_play has.
	check.Case(t, "HttpFlvPlayerUrls", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		cl, _ := httpFlvPlayerClient(c, srs, streamName(c))
		c.CheckEqual(cl["tcUrl"], "http://"+srs.Addr(server.HTTPServer)+"/live", "client tcUrl")
		c.CheckEqual(cl["pageUrl"], "http://page.example/flv.html", "client pageUrl, the Referer")
	})

	// HlsPlayerFields checks an HLS session: its hls_ctx is its client id,
	// of the stream, with its Referer as pageUrl. The name is
	// HlsClientName's.
	check.Case(t, "HlsPlayerFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := publishHLSHooked(c, srs, stream, "")
		ctx := hlsSession(c, srs, stream, "", http.Header{"Referer": {"http://page.example/hls.html"}})
		cl := apiClientOf(c, srs, ctx)
		w := clientWant{typ: "hls-play", url: "/live/" + stream}
		w.name, _ = cl["name"].(string)
		checkClient(c, cl, w)
		c.CheckEqual(cl["id"], ctx, "client id, the hls_ctx")
		c.CheckEqual(cl["pageUrl"], "http://page.example/hls.html", "client pageUrl")
		c.CheckEqual(cl["stream"], pub.Field("stream_id"), "client stream")
	})

	// HlsPlayerUrls checks the tcUrl of an HLS session is the http:// URL
	// of its app on the HTTP server, with the server's port, as an HTTP-FLV
	// player's is (HttpFlvPlayerUrls).
	check.Case(t, "HlsPlayerUrls", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		publishHLSHooked(c, srs, stream, "")
		cl := apiClientOf(c, srs, hlsSession(c, srs, stream, "", nil))
		c.CheckEqual(cl["tcUrl"], "http://"+srs.Addr(server.HTTPServer)+"/live", "client tcUrl")
	})

	// HlsClientName checks an HLS session names its stream like every
	// other client, without the playlist's extension.
	check.Case(t, "HlsClientName", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		publishHLSHooked(c, srs, stream, "")
		c.CheckEqual(apiClientOf(c, srs, hlsSession(c, srs, stream, "", nil))["name"], stream, "client name")
	})

	// WhipPublisherFields checks a WHIP publisher and its H.264 Baseline
	// and Opus 48 kHz stereo stream, after a second of media. The video size
	// is WhipVideoSize's.
	check.Case(t, "WhipPublisherFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		h, _ := whipStream(c, srs, stream, time.Second)
		checkClient(c, apiClientOf(c, srs, h.client), clientWant{typ: "rtc-publish", name: stream, url: "/live/" + stream, publish: true})
		st := apiStreamOf(c, srs, h.stream)
		v := object(c, st, "video")
		checkStream(c, st, stream, streamWant{
			&videoWant{"H264", "Baseline", int(num(c, v, "width")), int(num(c, v, "height"))},
			&audioWant{"Opus", "", 48000, 2},
		})
	})

	// WhipOpusAudioInfo checks the Opus of a WHIP stream: 48 kHz stereo,
	// as its SDP says.
	check.Case(t, "WhipOpusAudioInfo", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		h, _ := whipStream(c, srs, streamName(c), 200*time.Millisecond)
		a := object(c, apiStreamOf(c, srs, h.stream), "audio")
		c.CheckEqual(a["sample_rate"], 48000.0, "audio sample_rate")
		c.CheckEqual(a["channel"], 2.0, "audio channel")
	})

	// WhipVideoSize checks the video of a WHIP stream has the size of its
	// H.264 SPS, 768x320, as RTMP and SRT streams do.
	check.Case(t, "WhipVideoSize", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		h, _ := whipStream(c, srs, streamName(c), time.Second)
		v := object(c, apiStreamOf(c, srs, h.stream), "video")
		c.Check(v["width"] == 768.0 && v["height"] == 320.0, "video %vx%v, want 768x320", v["width"], v["height"])
	})

	// WhepPlayerFields checks a WHEP player.
	check.Case(t, "WhepPlayerFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		h, _ := whipStream(c, srs, stream, 200*time.Millisecond)
		rtcPlayer(c, whepURL(srs, "live", stream), nil)
		cl := apiClientOf(c, srs, waitHook(c, srs, "on_play", stream).Field("client_id"))
		checkClient(c, cl, clientWant{typ: "rtc-play", name: stream, url: "/live/" + stream})
		c.CheckEqual(cl["stream"], h.stream, "client stream")
	})

	// SrtPublisherFields checks an SRT publisher and its H.264 High and AAC
	// stream, with the srt:// tcUrl of its app.
	check.Case(t, "SrtPublisherFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		h := waitHook(c, srs, "on_publish", stream)
		cl := apiClientOf(c, srs, h.Field("client_id"))
		checkClient(c, cl, clientWant{typ: "srt-publish", name: stream, url: "/live/" + stream, publish: true})
		checkSrtTcURL(c, cl["tcUrl"])
		untilCodecs(c, srs, h.Field("stream_id"))
		st := apiStreamOf(c, srs, h.Field("stream_id"))
		checkStream(c, st, stream, streamWant{h264High, aacLC})
	})

	// SrtPlayerFields checks an SRT player.
	check.Case(t, "SrtPlayerFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		streamID := waitHook(c, srs, "on_publish", stream).Field("stream_id")
		player := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		srtReadSome(c, player)
		cl := apiClientOf(c, srs, waitHook(c, srs, "on_play", stream).Field("client_id"))
		checkClient(c, cl, clientWant{typ: "srt-play", name: stream, url: "/live/" + stream})
		checkSrtTcURL(c, cl["tcUrl"])
		c.CheckEqual(cl["stream"], streamID, "client stream")
	})

	// RtspPlayerFields checks an RTSP player, with the rtsp:// tcUrl of its
	// app. Its type is RtspPlayerType's.
	check.Case(t, "RtspPlayerFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		cl, streamID := rtspPlayerClient(c, srs, stream)
		checkClient(c, cl, clientWant{name: stream, url: "/live/" + stream, tcURL: rtspURL(srs, "live")})
		c.CheckEqual(cl["stream"], streamID, "client stream")
	})

	// RtspPlayerType checks an RTSP player's type is rtsp-play, not a
	// WebRTC player's rtc-play, as the type of every other player names its
	// protocol.
	check.Case(t, "RtspPlayerType", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		cl, _ := rtspPlayerClient(c, srs, streamName(c))
		c.CheckEqual(cl["type"], "rtsp-play", "client type")
		c.CheckEqual(cl["publish"], false, "client publish")
	})

	// GbPublisherFields checks a GB28181 stream: its publisher is the RTMP
	// client SRS publishes it with, of the device's id as the stream, with
	// H.264 High and AAC.
	check.Case(t, "GbPublisherFields", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264HighAAC, gbSSRC())
		s.sendThrough(c, time.Second)
		h := waitHook(c, srs, "on_publish", s.id)
		cl := apiClientOf(c, srs, h.Field("client_id"))
		checkClient(c, cl, clientWant{name: s.id, url: "/live/" + s.id, publish: true})
		typ, _ := cl["type"].(string)
		c.Check(strings.HasSuffix(typ, "-publish"), "client type %q, want a publish type", typ)
		checkStream(c, apiStreamWith(c, srs, h.Field("stream_id"), streamWant{h264High, aacLC}), s.id, streamWant{h264High, aacLC})
	})
}

// checkSrtTcURL checks the tcUrl of an SRT client: srt://<host>/live.
func checkSrtTcURL(c *check.C, v any) {
	c.Helper()
	s, _ := v.(string)
	c.Check(strings.HasPrefix(s, "srt://") && strings.HasSuffix(s, "/live"), "tcUrl %q, want srt://<host>/live", s)
}

// httpFlvPlayerClient plays a stream over HTTP-FLV with a Referer and returns
// the player's client of the API and the stream's id.
func httpFlvPlayerClient(c *check.C, srs *server.Server, stream string) (map[string]any, string) {
	c.Helper()
	_, _, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
	untilCodecs(c, srs, streamID)
	p := httpFlvPlayer(c, srs, "live/"+stream+".flv", &flv.HTTPOptions{Header: http.Header{"Referer": {"http://page.example/flv.html"}}})
	readHTTPFlv(c, p, playedUntil)
	return apiClientOf(c, srs, waitHook(c, srs, "on_play", stream).Field("client_id")), streamID
}

// rtspPlayerClient plays a stream over RTSP and returns the player's client
// of the API and the stream's id.
func rtspPlayerClient(c *check.C, srs *server.Server, stream string) (map[string]any, string) {
	c.Helper()
	start := rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
	streamID := waitHook(c, srs, "on_publish", stream).Field("stream_id")
	p := rtspPlay(c, srs, "live/"+stream, nil)
	start()
	rtspUntil(c, p, rtspPlayedUntil)
	return apiClientOf(c, srs, waitHook(c, srs, "on_play", stream).Field("client_id")), streamID
}

// whipIDs are the ids of a WHIP publisher and its stream.
type whipIDs struct{ client, stream string }

// whipStream publishes the WHIP test file to live/name, d of it at once, and
// keeps the publisher until the case ends.
func whipStream(c *check.C, srs *server.Server, name string, d time.Duration) (whipIDs, *rtc.Publisher) {
	c.Helper()
	pub := rtcPublisher(c, whipURL(srs, "live", name), nil)
	h := waitHook(c, srs, "on_publish", name)
	whipThrough(c, srs, pub, rtcSamples(c), d)
	ids := whipIDs{h.Field("client_id"), h.Field("stream_id")}
	untilCodecs(c, srs, ids.stream)
	return ids, pub
}

// apiCounts adds the cases of the frame and byte counts on Common.
func apiCounts(t *testing.T) {
	// SrtFrameCounts publishes 13s of SRT at once: the stream counts the TS
	// frames of each track, in steps of 289, so at least the first step of
	// each.
	check.Case(t, "SrtFrameCounts", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		conn := dialSRT(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")})
		streamID := waitHook(c, srs, "on_publish", stream).Field("stream_id")
		sendSRTThrough(c, srs, conn, media.H264HighAACTS, 13*time.Second)
		var st map[string]any
		c.Check(until(300*time.Millisecond, func() bool {
			st = apiStreamOf(c, srs, streamID)
			return num(c, st, "video_frames") >= 289 && num(c, st, "audio_frames") >= 289
		}), "stream frames video %v audio %v, want 289 or more of each", st["video_frames"], st["audio_frames"])
		c.CheckEqual(num(c, st, "frames"), num(c, st, "video_frames")+num(c, st, "audio_frames"), "frames")
	})

	// StreamBytesAfterClose publishes with a player, then the
	// publisher unpublishes and closes: the stream, which the player keeps,
	// counts the bytes SRS read from the publisher and sent it.
	check.Case(t, "StreamBytesAfterClose", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub, pubID, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		player, _ := playRTMPOf(c, srs, stream, nil)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Unpublish(ctx)
		c.Must(err, "unpublish")
		pub.Close()
		c.Must(untilGone(c, srs, "/api/v1/clients/"+pubID), "the publisher leaves")
		sent, got := pub.BytesSent(), pub.BytesReceived()

		st := apiStreamOf(c, srs, streamID)
		recv, send := uint64(num(c, st, "recv_bytes")), uint64(num(c, st, "send_bytes"))
		// The player's bytes join the stream's at SRS's next sample, 3s apart.
		c.Check(recv >= sent && recv <= sent+player.BytesSent(), "stream recv_bytes %d, want the %d the publisher sent, and at most the player's %d more", recv, sent, player.BytesSent())
		c.Check(send >= got && send <= got+player.BytesReceived(), "stream send_bytes %d, want the %d the publisher got, and at most the player's %d more", send, got, player.BytesReceived())
		v := defaultVhost(c, srs)
		c.Check(uint64(num(c, v, "recv_bytes")) >= sent, "vhost recv_bytes %v, want at least the stream's %d", v["recv_bytes"], sent)
	})
}

// sendSRTThrough sends a TS file on an SRT publisher through d at once, in a
// burst slot of the server, and keeps the publisher until the case ends.
func sendSRTThrough(c *check.C, srs *server.Server, conn *srt.Conn, file string, d time.Duration) {
	c.Helper()
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	c.Cleanup(func() { f.Close() })
	free := srs.Burst(c)
	defer free()
	c.Must(srt.NewSender(f).SendThrough(conn, d), "publish the media")
}

// TestApi_TsMount checks an HTTP-TS player of the API, on the shared TsMount
// server, whose remux mount is TS.
func TestApi_TsMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsMount", sharedTsMountConfig())

	// HttpTsPlayerFields checks an HTTP-TS player. Its type is
	// HttpTsPlayerType's.
	check.Case(t, "HttpTsPlayerFields", check.Normal, func(c *check.C) {
		stream := streamName(c)
		cl, streamID := remuxPlayerClient(c, srs, stream, tsMount)
		checkClient(c, cl, clientWant{name: stream, url: "/live/" + stream})
		c.CheckEqual(cl["stream"], streamID, "client stream")
	})

	// HttpTsPlayerType checks an HTTP-TS player's type is ts-play, not an
	// HTTP-FLV player's flv-play, as the type of every other player names its
	// protocol.
	check.Case(t, "HttpTsPlayerType", check.Normal, func(c *check.C) {
		cl, _ := remuxPlayerClient(c, srs, streamName(c), tsMount)
		c.CheckEqual(cl["type"], "ts-play", "client type")
		c.CheckEqual(cl["publish"], false, "client publish")
	})

	// DeleteHttpTsPlayer: the HTTP-TS response ends at once, as an HTTP-FLV
	// player's.
	check.Case(t, "DeleteHttpTsPlayer", check.Normal, func(c *check.C) {
		stream := streamName(c)
		p, _ := remuxMountPlayer(c, srs, stream, tsMount)
		checkRemuxPlayerDeleted(c, srs, p, stream)
	})
}

// TestApi_Mp3Mount checks an HTTP MP3 player of the API, on the shared
// Mp3Mount server, whose remux mount is MP3.
func TestApi_Mp3Mount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Mp3Mount", sharedMp3MountConfig())

	// HttpMp3PlayerType checks an HTTP MP3 player's type is mp3-play, not an
	// HTTP-FLV player's flv-play.
	check.Case(t, "HttpMp3PlayerType", check.Normal, func(c *check.C) {
		cl, _ := remuxPlayerClient(c, srs, streamName(c), mp3Mount)
		c.CheckEqual(cl["type"], "mp3-play", "client type")
		c.CheckEqual(cl["publish"], false, "client publish")
	})

	// DeleteHttpMp3Player: the MP3 response ends at once, as an HTTP-FLV
	// player's.
	check.Case(t, "DeleteHttpMp3Player", check.Normal, func(c *check.C) {
		stream := streamName(c)
		p, _ := remuxMountPlayer(c, srs, stream, mp3Mount)
		checkRemuxPlayerDeleted(c, srs, p, stream)
	})
}

// TestApi_AacMount checks an HTTP AAC player of the API, on the shared
// AacMount server, whose remux mount is AAC.
func TestApi_AacMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "AacMount", sharedAacMountConfig())

	// HttpAacPlayerType checks an HTTP AAC player's type is aac-play, not an
	// HTTP-FLV player's flv-play.
	check.Case(t, "HttpAacPlayerType", check.Normal, func(c *check.C) {
		cl, _ := remuxPlayerClient(c, srs, streamName(c), aacMount)
		c.CheckEqual(cl["type"], "aac-play", "client type")
		c.CheckEqual(cl["publish"], false, "client publish")
	})

	// DeleteHttpAacPlayer: the AAC response ends at once, as an HTTP-FLV
	// player's.
	check.Case(t, "DeleteHttpAacPlayer", check.Normal, func(c *check.C) {
		stream := streamName(c)
		p, _ := remuxMountPlayer(c, srs, stream, aacMount)
		checkRemuxPlayerDeleted(c, srs, p, stream)
	})
}

// remuxPlayerClient publishes the mount's file and plays the stream from the
// mount, and returns the player's client of the API and the stream's id.
func remuxPlayerClient(c *check.C, srs *server.Server, stream string, m remuxMount) (map[string]any, string) {
	c.Helper()
	_, streamID := remuxMountPlayer(c, srs, stream, m)
	return apiClientOf(c, srs, waitHook(c, srs, "on_play", stream).Field("client_id")), streamID
}

// remuxMountPlayer publishes the mount's file and plays the stream from the
// mount, and returns the player and the stream's id.
func remuxMountPlayer(c *check.C, srs *server.Server, stream string, m remuxMount) (*remuxPlayer, string) {
	c.Helper()
	_, _, streamID := publishRTMP(c, srs, stream, m.file)
	untilCodecs(c, srs, streamID)
	p := remuxPlayerOf(c, srs, "live/"+stream+"."+m.ext, nil)
	c.Must(p.readUntil(m.until, time.Second), "play")
	return p, streamID
}

// apiStreamWith returns a stream once the API has the codecs w wants, for at
// most 300ms: SRS announces the publish before it reads the sequence headers.
func apiStreamWith(c *check.C, srs *server.Server, id string, w streamWant) map[string]any {
	c.Helper()
	var st map[string]any
	until(300*time.Millisecond, func() bool {
		st = apiStreamOf(c, srs, id)
		return (w.video == nil || st["video"] != nil) && (w.audio == nil || st["audio"] != nil)
	})
	return st
}

// untilCodecs waits until the API has both codecs of a stream, for at most
// 300ms: SRS has read the first packets, or sequence headers, of each track.
func untilCodecs(c *check.C, srs *server.Server, streamID string) {
	c.Helper()
	var st map[string]any
	c.Check(until(300*time.Millisecond, func() bool {
		st = apiStreamOf(c, srs, streamID)
		return st["video"] != nil && st["audio"] != nil
	}), "stream video %v audio %v, want both codecs", st["video"], st["audio"])
}
