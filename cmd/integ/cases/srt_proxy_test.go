// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"

	"github.com/datarhei/gosrt/packet"
)

// The proxy cases run the Go proxy, srs-proxy, between the clients and SRS,
// and only when $SRT_PROXY names its binary, as
// skills/srs-develop/scripts/proxy-e2e-srt-impair-test.sh does: the suite
// tests SRS, and the proxy is another program. $SRT_PROXY_REDIS, a
// host:port, and $SRT_PROXY_REDIS_PREFIX, the prefix of every key, turn the
// Redis cases on; the script sets them and removes the keys.
const (
	srtProxyEnv       = "SRT_PROXY"
	srtProxyRedisEnv  = "SRT_PROXY_REDIS"
	srtProxyPrefixEnv = "SRT_PROXY_REDIS_PREFIX"
)

// srtProxyTTL is the origin TTL of the proxy of TestTimer_Srt_Proxy/TwoOrigins:
// an origin registered longer ago is picked for a new stream only when no
// other is fresh, so a case picks the origin of each stream by when it
// registers it.
const srtProxyTTL = 400 * time.Millisecond

// srtProxyIdle is the peer_idle_timeout of the origins, longer than FFmpeg
// takes to connect, 1s of media after it starts: SRS closes a player of a
// stream nobody publishes after that long.
const srtProxyIdle = 2 * time.Second

// srtProxyLeakBug is F10: the proxy never frees an SRT connection: the
// UDP socket it dialed to the origin and its reader stay after the client
// and SRS have closed (internal/proxy/srt.go, "Should cleanup the dead SRT
// connection").
const srtProxyLeakBug = "F10 the Go proxy never frees an SRT connection's socket to the origin"

// srtProxyStallBug is F11: a caller that sends its conclusion again, as
// one does whose answer was lost, stalls the proxy's SRT port: the proxy
// does the origin's handshake once more on the connection's socket, whose
// reader takes the origin's answers and relays them to the caller as they
// are (an induction answer, not a conclusion), and the proxy's one receive
// loop waits for an answer until the origin sends the connection anything
// else, so no other caller's packet is relayed meanwhile.
const srtProxyStallBug = "F11 a repeated SRT conclusion stalls the Go proxy's SRT port"

// srtProxyListeners are the proxy's listeners, each on a port of its own:
// it starts them all.
var srtProxyListeners = []string{"PROXY_RTMP_SERVER", "PROXY_HTTP_API", "PROXY_HTTP_SERVER", "PROXY_WEBRTC_SERVER", "PROXY_SRT_SERVER", "PROXY_SYSTEM_API"}

