// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"fmt"
	"net/http"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// The clients of the security cases are the hook cases' clients: each
// publishes or plays and reports whether SRS accepts or refuses it. Every
// client of a case connects from 127.0.0.1, or from ::1 over IPv6.

// securityConfig is a server with a listener for every protocol, HLS, an
// HTTP-FLV mount, the hook mock that accepts every client, and the security
// rules s.
func securityConfig(s server.Security) server.Config {
	cfg := server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI, server.HTTPServer, server.RTMPS},
		Hooks:     true,
		IPv6:      true,
		Env: map[string]string{
			"SRS_VHOST_MIN_LATENCY":        "on",
			"SRS_VHOST_PLAY_MW_LATENCY":    "0",
			"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
			"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream].flv",
			"SRS_VHOST_HLS_ENABLED":        "on",
		},
	}
	cfg = server.WithRTC(cfg, server.RTC{})
	cfg = server.WithSRT(cfg, server.SRTOptions{})
	cfg = server.WithRTSP(cfg, server.RTSPOptions{})
	cfg = server.WithGB(cfg, server.GBOptions{})
	return server.WithSecurity(cfg, s)
}

// securityPublishers are the publishers of every protocol that publishes.
func securityPublishers() []hookTarget {
	return []hookTarget{
		rtmpHookTarget("RtmpPublish", "on_publish", false, true),
		rtmpHookTarget("RtmpsPublish", "on_publish", true, true),
		srtPublishTarget("SrtPublish", "on_publish"),
		rtcHookTarget(true),
		gbHookTarget(),
	}
}

// securityPlayers are the players of every protocol, each with a publisher
// of the stream it plays, so a player SRS refuses is refused for itself, not
// for a stream that isn't there.
func securityPlayers() []hookTarget {
	rtmpPrepare := func(c *check.C, srs *server.Server, stream string) {
		startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)
		waitHook(c, srs, "on_publish", stream)
	}
	whep := rtcHookTarget(false)
	whep.prepare = func(c *check.C, srs *server.Server, stream string) {
		sendRTC(c, rtcPublisher(c, whipURL(srs, "live", stream), nil))
		waitHook(c, srs, "on_publish", stream)
	}
	rtsp := rtspHookTarget()
	rtsp.prepare = func(c *check.C, srs *server.Server, stream string) {
		rtspPublisher(c, srs, "live/"+stream, media.H264BaselineAAC)
		waitHook(c, srs, "on_publish", stream)
	}
	rtmpPlay := rtmpHookTarget("RtmpPlay", "on_play", false, false)
	rtmpPlay.prepare = rtmpPrepare
	rtmpsPlay := rtmpHookTarget("RtmpsPlay", "on_play", true, false)
	rtmpsPlay.prepare = rtmpPrepare
	return []hookTarget{rtmpPlay, rtmpsPlay, srtPlayTarget(), whep, httpHookTarget(flvHookMount), hlsHookTarget(), rtsp}
}

// securityCase publishes or plays a stream of the case's own: SRS accepts
// the client when allowed, and else refuses it, with the refusal of the
// protocol when want names one, and goes on. A publisher SRS refuses leaves
// the stream unpublished.
func securityCase(c *check.C, srs *server.Server, target hookTarget, allowed bool, want string) {
	name := streamName(c)
	if target.prepare != nil {
		target.prepare(c, srs, name)
	}
	r := target.try(c, srs, name, hookWait)
	if allowed {
		c.Check(r.accepted, "%s, want accepted", r)
	} else {
		checkRefused(c, r, want)
		if target.hook == "on_publish" {
			c.Check(!streamPublishing(c, srs, "live/"+name), "the stream is published, want it not")
		}
	}
	checkVersions(c, srs)
}

// securityRefusals are how SRS refuses a client the security denies, by the
// client's name: RTMP and SRT by a close, after the SRT handshake, WHIP with
// 401 and RTSP with 403; a GB28181 stream is never published. A refusal not
// listed is any refusal: the answer of a WHEP, HTTP-FLV or HLS player has a
// known-bug case of its own.
var securityRefusals = map[string]string{
	"RtmpPublish": "closed", "RtmpsPublish": "closed", "SrtPublish": "closed", "WhipPublish": "status 401",
	"GbPublish": "not published", "RtmpPlay": "closed", "RtmpsPlay": "closed", "SrtPlay": "closed",
	"RtspPlay": "status 403",
}

