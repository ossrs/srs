package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"math"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"testing"
	"time"
)

// fakeRTMPHub is the published streams served by several fake RTMP servers.
// A server delivers a stream to its players its play delay after the media
// reached the hub, and media published at it reaches the hub its publish delay
// after it came, as a forward target or an edge adds lag. A pause is answered
// after pauseDelay, or never when it is negative.
type fakeRTMPHub struct {
	pauseDelay time.Duration
	mu         sync.Mutex
	players    map[string][]*fakeRTMPPlayer
	pauses     int
}

type fakeRTMPPlayer struct {
	w     *rtmpWriter
	delay time.Duration
	queue chan fakeDelivery
}

type fakeDelivery struct {
	m  rtmpMessage
	at time.Time
}

type fakeRTMPServer struct {
	play, publish time.Duration
	// silent accepts players but never sends them media.
	silent bool
}

// listen serves RTMP on a loopback port.
func (h *fakeRTMPHub) listen(t *testing.T, delay time.Duration, silent bool) string {
	return h.listenServer(t, fakeRTMPServer{play: delay, silent: silent})
}

func (h *fakeRTMPHub) listenServer(t *testing.T, server fakeRTMPServer) string {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	go func() {
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			t.Cleanup(func() { conn.Close() })
			go h.handle(conn, server)
		}
	}()
	return ln.Addr().String()
}

// fakeCommandStream is the stream name of a play or publish: the string after
// the name, the transaction and null.
func fakeCommandStream(payload []byte) string {
	n := int(binary.BigEndian.Uint16(payload[1:3]))
	rest := payload[3+n+9+1:]
	if len(rest) < 3 || rest[0] != 2 {
		return ""
	}
	return fakeCommandName(rest)
}

func fakeCommandName(payload []byte) string {
	if len(payload) < 3 || payload[0] != 2 {
		return ""
	}
	n := int(binary.BigEndian.Uint16(payload[1:3]))
	if len(payload) < 3+n {
		return ""
	}
	return string(payload[3 : 3+n])
}

func fakeStatus(code string) rtmpMessage {
	payload := append(amfString("onStatus"), amfNumber(0)...)
	payload = append(payload, 5)
	payload = append(payload, amfObject("level", "status", "code", code)...)
	return rtmpMessage{typeID: 20, streamID: 1, payload: payload}
}

func (h *fakeRTMPHub) handle(conn net.Conn, server fakeRTMPServer) {
	defer conn.Close()
	var c0c1 [1537]byte
	if _, err := io.ReadFull(conn, c0c1[:]); err != nil {
		return
	}
	s0s1s2 := make([]byte, 3073)
	s0s1s2[0] = 3
	if _, err := conn.Write(s0s1s2); err != nil {
		return
	}
	var c2 [1536]byte
	if _, err := io.ReadFull(conn, c2[:]); err != nil {
		return
	}
	r, w := newRTMPReader(conn), newRTMPWriter(conn)
	published := ""
	for {
		m, err := r.read()
		if err != nil {
			return
		}
		if published != "" && (m.typeID == 8 || m.typeID == 9) {
			if stream := published; server.publish > 0 {
				time.AfterFunc(server.publish, func() { h.broadcast(stream, m) })
			} else {
				h.broadcast(stream, m)
			}
			continue
		}
		if m.typeID != 20 {
			continue
		}
		switch fakeCommandName(m.payload) {
		case "connect":
			payload := append(amfString("_result"), amfNumber(1)...)
			payload = append(payload, amfObject("fmsVer", "fake")...)
			w.write(3, rtmpMessage{typeID: 20, payload: payload})
		case "createStream":
			payload := append(amfString("_result"), amfNumber(2)...)
			payload = append(payload, 5)
			payload = append(payload, amfNumber(1)...)
			w.write(3, rtmpMessage{typeID: 20, payload: payload})
		case "publish":
			published = fakeCommandStream(m.payload)
			w.write(5, fakeStatus("NetStream.Publish.Start"))
		case "play":
			if !server.silent {
				h.addPlayer(fakeCommandStream(m.payload), w, server.play)
			}
		case "pause":
			// "pause", the transaction, null, then the pause flag.
			pause := len(m.payload) > 19 && m.payload[19] == 1
			h.mu.Lock()
			h.pauses++
			h.mu.Unlock()
			if h.pauseDelay < 0 {
				continue
			}
			code := "NetStream.Unpause.Notify"
			if pause {
				code = "NetStream.Pause.Notify"
			}
			time.AfterFunc(h.pauseDelay, func() { w.write(5, fakeStatus(code)) })
		}
	}
}