// TestTimer_Srt_Proxy checks SRT through the Go proxy, which relays each
// connection's UDP to the origin its load balancer picks for the stream:
//   - gosrt, libsrt v1.5.7 and FFmpeg publish through the proxy, and a gosrt
//     player through it plays;
//   - 5% loss each way between a publisher or a player and the proxy: every
//     message arrives, in order (F4, as without the proxy);
//   - a publisher's network dies: the origin closes it after
//     peer_idle_timeout, and a new publish through the proxy plays;
//   - two origins: each stream goes to the origin the proxy picked for it,
//     and its player follows it there, even once that origin is stale;
//   - two proxies sharing Redis: a player through one and a publisher
//     through the other reach the same origin;
//   - a caller sends its conclusion again: the proxy answers it, and another
//     caller connects meanwhile (F11);
//   - every client closed: the proxy keeps each connection's socket to the
//     origin (F10).
//
// The cases register the origins with the proxies by the proxy's API, as an
// origin's heartbeat does, so a case knows which origins a proxy has.
func TestTimer_Srt_Proxy(t *testing.T) {
	t.Parallel()
	bin := os.Getenv(srtProxyEnv)
	if bin == "" {
		t.Skip("the proxy cases run with " + srtProxyEnv + ", the srs-proxy binary: bash skills/srs-develop/scripts/proxy-e2e-srt-impair-test.sh")
	}

	cfg := srtImpairConfig(server.SRTOptions{PeerIdleTimeout: srtProxyIdle})
	cfg.Listeners = append(cfg.Listeners, server.RTMP)
	origin1, origin2 := server.Group(t, cfg), server.Group(t, cfg)

	one := startSRTProxy(t, bin, "one", nil)
	one.register(t, origin1, "origin1")

	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt, srt.FFmpeg} {
		class := check.Normal
		if srtInteropSlow(cl) {
			class = check.Timer
		}
		check.Case(t, "Publish_"+cl.Name(), class, func(c *check.C) {
			stream := streamName(c)
			play := dialSRTInterop(c, srt.GoSRT, one.srtAddr(), srt.Options{StreamID: srtID("live/"+stream, "request")})
			waitHook(c, origin1, "on_play", stream)
			publishSRTInterop(c, cl, one.srtAddr(), srt.Options{StreamID: srtID("live/"+stream, "publish")})
			checkSRTPlayed(c, srtAnalyzeUntil(c, play, srtPlayedUntil, srtInteropStep(cl)))
		})
	}

	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt} {
		for _, leg := range srtImpairLegs {
			check.Case(t, "Loss5_"+leg+"_"+cl.Name(), check.Timer, func(c *check.C) {
				run := startSRTImpairAt(c, origin1, one.srtAddr(), cl, srt.GoSRT, leg, srtImpairLatency)
				run.impairBoth(srt.Shape{Loss: 5})
				checkSRTWhole(c, run.realTime(c))
				run.checkPublishing(c, origin1)
			})
		}

		check.Case(t, "PublisherBlackholed_"+cl.Name(), check.Timer, func(c *check.C) {
			run := startSRTImpairAt(c, origin1, one.srtAddr(), cl, srt.GoSRT, "Publisher", srtImpairLatency)
			run.stream()
			time.Sleep(100 * time.Millisecond)
			run.relay.Blackhole(true)
			dead := time.Now()

			unpublished := waitHookWithin(c, origin1, "on_unpublish", run.name, 3*srtProxyIdle)
			idle := unpublished.Time.Sub(dead)
			c.Check(idle >= srtProxyIdle-100*time.Millisecond && idle <= srtProxyIdle+run.latency+500*time.Millisecond,
				"the publisher closed %v after its network died, want about %v", idle, srtProxyIdle)

			// The new player's on_play is the stream's second.
			play := dialSRTAt(c, srt.GoSRT, one.srtAddr(), srt.Options{StreamID: srtID("live/"+run.name, "request")})
			waitHookCount(c, origin1, mock.Match{Action: "on_play", Stream: run.name}, 2)
			publishSRTInterop(c, srt.GoSRT, one.srtAddr(), srt.Options{StreamID: srtID("live/"+run.name, "publish")})
			checkSRTPlayed(c, srtAnalyze(c, play))
			c.CheckEqual(len(origin1.Hooks.Requests(mock.Match{Action: "on_publish", Stream: run.name})), 2, "on_publish hooks")
		})
	}

	two := startSRTProxy(t, bin, "two", map[string]string{"PROXY_ORIGIN_SERVER_TTL": srtProxyTTL.String()})
	check.Case(t, "TwoOrigins", check.Timer, func(c *check.C) {
		name := streamName(c)
		first, second := name+"-1", name+"-2"

		// Only origin1 is registered: the first stream goes there. Its player
		// comes first, so it gets the stream from its start.
		two.register(c, origin1, "origin1")
		play1 := dialSRTAt(c, srt.GoSRT, two.srtAddr(), srt.Options{StreamID: srtID("live/"+first, "request")})
		waitHook(c, origin1, "on_play", first)
		publishSRTInterop(c, srt.GoSRT, two.srtAddr(), srt.Options{StreamID: srtID("live/"+first, "publish")})
		waitHook(c, origin1, "on_publish", first)

		// origin1 is stale once origin2 registers: the second stream goes to
		// origin2, and the first stays on origin1.
		time.Sleep(srtProxyTTL)
		two.register(c, origin2, "origin2")
		play2 := dialSRTAt(c, srt.GoSRT, two.srtAddr(), srt.Options{StreamID: srtID("live/"+second, "request")})
		waitHook(c, origin2, "on_play", second)
		publishSRTInterop(c, srt.GoSRT, two.srtAddr(), srt.Options{StreamID: srtID("live/"+second, "publish")})
		waitHook(c, origin2, "on_publish", second)
		late := dialSRTAt(c, srt.GoSRT, two.srtAddr(), srt.Options{StreamID: srtID("live/"+first, "request")})
		waitHookCount(c, origin1, mock.Match{Action: "on_play", Stream: first}, 2)

		checkSRTPlayed(c, srtAnalyze(c, play1))
		checkSRTPlayed(c, srtAnalyze(c, play2))
		// SRT has no GOP cache, so the late player gets the stream from where
		// it joined, in the middle of a GOP.
		got := collectSRT(late).waitFor(check.Scaled(time.Second), func(got []byte) bool { return len(got) > 0 })
		c.Check(len(got) > 0, "the late player of the first stream got nothing")
		c.CheckEqual(len(origin2.Hooks.Requests(mock.Match{Stream: first})), 0, "hooks of the first stream on origin2")
		c.CheckEqual(len(origin1.Hooks.Requests(mock.Match{Stream: second})), 0, "hooks of the second stream on origin1")
	})

	redisA, redisB := startSRTProxyRedis(t, bin, origin1, origin2)
	for _, cl := range []srt.Client{srt.GoSRT, srt.Libsrt} {
		check.Case(t, "Redis_"+cl.Name(), check.Normal, func(c *check.C) {
			if redisA == nil {
				c.Skip("the Redis cases run with " + srtProxyRedisEnv + " and " + srtProxyPrefixEnv)
			}
			stream := streamName(c)
			play := dialSRTAt(c, srt.GoSRT, redisB.srtAddr(), srt.Options{StreamID: srtID("live/"+stream, "request")})
			origin, other := srtProxyOriginOf(c, stream, origin1, origin2)
			publishSRTInterop(c, cl, redisA.srtAddr(), srt.Options{StreamID: srtID("live/"+stream, "publish")})
			waitHook(c, origin, "on_publish", stream)
			checkSRTPlayed(c, srtAnalyze(c, play))
			c.CheckEqual(len(other.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the stream on the other origin")
		})
	}

	stall := startSRTProxy(t, bin, "stall", nil)
	stall.register(t, origin1, "origin1")
	check.Case(t, "ConclusionRepeated", check.Timer, func(c *check.C) {
		stream := streamName(c)
		raw, err := srt.DialRaw(stall.srtAddr())
		c.Must(err, "dial the raw caller")
		c.Cleanup(func() { raw.Close() })
		cookie, err := raw.Induct(time.Second)
		c.Must(err, "induction")
		conclusion := raw.Conclusion(cookie, srtID("live/"+stream, "publish"), srtImpairCleanLatency)
		c.Must(raw.Write(conclusion), "conclusion")
		_, err = raw.ReadHandshake(time.Second)
		c.Must(err, "the conclusion's answer")
		waitHook(c, origin1, "on_publish", stream)

		// A caller whose answer was lost sends its conclusion again, and
		// another caller of the proxy calls.
		c.Must(raw.Write(conclusion), "the conclusion again")
		again, _ := raw.ReadHandshake(300 * time.Millisecond)
		answered := again != nil && again.HandshakeType == packet.HSTYPE_CONCLUSION
		start := time.Now()
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		play, err := srt.GoSRT.Dial(ctx, stall.srtAddr(), srt.Options{StreamID: srtID("live/"+stream+"-other", "request"), Timeout: time.Second})
		took := time.Since(start)
		if err == nil {
			play.Close()
		}

		if !answered || err != nil || took > 300*time.Millisecond {
			check.KnownBug(c, srtProxyStallBug)
		}
		c.Check(answered, "the repeated conclusion answered with %v, want a conclusion", handshakeOf(again))
		c.Check(err == nil && took <= 300*time.Millisecond, "another caller connected after %v (%v), want within 300ms", took, err)
	})

	leak := startSRTProxy(t, bin, "leak", nil)
	leak.register(t, origin1, "origin1")
	check.Case(t, "ClosedConnectionsFreed", check.Timer, func(c *check.C) {
		file, err := os.ReadFile(media.Path(c, media.H264HighAACTS))
		c.Must(err, "read the media")
		before := leak.socketsTo(c, origin1)
		for i := 0; i < 3; i++ {
			stream := fmt.Sprintf("%v-%v", streamName(c), i)
			play := dialSRTAt(c, srt.GoSRT, leak.srtAddr(), srt.Options{StreamID: srtID("live/"+stream, "request")})
			waitHook(c, origin1, "on_play", stream)
			pub := dialSRTAt(c, srt.GoSRT, leak.srtAddr(), srt.Options{StreamID: srtID("live/"+stream, "publish")})
			waitHook(c, origin1, "on_publish", stream)
			s := srt.NewSender(bytes.NewReader(file))
			c.Must(s.SendThrough(pub, 100*time.Millisecond), "publish")

			// SRS finds a closed player only when it sends to it, so the
			// publisher sends on after the player closed; without media, SRS
			// holds it until peer_idle_timeout.
			play.Close()
			c.Must(s.SendThrough(pub, 300*time.Millisecond), "publish after the player closed")
			waitHookCount(c, origin1, mock.Match{Action: "on_stop", Stream: stream}, 1)
			pub.Close()
			waitHookCount(c, origin1, mock.Match{Action: "on_unpublish", Stream: stream}, 1)
		}
		// SRS has closed both of each stream; the proxy has had the
		// shutdowns, and has nothing more to relay.
		after := leak.socketsTo(c, origin1)
		deadline := time.Now().Add(check.Scaled(time.Second))
		for after > before && time.Now().Before(deadline) {
			time.Sleep(100 * time.Millisecond)
			after = leak.socketsTo(c, origin1)
		}
		if after > before {
			check.KnownBug(c, srtProxyLeakBug)
		}
		c.Check(after == before, "the proxy holds %v UDP sockets to the origin after 6 clients closed, %v before", after, before)
	})
}

// srtProxy is a running srs-proxy, with each listener on a port of its own.
type srtProxy struct {
	pid   int
	ports map[string]int
}

// startSRTProxy starts the proxy bin, with the memory load balancer unless
// env says another, and stops it when the group ends; its log is shown when
// the group fails.
func startSRTProxy(t *testing.T, bin, name string, env map[string]string) *srtProxy {
	t.Helper()
	p := &srtProxy{ports: map[string]int{}}
	// The proxy serves its static files from a dir that must exist, by
	// default ./trunk/research of the repository.
	vars := append(os.Environ(), "PROXY_LOAD_BALANCER_TYPE=memory", "PROXY_STATIC_FILES="+t.TempDir())
	for _, k := range srtProxyListeners {
		p.ports[k] = server.ReservePort(t)
		vars = append(vars, k+"="+strconv.Itoa(p.ports[k]))
	}
	for k, v := range env {
		vars = append(vars, k+"="+v)
	}

	logPath := filepath.Join(t.TempDir(), name+".log")
	log, err := os.Create(logPath)
	if err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(bin)
	cmd.Env, cmd.Stdout, cmd.Stderr = vars, log, log
	if err := cmd.Start(); err != nil {
		log.Close()
		t.Fatalf("start the proxy %v: %v", bin, err)
	}
	p.pid = cmd.Process.Pid
	done := make(chan struct{})
	go func() {
		cmd.Wait()
		close(done)
	}()
	t.Cleanup(func() {
		cmd.Process.Kill()
		<-done
		log.Close()
		if t.Failed() {
			srtProxyLogTo(t, name, logPath)
		}
	})

	// The proxy starts every listener before its system API.
	url := "http://" + srtLocal(p.ports["PROXY_SYSTEM_API"]) + "/api/v1/versions"
	deadline := time.Now().Add(5 * time.Second)
	for {
		select {
		case <-done:
			srtProxyLogTo(t, name, logPath)
			t.Fatalf("the proxy %v exited", name)
		default:
		}
		if res, err := httpClient.Get(url); err == nil {
			res.Body.Close()
			if res.StatusCode == http.StatusOK {
				return p
			}
		}
		if time.Now().After(deadline) {
			t.Fatalf("the proxy %v did not answer %v in 5s", name, url)
		}
		time.Sleep(20 * time.Millisecond)
	}
}

// startSRTProxyRedis starts two proxies that share Redis, at $SRT_PROXY_REDIS
// with the keys of $SRT_PROXY_REDIS_PREFIX, and registers both origins
// with them; nil when Redis is not set.
func startSRTProxyRedis(t *testing.T, bin string, origins ...*server.Server) (*srtProxy, *srtProxy) {
	t.Helper()
	addr, prefix := os.Getenv(srtProxyRedisEnv), os.Getenv(srtProxyPrefixEnv)
	if addr == "" || prefix == "" {
		return nil, nil
	}
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		t.Fatalf("%v=%v: %v", srtProxyRedisEnv, addr, err)
	}
	env := map[string]string{
		"PROXY_LOAD_BALANCER_TYPE": "redis",
		"PROXY_REDIS_HOST":         host,
		"PROXY_REDIS_PORT":         port,
		"PROXY_REDIS_DB":           "0",
		"PROXY_REDIS_KEY_PREFIX":   prefix,
	}
	a, b := startSRTProxy(t, bin, "redisA", env), startSRTProxy(t, bin, "redisB", env)
	for i, o := range origins {
		a.register(t, o, "origin"+strconv.Itoa(i+1))
	}
	return a, b
}

