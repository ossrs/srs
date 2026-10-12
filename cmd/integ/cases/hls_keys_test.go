// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/mp4"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/ts"
)

// keyQuery is the query of the TsFloor server's hls_key_url, which SRS puts
// after each key file, as a token for a key server.
const keyQuery = "token=abc123&sig=xyz789"

// noPaddingBug marks the case a known bug when err is an encrypted segment
// with no padding. SRS pads only a partial last AES block of 4 TS packets, so
// whether a segment hits it depends on the media: which segment is a whole
// number of blocks changes with the ffmpeg that made the test media.
func noPaddingBug(c *check.C, err error) {
	if errors.Is(err, hls.ErrNoPadding) {
		check.KnownBug(c, "an encrypted TS segment of whole 4-packet AES blocks has no PKCS#7 padding, so a player can't decrypt it")
	}
}

// sharedTsFloor is the config of the server that TestHls_TsFloor,
// TestHls_Keys, TestHls_Sessions and TestRtc_NackTwccOff share. None of
// them depends on what the others set; WebRTC sessions write no files:
//   - hls_ts_floor with 2s fragments and [timestamp] in hls_ts_file;
//   - hls_keys with a new key every 2 segments, and hls_key_url on the CDN
//     host with a query;
//   - hls_ts_ctx off and hls_master_m3u8_path_relative on, the hls_ctx
//     session settings Common keeps at their defaults;
//   - every hook on a mock, for the sessions' on_play;
//   - WebRTC over UDP with nack and twcc off;
//   - SRT with the server settings of sharedTsFloorSRT.
func sharedTsFloor() server.Config {
	cfg := server.HLSConfig(server.HLS{
		Fragment:        2 * time.Second,
		TSFloor:         true,
		TSFile:          "[app]/[stream]-[timestamp].ts",
		Keys:            true,
		FragmentsPerKey: 2,
		KeyURL:          "http://" + cdnHost + "/?" + keyQuery,
		NoTSCtx:         true,
		MasterRelative:  true,
	})
	cfg.Hooks = true
	return server.WithSRT(server.WithRTC(cfg, sharedTsFloorRTC), sharedTsFloorSRT)
}