func (h *fakeRTMPHub) addPlayer(stream string, w *rtmpWriter, delay time.Duration) {
	p := &fakeRTMPPlayer{w: w, delay: delay, queue: make(chan fakeDelivery, 4096)}
	h.mu.Lock()
	if h.players == nil {
		h.players = make(map[string][]*fakeRTMPPlayer)
	}
	h.players[stream] = append(h.players[stream], p)
	h.mu.Unlock()
	go func() {
		for d := range p.queue {
			time.Sleep(time.Until(d.at))
			if err := p.w.write(6, d.m); err != nil {
				return
			}
		}
	}()
}

func (h *fakeRTMPHub) broadcast(stream string, m rtmpMessage) {
	now := time.Now()
	h.mu.Lock()
	defer h.mu.Unlock()
	for _, p := range h.players[stream] {
		select {
		case p.queue <- fakeDelivery{m: m, at: now.Add(p.delay)}:
		default:
		}
	}
}

func within(got, want, tolerance float64) bool { return math.Abs(got-want) <= tolerance }

// The probe measures each pause until NetStream.Pause.Notify, each unpause
// until NetStream.Unpause.Notify, and every video frame's lag on each path,
// from the publisher's send to the player's receive: published and played at
// the origin, played at the forward target, played at the edge, and published
// at the edge and played at the origin; each path adds its lag over the
// origin's.
func TestDelayProbeMeasuresPauseAndAddedLag(t *testing.T) {
	hub := &fakeRTMPHub{pauseDelay: 80 * time.Millisecond}
	probe := delayProbe{
		origin:        hub.listen(t, 20*time.Millisecond, false),
		forward:       hub.listen(t, 220*time.Millisecond, false),
		edge:          hub.listenServer(t, fakeRTMPServer{play: 320 * time.Millisecond, publish: 400 * time.Millisecond}),
		settle:        time.Second,
		window:        3 * time.Second,
		notifyTimeout: time.Second,
	}
	tags := cbrLikeTags()
	timeline, err := lagTimeline("test", tags)
	if err != nil {
		t.Fatal(err)
	}
	var seconds []delayCounters
	d, h, err := probe.measure(tags, timeline, func(second int, c delayCounters) error {
		if second != len(seconds)+1 {
			t.Errorf("second %d after %d", second, len(seconds))
		}
		seconds = append(seconds, c)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	if !h.Healthy {
		t.Fatalf("health = %+v", h)
	}
	if len(seconds) != 4 {
		t.Fatalf("%d seconds, want settle plus window, 4", len(seconds))
	}
	if seconds[3].Frames == 0 || seconds[3].Bytes == 0 || seconds[3].LagMillis < 300 {
		t.Fatalf("last second = %+v, want frames and the edge's lag", seconds[3])
	}
	for name, s := range map[string]delayStat{"pause": d.PauseMillis, "unpause": d.UnpauseMillis} {
		if s.Samples < 1 || !within(s.Mean, 80, 40) || s.Min > s.Max {
			t.Errorf("%s = %+v, want about 80ms", name, s)
		}
	}
	for name, c := range map[string]struct {
		s    delayStat
		want float64
	}{"origin": {d.OriginLagMillis, 20}, "forward": {d.ForwardLagMillis, 220}, "edge play": {d.EdgePlayLagMillis, 320}, "edge publish": {d.EdgePublishLagMillis, 420}} {
		// 25 fps for the 3s window, less a frame or two at its edges.
		if c.s.Samples < 60 || !within(c.s.Mean, c.want, 30) {
			t.Errorf("%s lag = %+v, want about %vms", name, c.s, c.want)
		}
	}
	if !within(d.ForwardAddedMillis, 200, 30) || !within(d.EdgePlayAddedMillis, 300, 30) || !within(d.EdgePublishAddedMillis, 400, 30) {
		t.Fatalf("added: forward %v, edge play %v, edge publish %v; want about 200, 300 and 400", d.ForwardAddedMillis, d.EdgePlayAddedMillis, d.EdgePublishAddedMillis)
	}
}

// The gap before a pause steps through the 350ms playing wait, and the time
// paused through a paused player's 500ms sleep, so the probes see every phase.
func TestPauseStepsCoverTheWaits(t *testing.T) {
	gaps, helds := map[time.Duration]bool{}, map[time.Duration]bool{}
	for i := 0; i < 50; i++ {
		gap, held := pauseSteps(i)
		if gap < 700*time.Millisecond || gap >= 1050*time.Millisecond || held < 100*time.Millisecond || held >= 600*time.Millisecond {
			t.Fatalf("step %d: gap %v, held %v", i, gap, held)
		}
		gaps[gap%(350*time.Millisecond)/(70*time.Millisecond)] = true
		helds[held%(500*time.Millisecond)/(100*time.Millisecond)] = true
	}
	if len(gaps) != 5 || len(helds) != 5 {
		t.Fatalf("phases: gaps %v, helds %v", gaps, helds)
	}
}

// An edge that never sends a frame leaves its lag unmeasured, so the probe is
// unhealthy and says which server.
func TestDelayProbeUnhealthyWithoutEdgeFrames(t *testing.T) {
	hub := &fakeRTMPHub{pauseDelay: 10 * time.Millisecond}
	probe := delayProbe{
		origin:        hub.listen(t, 0, false),
		forward:       hub.listen(t, 0, false),
		edge:          hub.listen(t, 0, true),
		settle:        500 * time.Millisecond,
		window:        2 * time.Second,
		notifyTimeout: time.Second,
	}
	tags := cbrLikeTags()
	timeline, _ := lagTimeline("test", tags)
	d, h, err := probe.measure(tags, timeline, nil)
	if err != nil {
		t.Fatal(err)
	}
	if h.Healthy || !strings.Contains(h.Reason, "edge play") {
		t.Fatalf("health = %+v, want unhealthy for the edge", h)
	}
	if d.EdgePlayLagMillis.Samples != 0 || d.OriginLagMillis.Samples == 0 || d.EdgePublishLagMillis.Samples == 0 {
		t.Fatalf("delays = %+v", d)
	}
}

// A pause SRS never answers fails the probe after the notify timeout.
func TestDelayProbeUnhealthyWithoutPauseNotify(t *testing.T) {
	hub := &fakeRTMPHub{pauseDelay: -1}
	probe := delayProbe{
		origin:        hub.listen(t, 0, false),
		forward:       hub.listen(t, 0, false),
		edge:          hub.listen(t, 0, false),
		settle:        500 * time.Millisecond,
		window:        2 * time.Second,
		notifyTimeout: 300 * time.Millisecond,
	}
	tags := cbrLikeTags()
	timeline, _ := lagTimeline("test", tags)
	d, h, err := probe.measure(tags, timeline, nil)
	if err != nil {
		t.Fatal(err)
	}
	if h.Healthy || !strings.Contains(h.Reason, "NetStream.Pause.Notify") {
		t.Fatalf("health = %+v, want no pause notify", h)
	}
	if d.PauseMillis.Samples != 0 {
		t.Fatalf("pause = %+v", d.PauseMillis)
	}
	hub.mu.Lock()
	defer hub.mu.Unlock()
	if hub.pauses == 0 {
		t.Fatal("no pause was sent")
	}
}

// pause is "pause", transaction 0, null, the pause flag and the stream time,
// as SrsPausePacket decodes it.
func TestPauseCommandPayload(t *testing.T) {
	got := pauseCommand(true, 1500)
	want := append(amfString("pause"), amfNumber(0)...)
	want = append(want, 5, 1, 1)
	want = append(want, amfNumber(1500)...)
	if !bytes.Equal(got, want) {
		t.Fatalf("pause = %x, want %x", got, want)
	}
	if unpause := pauseCommand(false, 0); unpause[19] != 0 || len(unpause) != len(want) {
		t.Fatalf("unpause = %x", unpause)
	}
}

func TestDelayStatOf(t *testing.T) {
	got := delayStatOf([]float64{300, 100, 200})
	if got.Samples != 3 || got.Mean != 200 || got.SD != 100 || got.Min != 100 || got.Max != 300 {
		t.Fatalf("stat = %+v", got)
	}
	if empty := delayStatOf(nil); empty != (delayStat{}) {
		t.Fatalf("empty = %+v", empty)
	}
}

// The origin forwards every stream to the forward target, and the edge pulls
// from the origin, each a second SRS in the SRS container's network namespace
// on the ports the probe dials.
func TestDelayEnvsForwardAndEdge(t *testing.T) {
	envs := map[string][]string{
		"delays.env":         {"SRS_RTMP_LISTEN=1935", "SRS_VHOST_FORWARD_ENABLED=on", "SRS_VHOST_FORWARD_DESTINATION=127.0.0.1:" + forwardPort},
		"delays-forward.env": {"SRS_RTMP_LISTEN=" + forwardPort},
		"delays-edge.env":    {"SRS_RTMP_LISTEN=" + edgePort, "SRS_VHOST_CLUSTER_MODE=remote", "SRS_VHOST_CLUSTER_ORIGIN=127.0.0.1:1935"},
	}
	for name, lines := range envs {
		data, err := os.ReadFile(filepath.Join("env", name))
		if err != nil {
			t.Fatal(err)
		}
		for _, line := range lines {
			if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
				t.Errorf("%s: missing %s", name, line)
			}
		}
	}
	if forward, err := os.ReadFile(filepath.Join("env", "delays-forward.env")); err != nil || bytes.Contains(forward, []byte("FORWARD")) {
		t.Fatalf("the forward target must not forward again: %v", err)
	}
	s, ok := scenarioByName("delays")
	if !ok || s.Unit != "probes" || s.Publisher != "rtmp" || s.Player != "rtmp" {
		t.Fatalf("delays scenario = %+v", s)
	}
	if srsDelayProbe.origin != "127.0.0.1:1935" || srsDelayProbe.forward != "127.0.0.1:"+forwardPort || srsDelayProbe.edge != "127.0.0.1:"+edgePort {
		t.Fatalf("probe = %+v", srsDelayProbe)
	}
}

// A probe is not a measurement of CPU: it has no CPU per client, and compare
// shows each delay old and new, flagging a 10% change.
func TestCompareDelayProbes(t *testing.T) {
	probe := func(pause, forward float64) result {
		value := comparisonResult("probe", "5", 0, 0)
		value.Delays = &delays{
			PauseMillis:            delayStat{Samples: 10, Mean: pause, SD: 50, Min: 10, Max: 350},
			UnpauseMillis:          delayStat{Samples: 10, Mean: 100, SD: 20, Min: 50, Max: 150},
			OriginLagMillis:        delayStat{Samples: 1500, Mean: 150, SD: 90},
			ForwardLagMillis:       delayStat{Samples: 1500, Mean: 150 + forward, SD: 90},
			EdgePlayLagMillis:      delayStat{Samples: 1500, Mean: 150, SD: 90},
			EdgePublishLagMillis:   delayStat{Samples: 1500, Mean: 450, SD: 90},
			ForwardAddedMillis:     forward,
			EdgePlayAddedMillis:    0,
			EdgePublishAddedMillis: 300,
		}
		return value
	}
	var out bytes.Buffer
	if err := compareRunFiles(&out, writeComparisonFixture(t, "old.json", probe(200, 250)), writeComparisonFixture(t, "new.json", probe(100, 240))); err != nil {
		t.Fatal(err)
	}
	want := "delay probe, delays in ms (mean ± sd, n)\n" +
		"pause: 200 ± 50 (10) -> 100 ± 50 (10)\n" +
		"unpause: 100 ± 20 (10) -> 100 ± 20 (10)\n" +
		"origin lag: 150 ± 90 (1500) -> 150 ± 90 (1500)\n" +
		"forward lag: 400 ± 90 (1500) -> 390 ± 90 (1500)\n" +
		"edge play lag: 150 ± 90 (1500) -> 150 ± 90 (1500)\n" +
		"edge publish lag: 450 ± 90 (1500) -> 450 ± 90 (1500)\n" +
		"forward added: 250 -> 240\n" +
		"edge play added: 0 -> 0\n" +
		"edge publish added: 300 -> 300\n" +
		"pause gain >= 10%\n"
	if out.String() != want {
		t.Fatalf("output\n%s\nwant\n%s", out.String(), want)
	}
	samples := []sample{{SRSCPUPercent: 3, Frames: 1}, {SRSCPUPercent: 4, Frames: 1}}
	summary := summarizeResult(result{Steps: []step{{Mode: "probe", Clients: 5, SettleSeconds: 1, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}}}, "probe")
	if summary.CPUPerClient != 0 || summary.Capacity != 0 || summary.Steps[0].Window == nil {
		t.Fatalf("probe summary = %+v", summary)
	}
}