// srtProxyLogTo shows the last lines of a proxy's log.
func srtProxyLogTo(t *testing.T, name, path string) {
	b, err := os.ReadFile(path)
	if err != nil {
		t.Logf("the proxy %v log: %v", name, err)
		return
	}
	lines := strings.Split(strings.TrimRight(string(b), "\n"), "\n")
	if len(lines) > 40 {
		lines = lines[len(lines)-40:]
	}
	t.Logf("the proxy %v log, last %v lines:\n%v", name, len(lines), strings.Join(lines, "\n"))
}

// srtAddr is the address of the proxy's SRT listener.
func (p *srtProxy) srtAddr() string {
	return srtLocal(p.ports["PROXY_SRT_SERVER"])
}

// register registers an origin with the proxy as id, as its heartbeat does:
// its RTMP, API and SRT ports on 127.0.0.1.
func (p *srtProxy) register(t interface {
	Helper()
	Fatalf(format string, args ...any)
}, origin *server.Server, id string) {
	t.Helper()
	body, err := json.Marshal(map[string]any{
		"ip": "127.0.0.1", "server": id, "service": id, "pid": strconv.Itoa(origin.PID()), "device_id": id,
		"rtmp": []string{strconv.Itoa(origin.Ports[server.RTMP])},
		"api":  []string{strconv.Itoa(origin.Ports[server.HTTPAPI])},
		"srt":  []string{strconv.Itoa(origin.Ports[server.SRT])},
	})
	if err != nil {
		t.Fatalf("register %v: %v", id, err)
	}
	url := "http://" + srtLocal(p.ports["PROXY_SYSTEM_API"]) + "/api/v1/srs/register"
	res, err := httpClient.Post(url, "application/json", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("register %v: %v", id, err)
	}
	defer res.Body.Close()
	var reply struct {
		Code int `json:"code"`
	}
	if err := json.NewDecoder(res.Body).Decode(&reply); err != nil || res.StatusCode != http.StatusOK || reply.Code != 0 {
		t.Fatalf("register %v: status %v, code %v, err %v", id, res.StatusCode, reply.Code, err)
	}
}