// TestSecurity_DenyPublish denies publishing from 127.0.0.1 and allows it
// from all: a deny rule wins over an allow rule, so every publisher on IPv4
// is refused, and one on IPv6 is not. Playing is allowed from all.
func TestSecurity_DenyPublish(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, securityConfig(server.Security{
		AllowPublish: []string{"all"}, DenyPublish: []string{"127.0.0.1"}, AllowPlay: []string{"all"},
	}))

	for _, target := range securityPublishers() {
		check.Case(t, target.name+"Denied", check.Normal, func(c *check.C) {
			securityCase(c, srs, target, false, securityRefusals[target.name])
		})
	}

	// RtmpPublishIpv6Allowed publishes over ::1, which the deny rule doesn't
	// name, so allow publish all allows it, and a player on 127.0.0.1 plays
	// it.
	check.Case(t, "RtmpPublishIpv6Allowed", check.Normal, func(c *check.C) {
		if !server.HasIPv6() {
			c.Skip("the host has no IPv6 loopback")
		}
		path := "live/" + streamName(c)
		startPublisher(c, "rtmp://[::1]:"+port(srs)+"/"+path, media.Path(c, media.H264HighAAC), 200*time.Millisecond)
		playChecked(c, rtmpURL(srs, path))
	})

	// RtmpPlayAllowed plays a stream before its publish: allow play all lets
	// the player in, and it waits for the stream.
	check.Case(t, "RtmpPlayAllowed", check.Normal, func(c *check.C) {
		securityCase(c, srs, rtmpHookTarget("RtmpPlay", "on_play", false, false), true, "")
	})
}

// TestSecurity_DenyPlay allows publishing from 127.0.0.0/8, a CIDR, and
// denies playing from all: every publisher is accepted, and every player
// refused, though its stream is published.
func TestSecurity_DenyPlay(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, securityConfig(server.Security{
		AllowPublish: []string{"127.0.0.0/8"}, DenyPlay: []string{"all"},
	}))

	for _, target := range securityPublishers() {
		check.Case(t, target.name+"Allowed", check.Normal, func(c *check.C) {
			securityCase(c, srs, target, true, "")
		})
	}
	for _, target := range securityPlayers() {
		check.Case(t, target.name+"Denied", check.Normal, func(c *check.C) {
			securityCase(c, srs, target, false, securityRefusals[target.name])
		})
	}

	// The DeniedAnswer cases check that an HTTP player the security denies
	// is answered with a client error, as RTSP answers 403 and WHIP 401: a
	// refused client is not a server fault.
	for _, target := range securityPlayers() {
		reason, ok := securityAnswerBugs[target.name]
		if !ok {
			continue
		}
		check.Case(t, target.name+"DeniedAnswer", check.Normal, func(c *check.C) {
			check.KnownBug(c, reason)
			name := streamName(c)
			target.prepare(c, srs, name)
			r := target.try(c, srs, name, hookWait)
			c.Check(strings.HasPrefix(r.refusal, "status 4"), "%s, want a status 4xx", r)
		})
	}
}

// securityAnswerBugs are the HTTP players a security deny isn't answered with
// a client error, by name.
var securityAnswerBugs = map[string]string{
	"WhepPlay":    "a WHEP player the security denies is answered 500, a WHIP publisher 401",
	"HttpFlvPlay": "an HTTP-FLV player the security denies gets no HTTP answer, the connection closed",
	"HlsPlay":     "an HLS player the security denies gets no HTTP answer, the connection closed",
}

// TestSecurity_AllowOthers allows publishing only from 10.0.0.0/8 and
// playing only from 127.0.0.1: a publisher on 127.0.0.1, which no allow rule
// matches, is refused, and a player there, which an IP names, is let in.
func TestSecurity_AllowOthers(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, securityConfig(server.Security{
		AllowPublish: []string{"10.0.0.0/8"}, AllowPlay: []string{"127.0.0.1"},
	}))

	for _, target := range securityPublishers() {
		check.Case(t, target.name+"NotAllowed", check.Normal, func(c *check.C) {
			securityCase(c, srs, target, false, securityRefusals[target.name])
		})
	}
	for _, target := range []hookTarget{
		rtmpHookTarget("RtmpPlay", "on_play", false, false),
		rtmpHookTarget("RtmpsPlay", "on_play", true, false),
	} {
		check.Case(t, target.name+"Allowed", check.Normal, func(c *check.C) {
			securityCase(c, srs, target, true, "")
		})
	}
}

// TestSecurity_NoRules turns the security on with no rule: SRS denies every
// client, a publisher and a player alike.
func TestSecurity_NoRules(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.WithSecurity(server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI},
	}, server.Security{}))

	check.Case(t, "RtmpPublishDenied", check.Normal, func(c *check.C) {
		securityCase(c, srs, rtmpHookTarget("RtmpPublish", "", false, true), false, "closed")
	})
	check.Case(t, "RtmpPlayDenied", check.Normal, func(c *check.C) {
		securityCase(c, srs, rtmpHookTarget("RtmpPlay", "", false, false), false, "closed")
	})
}