// TestHls_Keys plays the AES-128 encrypted TS segments of hls_keys: a key
// file for every 2 segments, its tag in the playlist with the key URL and
// its query, and segments that only decrypt with their key and IV.
func TestHls_Keys(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsFloor", sharedTsFloor())

	// publish7s publishes 7s at once, which closes the 2s segments 0 to 2,
	// and returns the playlist.
	publish7s := func(c *check.C, stream string) *hls.Playlist {
		publishThrough(c, srs, stream, media.H264HighAAC, 7*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(2))
		return pl
	}

	// Every segment has an AES-128 key with an IV; segments 0 and 1 share
	// the key of 0, and segment 2 starts the next, with another IV.
	check.Case(t, "KeyTags", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		pl := publish7s(c, stream)
		if !c.Check(len(pl.Segments) >= 3, "segments %+v, want 3", pl.Segments) {
			return
		}
		for _, s := range pl.Segments[:3] {
			if !c.Check(s.Key != nil, "segment %v has no key", s.Sequence) {
				return
			}
			c.CheckEqual(s.Key.Method, "AES-128", fmt.Sprintf("key method of segment %v", s.Sequence))
			c.CheckEqual(len(s.Key.IV), 16, fmt.Sprintf("IV bytes of segment %v", s.Sequence))
			u, _ := url.Parse(s.Key.URI)
			c.CheckEqual(u.Path, fmt.Sprintf("/%v-%v.key", stream, s.Sequence/2*2), fmt.Sprintf("key path of segment %v", s.Sequence))
		}
		k0, k1, k2 := pl.Segments[0].Key, pl.Segments[1].Key, pl.Segments[2].Key
		c.Check(k0 == k1, "segment 1 has its own key tag %+v, want the one of segment 0", k1)
		c.Check(!bytes.Equal(k0.IV, k2.IV), "the keys of segments 0 and 2 have the same IV %x", k0.IV)
	})

	// hls_key_url is the base of the key URIs, and its query goes after the
	// key file; the key is served there.
	check.Case(t, "KeyUrlQuery", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		pl := publish7s(c, stream)
		k := pl.Segments[0].Key
		if !c.Check(k != nil, "segment 0 has no key") {
			return
		}
		u, _ := url.Parse(k.URI)
		c.CheckEqual(u.Host, cdnHost, "key host")
		c.CheckEqual(u.RawQuery, keyQuery, "key query")
		res, body := httpGet(c, cdnClient(c, srs), k.URI)
		c.CheckEqual(res.StatusCode, 200, "status of the key")
		c.CheckEqual(len(body), 16, "key bytes")
	})

	// The key files are under hls_path, one for each 2 segments, each a
	// different 16-byte key, and the HTTP server serves them.
	check.Case(t, "KeyFiles", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publish7s(c, stream)
		dir := filepath.Join(srs.Dir, "objs", "nginx", "html")
		var keys [][]byte
		for _, seq := range []int{0, 2} {
			f := fmt.Sprintf("%v-%v.key", stream, seq)
			key, err := os.ReadFile(filepath.Join(dir, f))
			c.Must(err, "the key file "+f)
			c.CheckEqual(len(key), 16, "bytes of "+f)
			res, body := httpGet(c, httpClient, flvURL(srs, f))
			c.CheckEqual(res.StatusCode, 200, "status of "+f)
			c.Check(bytes.Equal(body, key), "%v served as %x, want the file's %x", f, body, key)
			keys = append(keys, key)
		}
		c.Check(!bytes.Equal(keys[0], keys[1]), "the keys of segments 0 and 2 are the same")
		_, err := os.Stat(filepath.Join(dir, stream+"-1.key"))
		c.Check(os.IsNotExist(err), "a key file for segment 1: %v, want none", err)
	})

	// A segment is not TS as served; decrypted with its key and IV it is a
	// segment that starts with a keyframe. The next key doesn't decrypt it.
	check.Case(t, "SegmentsDecrypt", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		pl := publish7s(c, stream)
		client := cdnClient(c, srs)
		keys := map[string][]byte{}
		for _, s := range pl.Segments[:3] {
			if !c.Check(s.Key != nil, "segment %v has no key", s.Sequence) {
				return
			}
			if _, ok := keys[s.Key.URI]; !ok {
				_, keys[s.Key.URI] = httpGet(c, client, s.Key.URI)
			}
			res, raw := httpGet(c, client, s.URI)
			c.CheckEqual(res.StatusCode, 200, "status of "+s.URI)
			c.Check(!isTS(raw), "segment %v is clear TS, want it encrypted", s.Sequence)

			plain, err := hls.Decrypt(keys[s.Key.URI], s, raw)
			noPaddingBug(c, err)
			c.Must(err, fmt.Sprintf("decrypt segment %v", s.Sequence))
			c.Check(isTS(plain), "segment %v decrypted is not TS", s.Sequence)
			r, err := ts.Analyze(ts.NewDemuxer(bytes.NewReader(plain)), report.Until{})
			c.Must(err, fmt.Sprintf("analyze segment %v", s.Sequence))
			checkPlayed(c, r, "h264", "aac")
			c.CheckEqual(r.Video.LeadingFrames, 0, fmt.Sprintf("frames before the first keyframe of segment %v", s.Sequence))
		}

		s0, s2 := pl.Segments[0], pl.Segments[2]
		_, raw := httpGet(c, client, s0.URI)
		wrong := hls.Segment{Sequence: s0.Sequence, Key: &hls.Key{Method: "AES-128", IV: s0.Key.IV}}
		plain, err := hls.Decrypt(keys[s2.Key.URI], wrong, raw)
		c.Check(err != nil || !isTS(plain), "segment 0 decrypts with the key of segment 2")
	})

	// A player follows the keys and plays the stream.
	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 7*time.Second)
		r := playHLS(c, cdnClient(c, srs), flvURL(srs, stream+".m3u8"), report.Until{VideoHeader: true, AudioHeader: true, Duration: 5 * time.Second})
		checkPlayed(c, r, "h264", "aac")
		c.Check(r.Video.Keyframes >= 5, "%v keyframes, want a 1s GOP over 5s", r.Video.Keyframes)
	})
}