// socketsTo is how many UDP sockets the proxy has connected to the origin's
// SRT port, one for each connection it relays, from lsof.
func (p *srtProxy) socketsTo(c *check.C, origin *server.Server) int {
	c.Helper()
	out, err := exec.Command("lsof", "-nP", "-a", "-p", strconv.Itoa(p.pid), "-iUDP").Output()
	var exit *exec.ExitError
	// lsof exits 1 when the process has no UDP socket at all.
	if err != nil && !(errors.As(err, &exit) && exit.ExitCode() == 1 && len(out) == 0) {
		c.Fatalf("lsof the proxy's UDP sockets: %v", err)
	}
	n := 0
	peer := "->" + origin.Addr(server.SRT)
	sc := bufio.NewScanner(bytes.NewReader(out))
	for sc.Scan() {
		if strings.Contains(sc.Text(), peer) {
			n++
		}
	}
	return n
}

// srtProxyOriginOf is the origin that got a player of a stream, by its
// on_play, and the other one.
func srtProxyOriginOf(c *check.C, stream string, a, b *server.Server) (*server.Server, *server.Server) {
	c.Helper()
	deadline := time.Now().Add(check.Scaled(time.Second))
	for time.Now().Before(deadline) {
		if len(a.Hooks.Requests(mock.Match{Action: "on_play", Stream: stream})) > 0 {
			return a, b
		}
		if len(b.Hooks.Requests(mock.Match{Action: "on_play", Stream: stream})) > 0 {
			return b, a
		}
		time.Sleep(5 * time.Millisecond)
	}
	c.Fatalf("no origin got the player of %v", stream)
	return nil, nil
}
