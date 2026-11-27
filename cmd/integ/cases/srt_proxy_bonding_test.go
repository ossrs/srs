// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"os"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/mock"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// TestTimer_Srt_ProxyBonding checks a bonded SRT publish through the Go
// proxy, to two origins with srt_server.bonding on. Both origins are fresh,
// so a proxy picks one at random for each stream, and pins the stream to it;
// each link of a group is a connection of its own to a proxy, with its own
// socket to the origin. For each client that bonds and each group mode:
//   - the proxy with the memory load balancer, a group of two links
//     publishes through it: both links reach the origin picked for the
//     stream, where the group is one publisher, with one on_connect and one
//     on_publish, and the other origin gets nothing of the stream; a player
//     through the proxy gets every message once;
//   - each link through a relay to that proxy, the one that carries the most
//     media blackholed mid-stream: the player through the proxy gets every
//     message once, in order, with no wait longer than srtBondGap (a backup
//     group, F14), and the group stays the one publisher;
//   - two proxies sharing Redis, one link of the group through each, dialed
//     at once as an encoder does: both links reach one origin, the same
//     checks, with the player through the second proxy (F16);
//   - the same, the second link's handshake 30ms later, so the first proxy
//     has pinned the stream, and a link blackholed, the link through each
//     proxy in turn for broadcast and the active link for backup: the stream
//     goes on over the link through the other proxy.
//
// Several proxies need Redis for bonding: each proxy of the memory load
// balancer pins a stream on its own, so two links of a group through two
// such proxies may reach two origins, each a group of its own there with
// one link.
//
// Like TestTimer_Srt_Proxy, the cases run only with $SRT_PROXY, and the
// Redis cases with $SRT_PROXY_REDIS and $SRT_PROXY_REDIS_PREFIX, as
// skills/srs-develop/scripts/proxy-e2e-srt-bonding-test.sh does.
func TestTimer_Srt_ProxyBonding(t *testing.T) {
	t.Parallel()
	bin := os.Getenv(srtProxyEnv)
	if bin == "" {
		t.Skip("the proxy bonding cases run with " + srtProxyEnv + ", the srs-proxy binary: bash skills/srs-develop/scripts/proxy-e2e-srt-bonding-test.sh")
	}

	cfg := srtBondConfig(server.SRTOptions{PeerIdleTimeout: srtFailIdle})
	cfg.Listeners = append(cfg.Listeners, server.RTMP)
	origin1, origin2 := server.Group(t, cfg), server.Group(t, cfg)

	proxy := startSRTProxy(t, bin, "bonding", nil)
	proxy.register(t, origin1, "origin1")
	proxy.register(t, origin2, "origin2")

	for _, cl := range srtBondClients {
		for _, mode := range srtBondModes {
			check.Case(t, "Publish_"+string(mode)+"_"+cl.Name(), check.Normal, func(c *check.C) {
				links := []string{proxy.srtAddr(), proxy.srtAddr()}
				checkSRTProxyBondPublish(c, dialSRTBond(c, links, cl, mode, nil), proxy, origin1, origin2)
			})

			check.Case(t, "LinkBlackholed_"+string(mode)+"_"+cl.Name(), check.Timer, func(c *check.C) {
				links := []string{proxy.srtAddr(), proxy.srtAddr()}
				run := dialSRTBond(c, links, cl, mode, make([]*srt.Impair, 2))
				origin, other := srtProxyOriginOf(c, "on_publish", run.name, origin1, origin2)
				run.playFrom(c, origin, proxy.srtAddr(), srt.GoSRT)
				checkSRTBondOutage(c, origin, run, mode)
				checkSRTProxyOtherOrigin(c, other, run.name)
			})
		}
	}

	redisA, redisB := startSRTProxyRedis(t, bin, origin1, origin2)
	skipRedis := func(c *check.C) {
		if redisA == nil {
			c.Skip("the Redis cases run with " + srtProxyRedisEnv + " and " + srtProxyPrefixEnv)
		}
	}
	for _, cl := range srtBondClients {
		for _, mode := range srtBondModes {
			check.Case(t, "Redis_Publish_"+string(mode)+"_"+cl.Name(), check.Normal, func(c *check.C) {
				skipRedis(c)
				links := []string{redisA.srtAddr(), redisB.srtAddr()}
				run := dialSRTBond(c, links, cl, mode, nil)
				if run.pub.Links() < len(links) {
					// The caller's libsrt refuses a link whose answer comes from
					// another origin's group than the first link's. The stream is
					// pinned to one of the two, so a player may find no publisher:
					// the case ends here.
					c.Logf("the group has %v of %v links: the proxies picked two origins", run.pub.Links(), len(links))
					check.KnownBug(c, srtProxyRedisPickBug)
					c.CheckEqual(run.pub.Links(), len(links), "links of the group connected")
					return
				}
				checkSRTProxyBondPublish(c, run, redisB, origin1, origin2)
			})
		}

		// A broadcast group's links carry the same, so each is blackholed in
		// a case of its own; a backup group's standby link carries no media to
		// lose, so only its active link is.
		for i, through := range []string{"ProxyA", "ProxyB"} {
			check.Case(t, "Redis_LinkBlackholed_"+string(srt.GroupBroadcast)+"_"+through+"_"+cl.Name(), check.Timer, func(c *check.C) {
				skipRedis(c)
				run := startSRTProxyRedisBond(c, cl, srt.GroupBroadcast, redisA, redisB, origin1, origin2)
				checkSRTBondOutageOf(c, run.origin, run.srtBondRun, srt.GroupBroadcast, func() *srt.Impair { return run.relays[i] })
				checkSRTProxyOtherOrigin(c, run.other, run.name)
			})
		}
		check.Case(t, "Redis_LinkBlackholed_"+string(srt.GroupBackup)+"_"+cl.Name(), check.Timer, func(c *check.C) {
			skipRedis(c)
			run := startSRTProxyRedisBond(c, cl, srt.GroupBackup, redisA, redisB, origin1, origin2)
			checkSRTBondOutage(c, run.origin, run.srtBondRun, srt.GroupBackup)
			checkSRTProxyOtherOrigin(c, run.other, run.name)
		})
	}
}