// isTS reports whether b starts with three TS packets, each with its sync
// byte.
func isTS(b []byte) bool {
	const size = 188
	if len(b) < 3*size {
		return false
	}
	return b[0] == 0x47 && b[size] == 0x47 && b[2*size] == 0x47
}

// TestHls_Sessions checks the hls_ctx sessions, on by default: a master
// playlist that starts a session, a client and an on_play for each session,
// and a refused session that is not kept. Its server has the two session
// settings Common keeps at their defaults: hls_master_m3u8_path_relative
// on, and hls_ts_ctx off.
func TestHls_Sessions(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsFloor", sharedTsFloor())

	// publish publishes 3s, which closes a segment, and waits for the
	// playlist. It returns the stream's name and its playlist's URL.
	publish := func(c *check.C) (string, string) {
		name := streamName(c)
		publishThrough(c, srs, "live/"+name, media.H264HighAAC, 3*time.Second)
		u := flvURL(srs, "live/"+name+".m3u8")
		waitPlaylist(c, httpClient, u, hasSegment(0))
		return name, u
	}

	// The master playlist links the media playlist by a relative path, the
	// stream's file name and its hls_ctx.
	check.Case(t, "MasterRelative", check.Normal, func(c *check.C) {
		name, u := publish(c)
		uri := sessionURI(c, u)
		c.Check(strings.HasPrefix(uri, name+".m3u8?hls_ctx="), "media playlist %q, want %v.m3u8?hls_ctx=<session>", uri, name)
		ref, err := url.Parse(uri)
		c.Must(err, "parse "+uri)
		base, _ := url.Parse(u)
		pl := fetchPlaylist(c, base.ResolveReference(ref).String())
		c.Check(!pl.Master && len(pl.Segments) > 0, "playlist %+v, want the media playlist", pl)
	})

	// With hls_ts_ctx off, a session's media playlist lists the segments
	// without its hls_ctx.
	check.Case(t, "NoTsCtx", check.Normal, func(c *check.C) {
		_, u := publish(c)
		pl, mediaURL := waitPlaylist(c, httpClient, u, hasSegment(0))
		c.Check(query(c, mediaURL).Get("hls_ctx") != "", "media playlist %v, want an hls_ctx", mediaURL)
		for _, s := range pl.Segments {
			c.Check(!strings.Contains(s.URI, "hls_ctx"), "segment %v, want no hls_ctx", s.URI)
		}
	})

	// The query of the first request goes after the session's hls_ctx.
	check.Case(t, "QueryKept", check.Normal, func(c *check.C) {
		_, u := publish(c)
		master := sessionMaster(c, u+"?token=abc")
		q := query(c, master.Variants[0].URI)
		c.Check(q.Get("hls_ctx") != "", "media playlist %v, want an hls_ctx", master.Variants[0].URI)
		c.CheckEqual(q.Get("token"), "abc", "the token of the first request")
	})

	// A player may choose its session's hls_ctx: the master playlist links
	// the media playlist with it, which then answers the media playlist.
	check.Case(t, "ChosenCtx", check.Normal, func(c *check.C) {
		_, u := publish(c)
		ctx := sessionCtx()
		master := sessionMaster(c, u+"?hls_ctx="+ctx)
		c.CheckEqual(query(c, master.Variants[0].URI).Get("hls_ctx"), ctx, "hls_ctx of the media playlist")
		pl := fetchPlaylist(c, u+"?hls_ctx="+ctx)
		c.Check(!pl.Master && len(pl.Segments) > 0, "playlist %+v, want the media playlist", pl)
	})

	// Each session is an hls-play client of its stream in the HTTP API, by
	// its hls_ctx.
	check.Case(t, "ClientPerSession", check.Normal, func(c *check.C) {
		name, u := publish(c)
		var ctxs []string
		for range 2 {
			master := sessionMaster(c, u)
			ctx := query(c, master.Variants[0].URI).Get("hls_ctx")
			ctxs = append(ctxs, ctx)
			var v struct {
				Code   int `json:"code"`
				Client struct {
					ID      string `json:"id"`
					Type    string `json:"type"`
					URL     string `json:"url"`
					Publish bool   `json:"publish"`
				} `json:"client"`
			}
			apiGet(c, srs, "/api/v1/clients/"+ctx, &v)
			c.CheckEqual(v.Code, 0, "code of the session's client")
			c.CheckEqual(v.Client.ID, ctx, "client id")
			c.CheckEqual(v.Client.Type, "hls-play", "client type")
			c.CheckEqual(v.Client.URL, "/live/"+name, "client url")
			c.Check(!v.Client.Publish, "the session is a publisher")
		}
		c.Check(ctxs[0] != ctxs[1], "two sessions got the same hls_ctx %v", ctxs[0])
	})

	// on_play is called once when a session starts, with the session as its
	// client, not for each request in the session.
	check.Case(t, "OnPlayPerSession", check.Normal, func(c *check.C) {
		name, u := publish(c)
		// The wait for the playlist started a session of its own.
		before := len(hlsPlays(srs, name))
		master := sessionMaster(c, u)
		ctx := query(c, master.Variants[0].URI).Get("hls_ctx")
		for range 3 {
			fetchPlaylist(c, master.Variants[0].URI)
		}
		plays := hlsPlays(srs, name)[before:]
		if c.CheckEqual(len(plays), 1, "on_play of one session") {
			c.CheckEqual(plays[0].Field("client_id"), ctx, "on_play client_id")
			c.CheckEqual(plays[0].Field("app"), "live", "on_play app")
		}
		sessionMaster(c, u)
		c.CheckEqual(len(hlsPlays(srs, name))-before, 2, "on_play of two sessions")
	})

	// A session refused by on_play is not kept: a retry with the same
	// hls_ctx is checked again, and refused again, rather than served as a
	// session, and the API has no client for it.
	check.Case(t, "RefusedNotKept", check.Normal, func(c *check.C) {
		name, u := publish(c)
		before := len(hlsPlays(srs, name))
		for _, stream := range []string{name, name + ".m3u8"} {
			srs.Hooks.Script(mock.Match{Action: "on_play", Stream: stream}, mock.Reject(403))
		}
		ctx := sessionCtx()
		for i := range 2 {
			res, body := httpGet(c, httpClient, u+"?hls_ctx="+ctx)
			c.CheckEqual(res.StatusCode, 401, fmt.Sprintf("status of request %v", i+1))
			c.Check(!bytes.Contains(body, []byte("#EXTINF")), "request %v got the media playlist", i+1)
		}
		c.CheckEqual(len(hlsPlays(srs, name))-before, 2, "on_play requests")
		c.Check(apiCode(c, srs, "/api/v1/clients/"+ctx) != 0, "the refused session %v is a client", ctx)
	})
}