// A securityPublisher publishes a stream over one protocol: first holds it,
// and second tries to publish it while another protocol holds it.
type securityPublisher struct {
	name   string
	first  func(c *check.C, srs *server.Server, stream string)
	second func(c *check.C, srs *server.Server, stream, firstID string)
}

// securitySecondPublishers are the publishers of every protocol that
// publishes, for the second-publisher cases on Common.
var securitySecondPublishers = []securityPublisher{
	{
		name: "Rtmp",
		first: func(c *check.C, srs *server.Server, stream string) {
			startPublisher(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond)
		},
		second: func(c *check.C, srs *server.Server, stream, _ string) {
			checkSecondRejected(c, rtmpURL(srs, "live/"+stream), nil)
		},
	},
	{
		name: "Rtmps",
		first: func(c *check.C, srs *server.Server, stream string) {
			startPublisherWith(c, rtmpsURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 200*time.Millisecond, tlsOpts(c), rtmp.FMLE)
		},
		second: func(c *check.C, srs *server.Server, stream, _ string) {
			checkSecondRejected(c, rtmpsURL(srs, "live/"+stream), tlsOpts(c))
		},
	},
	{
		name: "Whip",
		first: func(c *check.C, srs *server.Server, stream string) {
			sendRTC(c, rtcPublisher(c, whipURL(srs, "live", stream), nil))
		},
		second: func(c *check.C, srs *server.Server, stream, _ string) {
			offer := rtcOffer(c, true, []string{"H264"}, []string{"opus"})
			rtcRefused(c, whipURL(srs, "live", stream), offer, http.StatusConflict)
		},
	},
	{
		name: "Srt",
		first: func(c *check.C, srs *server.Server, stream string) {
			startSRTPublisher(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}, media.H264HighAACTS)
		},
		second: func(c *check.C, srs *server.Server, stream, _ string) {
			checkSRTClosedEarly(c, dialSRTEarlyClose(c, srs, srt.Options{StreamID: srtID("live/"+stream, "publish")}))
		},
	},
	{
		name: "Gb",
		first: func(c *check.C, srs *server.Server, stream string) {
			gbStartID(c, srs, media.H264BaselineAAC, stream, gbSSRC()).lead(c)
		},
		// The device sees nothing: SRS connects the RTMP client it publishes
		// the stream with, refuses it as busy before its on_publish, and it
		// closes.
		second: func(c *check.C, srs *server.Server, stream, firstID string) {
			gbStartID(c, srs, media.H264BaselineAAC, stream, gbSSRC()).lead(c)
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			conn, err := srs.Hooks.Wait(ctx, mock.Match{Action: "on_connect", Stream: stream, Func: func(r mock.Request) bool {
				return r.Field("client_id") != firstID
			}})
			c.Must(err, "the on_connect of the GB28181 stream's RTMP client")
			id := conn.Field("client_id")
			_, err = srs.Hooks.Wait(ctx, mock.Match{Action: "on_close", Func: func(r mock.Request) bool { return r.Field("client_id") == id }})
			c.Must(err, "the on_close of the GB28181 stream's RTMP client")
			c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 1, "on_publish requests, the first's")
		},
	},
}

// securitySecond adds to TestSecurity_Common a case for each pair of
// publishers of different protocols: the first publishes a stream, and SRS
// refuses the second, with the second protocol's refusal, while the first
// stays the stream's publisher.
func securitySecond(t *testing.T) {
	for _, first := range securitySecondPublishers {
		for _, second := range securitySecondPublishers {
			if first.name == second.name {
				continue
			}
			check.Case(t, fmt.Sprintf("%sThen%sRejected", first.name, second.name), check.Normal, func(c *check.C) {
				srs := server.Common(c)
				stream := streamName(c)
				first.first(c, srs, stream)
				pub := waitHook(c, srs, "on_publish", stream)
				second.second(c, srs, stream, pub.Field("client_id"))
				st := apiStreamOf(c, srs, pub.Field("stream_id"))
				p := object(c, st, "publish")
				c.CheckEqual(p["active"], true, "the stream is published")
				c.CheckEqual(p["cid"], pub.Field("client_id"), "the stream's publisher, the first")
				checkVersions(c, srs)
			})
		}
	}
}

// TestSecurity_Common has the security cases on the Common server, whose
// security is off.
func TestSecurity_Common(t *testing.T) {
	t.Parallel()
	securitySecond(t)
}