// srtProxyRedisPickBug is F16: the Redis load balancer pins a stream to an
// origin by a get, a random pick and a set, not at once, so two proxies
// that pick a new stream at the same time may each pick another origin
// (internal/lb/redis.go Pick), as the two links of a group do.
const srtProxyRedisPickBug = "F16 two proxies sharing Redis that pick a new stream at once may pick two origins"

// srtProxyRedisLater is how much later the second link's handshake reaches
// its proxy than the first's, so the first proxy has pinned the stream in
// Redis when the second picks it (F16).
const srtProxyRedisLater = 30 * time.Millisecond

// srtProxyBondRun is a group publishing through proxies, with the origin
// the stream went to and the other one.
type srtProxyBondRun struct {
	*srtBondRun
	origin, other *server.Server
}

// startSRTProxyRedisBond starts a group of cl in mode with one link through
// a relay to each of the proxies a and b, which share Redis, the link
// through b srtProxyRedisLater, and a gosrt player through b of the origin
// the stream went to.
func startSRTProxyRedisBond(c *check.C, cl srt.GroupClient, mode srt.GroupMode, a, b *srtProxy, origin1, origin2 *server.Server) *srtProxyBondRun {
	c.Helper()
	var links []string
	var impairs []*srt.Impair
	for i, addr := range []string{a.srtAddr(), b.srtAddr()} {
		relay, err := srt.NewImpair(addr, time.Now().UnixNano()+int64(i))
		c.Must(err, "start the relay")
		c.Cleanup(func() { relay.Close() })
		impairs = append(impairs, relay)
		links = append(links, relay.Addr().String())
	}
	impairs[1].Set(srt.Up, srt.Shape{Delay: srtProxyRedisLater})
	run := dialSRTBond(c, links, cl, mode, nil)
	impairs[1].Set(srt.Up, srt.Shape{})
	run.relays = impairs
	c.CheckEqual(run.pub.Links(), len(links), "links of the group connected")
	origin, other := srtProxyOriginOf(c, "on_publish", run.name, origin1, origin2)
	run.playFrom(c, origin, b.srtAddr(), srt.GoSRT)
	return &srtProxyBondRun{srtBondRun: run, origin: origin, other: other}
}

// checkSRTProxyBondPublish checks a group that dialed its links through
// proxies to origin1 or origin2: both links connected, the group is one
// publisher of the origin it went to, with one on_connect and one
// on_publish, the other origin got nothing of the stream, and a gosrt player
// through player gets every message once.
func checkSRTProxyBondPublish(c *check.C, run *srtBondRun, player *srtProxy, origin1, origin2 *server.Server) {
	c.Helper()
	c.CheckEqual(run.pub.Links(), 2, "links of the group connected")
	origin, other := srtProxyOriginOf(c, "on_publish", run.name, origin1, origin2)
	run.playFrom(c, origin, player.srtAddr(), srt.GoSRT)
	run.send(c, srtLead)
	run.waitWhole("the group's media", run.sent.messages()).check(c)

	h := waitHook(c, origin, "on_publish", run.name)
	c.CheckEqual(srtHooksOf(origin, h.Field("client_id")), "on_connect,on_publish", "the group's hooks")
	c.CheckEqual(len(origin.Hooks.Requests(mock.Match{Action: "on_publish", Stream: run.name})), 1, "on_publish hooks")
	checkClient(c, apiClientOf(c, origin, h.Field("client_id")),
		clientWant{typ: "srt-publish", name: run.name, url: "/live/" + run.name, publish: true})
	c.CheckEqual(streamClients(c, origin, "/api/v1/streams/"+h.Field("stream_id")), 2, "stream clients, the group and the player")
	checkSRTProxyOtherOrigin(c, other, run.name)
}

// checkSRTProxyOtherOrigin checks that the origin the proxy did not pick
// for a stream got no hook of it: a link of the group sent there would be a
// group of its own, with its own on_connect and on_publish.
func checkSRTProxyOtherOrigin(c *check.C, other *server.Server, stream string) {
	c.Helper()
	c.CheckEqual(len(other.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the stream on the other origin")
}