// sessionMaster gets an HLS URL that starts a session and returns its master
// playlist, which must link one media playlist.
func sessionMaster(c *check.C, raw string) *hls.Playlist {
	c.Helper()
	pl := fetchPlaylist(c, raw)
	if !c.Check(pl.Master && len(pl.Variants) == 1, "playlist %+v, want a master playlist with one variant", pl) {
		c.FailNow()
	}
	return pl
}

// sessionURI gets an HLS URL that starts a session and returns the URI of
// the master playlist's media playlist as it is written, not resolved.
func sessionURI(c *check.C, raw string) string {
	c.Helper()
	res, body := httpGet(c, httpClient, raw)
	c.CheckEqual(res.StatusCode, 200, "status of "+raw)
	for _, line := range strings.Split(string(body), "\n") {
		if line = strings.TrimSpace(line); line != "" && !strings.HasPrefix(line, "#") {
			return line
		}
	}
	c.Fatalf("no URI in the master playlist %q", body)
	return ""
}

// fetchPlaylist gets and parses a playlist, which must answer 200.
func fetchPlaylist(c *check.C, raw string) *hls.Playlist {
	c.Helper()
	res, body := httpGet(c, httpClient, raw)
	c.CheckEqual(res.StatusCode, 200, "status of "+raw)
	base, _ := url.Parse(raw)
	pl, err := hls.Parse(body, base)
	c.Must(err, "parse "+raw)
	return pl
}

// sessionCtx is a new hls_ctx of the case's own, 8 characters like the ones
// SRS makes.
func sessionCtx() string {
	b := make([]byte, 4)
	rand.Read(b)
	return hex.EncodeToString(b)
}

// hlsPlays is the on_play requests of a stream's HLS sessions, which SRS
// names by the playlist's file, so either name matches.
func hlsPlays(srs *server.Server, name string) []mock.Request {
	var out []mock.Request
	for _, r := range srs.Hooks.Requests(mock.Match{Action: "on_play"}) {
		if s := r.Stream(); s == name || s == name+".m3u8" {
			out = append(out, r)
		}
	}
	return out
}

// sharedFmp4Keys is the config of the server that TestHls_Fmp4Keys,
// TestHls_CtxOff, TestDash_StreamDir, TestRtc_Transport and
// TestRtc_PreferRtx share: HLS in fmp4 with 2s fragments and the init file
// beside the playlist, hls_keys with a new key every 2 segments at the
// default hls_key_url, hls_ctx off, every hook on a mock, DASH with 2s
// fragments and the MPD in a dir per stream, and WebRTC over UDP only with
// dtls_role active, stun_strict_check on, api_as_candidates off and
// nack_prefer_rtx on; TestSrt_LatencyOnly shares it too, for SRT with
// latency set alone, TestRtsp_VhostOff, for the RTSP listener with RTSP off
// on the vhost, and TestHttpStatic_CtxOff, for static HLS with hls_ctx off.
// HLS and DASH write different files, WebRTC, SRT and RTSP sessions write
// none, and the static cases write in dirs of their own, so none of the
// groups depends on what the others set.
func sharedFmp4Keys() server.Config {
	cfg := server.HLSConfig(server.HLS{
		FMP4:            true,
		Fragment:        2 * time.Second,
		InitFile:        "[app]/[stream]-init.mp4",
		Keys:            true,
		FragmentsPerKey: 2,
		NoCtx:           true,
	})
	cfg.Hooks = true
	cfg = server.WithDASH(cfg, server.DASH{
		Fragment: 2 * time.Second,
		MPDFile:  "[app]/[stream]/index.mpd",
	})
	cfg = server.WithSRT(server.WithRTC(cfg, sharedFmp4KeysRTC), sharedFmp4KeysSRT)
	return server.WithRTSP(cfg, server.RTSPOptions{VhostOff: true})
}

// TestHls_Fmp4Keys is hls_keys with fmp4 segments: SAMPLE-AES keys in the
// playlist and an init file that marks its tracks encrypted, cbcs.
func TestHls_Fmp4Keys(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())

	// publish publishes 9s at once, which closes the 3s segments 0 and 1,
	// and returns the playlist.
	publish := func(c *check.C, stream string) *hls.Playlist {
		publishThrough(c, srs, stream, media.H264HighAAC, 9*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(1))
		return pl
	}

	// Each segment has a SAMPLE-AES key with an IV, the first one for both,
	// and the key file is under hls_path.
	check.Case(t, "KeyTags", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		pl := publish(c, stream)
		for _, s := range pl.Segments[:2] {
			if !c.Check(s.Key != nil, "segment %v has no key", s.Sequence) {
				return
			}
			c.CheckEqual(s.Key.Method, "SAMPLE-AES", fmt.Sprintf("key method of segment %v", s.Sequence))
			c.CheckEqual(len(s.Key.IV), 16, fmt.Sprintf("IV bytes of segment %v", s.Sequence))
		}
		c.Check(pl.Segments[0].Key == pl.Segments[1].Key, "segment 1 has its own key tag, want the one of segment 0")
		key, err := os.ReadFile(filepath.Join(srs.Dir, "objs", "nginx", "html", stream+"-0.key"))
		c.Must(err, "the key file")
		c.CheckEqual(len(key), 16, "key bytes")
	})

	// The init file's tracks are encrypted sample entries of the cbcs
	// scheme: video with the 1:9 pattern, audio whole, and a constant IV.
	check.Case(t, "InitProtected", check.Normal, func(c *check.C) {
		pl := publish(c, "live/"+streamName(c))
		init := fmp4Init(c, pl)
		var tracks []string
		for _, t := range init.Tracks {
			p := t.Protection
			if !c.Check(p != nil, "the %v track %v is not protected", t.Handler, t.Codec) {
				continue
			}
			tracks = append(tracks, fmt.Sprintf("%v:%v(%v) %v %v:%v", t.Handler, t.Codec, p.Format, p.Scheme, p.CryptBlocks, p.SkipBlocks))
			c.CheckEqual(len(p.KID), 16, "KID bytes of the "+t.Handler+" track")
			c.CheckEqual(len(p.ConstantIV), 16, "constant IV bytes of the "+t.Handler+" track")
		}
		c.CheckEqual(fmt.Sprint(tracks), "[vide:encv(avc1) cbcs 1:9 soun:enca(mp4a) cbcs 0:0]", "tracks")
	})

	// The samples are encrypted: the first video and audio samples are not
	// the published frames in clear, have a senc entry, and decrypted by cbcs
	// with the key of the playlist and the init file's constant IV, which is
	// the playlist's IV, they are the published frames.
	check.Case(t, "SamplesEncrypted", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		pl := publish(c, stream)
		init := fmp4Init(c, pl)
		key, err := os.ReadFile(filepath.Join(srs.Dir, "objs", "nginx", "html", stream+"-0.key"))
		c.Must(err, "the key file")
		res, body := httpGet(c, httpClient, pl.Segments[0].URI)
		c.CheckEqual(res.StatusCode, 200, "status of segment 0")
		samples, err := mp4.ParseFragments(init, body)
		c.Must(err, "parse segment 0")

		for _, t := range init.Tracks {
			if k := pl.Segments[0].Key; t.Protection != nil && k != nil {
				c.Check(bytes.Equal(t.Protection.ConstantIV, k.IV), "the constant IV of the %v track is %x, the playlist's IV %x", t.Handler, t.Protection.ConstantIV, k.IV)
			}
		}

		video, audio := firstFrames(c)
		verify := func(s mp4.Sample, frame []byte, name string) {
			c.Check(!bytes.Equal(s.Data, frame), "the first %v sample, %v bytes, is the published frame in clear", name, len(s.Data))
			c.Check(s.Senc, "the first %v sample has no senc entry", name)
			clear, err := mp4.DecryptCBCS(s, key)
			if c.Check(err == nil, "decrypt the first %v sample: %v", name, err) {
				c.Check(bytes.Equal(clear, frame), "the first %v sample, decrypted, is not the published frame", name)
			}
		}
		for _, s := range samples {
			switch {
			case s.Track.Handler == "vide" && video != nil:
				verify(s, video, "video")
				video = nil
			case s.Track.Handler == "soun" && audio != nil:
				verify(s, audio, "audio")
				audio = nil
			}
		}
		c.Check(video == nil && audio == nil, "segment 0 has no video or audio sample")
	})

	// The key URIs of the playlist, at the default hls_key_url, are served.
	check.Case(t, "DefaultKeyUrl", check.Normal, func(c *check.C) {
		check.KnownBug(c, "the default hls_key_url is hls_path, a dir on disk, which SRS puts before the key file without a slash, so the key URI is not served")
		pl := publish(c, "live/"+streamName(c))
		k := pl.Segments[0].Key
		if !c.Check(k != nil, "segment 0 has no key") {
			return
		}
		res, body := httpGet(c, httpClient, k.URI)
		c.CheckEqual(res.StatusCode, 200, "status of the key "+k.URI)
		c.CheckEqual(len(body), 16, "key bytes")
	})
}

// fmp4Init fetches and parses the init file of a playlist's first segment.
func fmp4Init(c *check.C, pl *hls.Playlist) *mp4.Init {
	c.Helper()
	if !c.Check(len(pl.Segments) > 0 && pl.Segments[0].Map != nil, "no #EXT-X-MAP: %+v", pl) {
		c.FailNow()
	}
	res, body := httpGet(c, httpClient, pl.Segments[0].Map.URI)
	c.CheckEqual(res.StatusCode, 200, "status of the init file")
	init, err := mp4.ParseInit(body)
	c.Must(err, "parse the init file")
	return init
}

// firstFrames is the first video frame and audio frame of the test file, as
// an MP4 sample has them: the length-prefixed NALUs of the keyframe, and the
// raw AAC frame.
func firstFrames(c *check.C) (video, audio []byte) {
	c.Helper()
	for _, tag := range readTags(c, media.H264HighAAC, 0, time.Second) {
		switch {
		case tag.Type == 9 && video == nil && len(tag.Data) > 5 && tag.Data[1] == 1:
			video = tag.Data[5:]
		case tag.Type == 8 && audio == nil && len(tag.Data) > 2 && tag.Data[1] == 1:
			audio = tag.Data[2:]
		}
	}
	c.Check(video != nil && audio != nil, "no video or audio frame in the first second of the test file")
	return video, audio
}

// TestHls_CtxOff is HLS with hls_ctx off: the playlist is the media playlist,
// with no session, client or on_play for its players.
func TestHls_CtxOff(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())

	check.Case(t, "NoMaster", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		publishThrough(c, srs, stream, media.H264HighAAC, 4*time.Second)
		pl, mediaURL := waitPlaylist(c, httpClient, flvURL(srs, stream+".m3u8"), hasSegment(0))
		c.CheckEqual(mediaURL, flvURL(srs, stream+".m3u8"), "the media playlist's URL")
		c.Check(!pl.Master, "a master playlist")
		for _, s := range pl.Segments {
			c.Check(!strings.Contains(s.URI, "hls_ctx"), "segment %v, want no hls_ctx", s.URI)
		}
	})

	check.Case(t, "NoSession", check.Normal, func(c *check.C) {
		name := streamName(c)
		publishThrough(c, srs, "live/"+name, media.H264HighAAC, 4*time.Second)
		pl, _ := waitPlaylist(c, httpClient, flvURL(srs, "live/"+name+".m3u8"), hasSegment(0))
		res, _ := httpGet(c, httpClient, pl.Segments[0].URI)
		c.CheckEqual(res.StatusCode, 200, "status of segment 0")

		c.CheckEqual(len(hlsPlays(srs, name)), 0, "on_play requests")
		var v struct {
			Clients []struct {
				Type string `json:"type"`
				URL  string `json:"url"`
			} `json:"clients"`
		}
		apiGet(c, srs, "/api/v1/clients/?count=1000", &v)
		for _, cl := range v.Clients {
			c.Check(!(cl.Type == "hls-play" && cl.URL == "/live/"+name), "an hls-play client of the stream: %+v", cl)
		}
	})
}
