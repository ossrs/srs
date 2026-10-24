package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"sort"
	"sync"
	"time"
)

// The delay probes measure the fixed delays of C7: how long SRS takes to
// answer an RTMP player's pause and unpause while it plays, and the lag a
// forward and an edge add. The origin forwards every stream to a second SRS on
// forwardPort, and an edge SRS on edgePort pulls a played stream from the
// origin and forwards a published one to it; both run in the SRS container's
// network namespace, each from its own env.
const (
	forwardPort = "19351"
	edgePort    = "19352"
	// delayStream is published at the origin; delayEdgeStream at the edge,
	// which forwards it to the origin.
	delayStream     = "perf-delays"
	delayEdgeStream = "perf-delays-edge"
	// delayClients is the probe's players: one each at the origin, the
	// forward target and the edge for lag, one at the origin for the stream
	// published at the edge, and one at the origin for pauses.
	delayClients = 5
)

// delayStat is a delay's samples in milliseconds: their count, mean, sample
// sd, min and max.
type delayStat struct {
	Samples int     `json:"samples"`
	Mean    float64 `json:"mean"`
	SD      float64 `json:"sd"`
	Min     float64 `json:"min"`
	Max     float64 `json:"max"`
}

func delayStatOf(values []float64) delayStat {
	if len(values) == 0 {
		return delayStat{}
	}
	s := statOf(values)
	sorted := append([]float64(nil), values...)
	sort.Float64s(sorted)
	return delayStat{Samples: len(values), Mean: s.Mean, SD: s.SD, Min: sorted[0], Max: sorted[len(sorted)-1]}
}

// delays is what a probe measured. A lag is each video frame's, from the
// publisher's send to the player's receive:
//   - origin: published and played at the origin.
//   - forward: published at the origin, played at the forward target.
//   - edge play: published at the origin, played at the edge.
//   - edge publish: published at the edge, played at the origin.
//
// An added lag is a path's mean lag over the origin's.
type delays struct {
	PauseMillis            delayStat `json:"pause_millis"`
	UnpauseMillis          delayStat `json:"unpause_millis"`
	OriginLagMillis        delayStat `json:"origin_lag_millis"`
	ForwardLagMillis       delayStat `json:"forward_lag_millis"`
	EdgePlayLagMillis      delayStat `json:"edge_play_lag_millis"`
	EdgePublishLagMillis   delayStat `json:"edge_publish_lag_millis"`
	ForwardAddedMillis     float64   `json:"forward_added_millis"`
	EdgePlayAddedMillis    float64   `json:"edge_play_added_millis"`
	EdgePublishAddedMillis float64   `json:"edge_publish_added_millis"`
}

// delayCounters is one second of the probe: the messages and bytes the lag
// players got, and the largest frame lag among them.
type delayCounters struct {
	Frames, Bytes, LagMillis int64
}

// delayProbe is where the probe's servers listen and how long it runs: it
// settles, for the forward and the edge to start and the GOP caches to pass,
// then counts the window. notifyTimeout fails a pause SRS does not answer.
type delayProbe struct {
	origin, forward, edge string
	settle, window        time.Duration
	notifyTimeout         time.Duration
}

var srsDelayProbe = delayProbe{
	origin:        "127.0.0.1:1935",
	forward:       "127.0.0.1:" + forwardPort,
	edge:          "127.0.0.1:" + edgePort,
	notifyTimeout: 5 * time.Second,
}

// pauseCommand is an RTMP pause, as SrsPausePacket decodes it: the name,
// transaction 0, null, the pause flag and the stream time in milliseconds.
func pauseCommand(pause bool, at uint32) []byte {
	payload := append(amfString("pause"), amfNumber(0)...)
	flag := byte(0)
	if pause {
		flag = 1
	}
	payload = append(payload, 5, 1, flag)
	return append(payload, amfNumber(float64(at))...)
}

// pauseSteps gives the i-th probe's gap before its pause, which steps through
// 350ms, the playing wait (mw_latency), and how long it stays paused, which
// steps through 500ms, a paused player's sleep; so the probes land at every
// point of both.
func pauseSteps(i int) (gap, held time.Duration) {
	gap = 700*time.Millisecond + time.Duration(i*70%350)*time.Millisecond
	held = 100*time.Millisecond + time.Duration(i*110%500)*time.Millisecond
	return gap, held
}

// lagRecorder keeps one lag player's counted lags and its counters since the
// last second.
type lagRecorder struct {
	mu            sync.Mutex
	lags          []float64
	frames, bytes int64
	maxLag        int64
}

func (r *lagRecorder) got(size int) {
	r.mu.Lock()
	r.frames++
	r.bytes += int64(size)
	r.mu.Unlock()
}

func (r *lagRecorder) lag(ms int64) {
	r.mu.Lock()
	r.lags = append(r.lags, float64(ms))
	if ms > r.maxLag {
		r.maxLag = ms
	}
	r.mu.Unlock()
}

// record keeps a pause's delay, in milliseconds to the microsecond, since SRS
// may answer within a millisecond.
func (r *lagRecorder) record(ms float64) {
	r.mu.Lock()
	r.lags = append(r.lags, ms)
	r.mu.Unlock()
}

func (r *lagRecorder) take() (frames, bytes, maxLag int64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	frames, bytes, maxLag = r.frames, r.bytes, r.maxLag
	r.frames, r.bytes, r.maxLag = 0, 0, 0
	return frames, bytes, maxLag
}

func (r *lagRecorder) all() []float64 {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]float64(nil), r.lags...)
}

var errProbeStopped = errors.New("probe stopped")

// dialRTMPUntil dials until it connects, the deadline passes or stop, since
// the forward target and the edge may still be starting.
func dialRTMPUntil(addr string, deadline time.Time, stop <-chan struct{}) (*rtmpClient, error) {
	for {
		c, err := dialRTMP(addr, "live")
		if err == nil {
			return c, nil
		}
		if time.Now().After(deadline) {
			return nil, err
		}
		select {
		case <-stop:
			return nil, errProbeStopped
		case <-time.After(200 * time.Millisecond):
		}
	}
}

// playLag plays stream at addr and records the lag of every video frame
// found in the source, from when its publisher sent it to when it came, once
// the probe has settled.
func playLag(addr, stream string, clock *mediaClock, timeline *sourceTimeline, counted time.Time, stop <-chan struct{}, rec *lagRecorder) error {
	c, err := dialRTMPUntil(addr, counted, stop)
	if err != nil {
		return err
	}
	report := make(chan rtmpMessage, 256)
	done := make(chan error, 1)
	go func() {
		done <- playRTMP(c, stream, stop, report)
		close(report)
	}()
	for m := range report {
		now := time.Now()
		rec.got(len(m.payload))
		if now.Before(counted) {
			continue
		}
		at, ok := timeline.inLoop(m)
		if !ok {
			continue
		}
		sent := clock.begunAt().Add(time.Duration(timeline.publisherTime(at, clock.get())) * time.Millisecond)
		rec.lag(now.Sub(sent).Milliseconds())
	}
	return <-done
}

// probePauses plays the stream at the origin and, once the probe has settled,
// pauses and unpauses it until the window ends, timing each until SRS's
// NetStream.Pause.Notify or NetStream.Unpause.Notify (pauseSteps).
func (p delayProbe) probePauses(clock *mediaClock, counted, end time.Time, stop <-chan struct{}, pauses, unpauses *lagRecorder) error {
	c, err := dialRTMPUntil(p.origin, counted, stop)
	if err != nil {
		return err
	}
	defer c.conn.Close()
	if err := c.command("play", delayStream); err != nil {
		return err
	}
	c.conn.SetDeadline(time.Time{})
	go func() {
		<-stop
		c.conn.Close()
	}()
	notify := make(chan string, 8)
	go func() {
		defer close(notify)
		codes := []string{"NetStream.Pause.Notify", "NetStream.Unpause.Notify"}
		for {
			m, err := c.r.read()
			if err != nil {
				return
			}
			if m.typeID != 20 && m.typeID != 17 {
				continue
			}
			for _, code := range codes {
				if bytes.Contains(m.payload, []byte(code)) {
					select {
					case notify <- code:
					default:
					}
				}
			}
		}
	}()
	wait := func(d time.Duration) bool {
		select {
		case <-stop:
			return false
		case <-time.After(d):
			return true
		}
	}
	if !wait(time.Until(counted)) {
		return nil
	}
	for i := 0; ; i++ {
		gap, held := pauseSteps(i)
		if time.Now().Add(gap + held).After(end.Add(-time.Second)) {
			return nil
		}
		if !wait(gap) {
			return nil
		}
		for _, pause := range []bool{true, false} {
			code, rec := "NetStream.Pause.Notify", pauses
			if !pause {
				code, rec = "NetStream.Unpause.Notify", unpauses
			}
			sent := time.Now()
			if err := c.w.write(8, rtmpMessage{typeID: 20, streamID: c.id, payload: pauseCommand(pause, clock.get())}); err != nil {
				return err
			}
			if err := p.awaitNotify(notify, code, stop); err != nil {
				return err
			}
			rec.record(float64(time.Since(sent).Microseconds()) / 1000)
			if pause && !wait(held) {
				return nil
			}
		}
	}
}

func (p delayProbe) awaitNotify(notify <-chan string, code string, stop <-chan struct{}) error {
	timer := time.NewTimer(p.notifyTimeout)
	defer timer.Stop()
	for {
		select {
		case got, ok := <-notify:
			if !ok {
				select {
				case <-stop:
					return errProbeStopped
				default:
				}
				return fmt.Errorf("connection closed before %s", code)
			}
			if got == code {
				return nil
			}
		case <-timer.C:
			return fmt.Errorf("no %s within %s", code, p.notifyTimeout)
		}
	}
}

// publishUntil publishes the source as stream at addr, dialing until the
// deadline, since the edge may still be starting.
func publishUntil(addr, stream string, deadline time.Time, tags []rtmpMessage, clock *mediaClock, stop <-chan struct{}) error {
	c, err := dialRTMPUntil(addr, deadline, stop)
	if err != nil {
		return err
	}
	return publishRTMP(c, stream, tags, clock, stop)
}

// measure publishes the source at the origin and at the edge, plays each
// path's stream, probes pauses at the origin, and calls onSecond each second
// of the settle and the window. The probe is unhealthy when a client fails, a
// pause goes unanswered, or a path's lag has no sample.
func (p delayProbe) measure(tags []rtmpMessage, timeline *sourceTimeline, onSecond func(second int, c delayCounters) error) (delays, health, error) {
	clock, edgeClock := &mediaClock{}, &mediaClock{}
	stop := make(chan struct{})
	var stopOnce sync.Once
	stopAll := func() { stopOnce.Do(func() { close(stop) }) }
	defer stopAll()
	pubDone := make(chan error, 1)
	go func() {
		c, err := dialRTMP(p.origin, "live")
		if err != nil {
			pubDone <- err
			return
		}
		pubDone <- publishRTMP(c, delayStream, tags, clock, stop)
	}()
	for deadline := time.Now().Add(5 * time.Second); !clock.published(); {
		select {
		case err := <-pubDone:
			if err == nil {
				err = errors.New("publisher stopped")
			}
			return delays{}, health{}, fmt.Errorf("publish: %w", err)
		case <-time.After(10 * time.Millisecond):
		}
		if time.Now().After(deadline) {
			return delays{}, health{}, errors.New("publisher sent no frames")
		}
	}
	start := time.Now()
	counted, end := start.Add(p.settle), start.Add(p.settle+p.window)
	var mu sync.Mutex
	var failure string
	fail := func(reason string) {
		mu.Lock()
		if failure == "" {
			failure = reason
		}
		mu.Unlock()
	}
	running := func(name string, err error) {
		if err == nil || errors.Is(err, errProbeStopped) {
			return
		}
		select {
		case <-stop:
		default:
			fail(fmt.Sprintf("%s: %v", name, err))
		}
	}
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		running("edge publisher", publishUntil(p.edge, delayEdgeStream, counted, tags, edgeClock, stop))
	}()
	paths := []struct {
		name, addr, stream string
		clock              *mediaClock
	}{
		{"origin", p.origin, delayStream, clock},
		{"forward", p.forward, delayStream, clock},
		{"edge play", p.edge, delayStream, clock},
		{"edge publish", p.origin, delayEdgeStream, edgeClock},
	}
	lags := make([]*lagRecorder, len(paths))
	for i := range paths {
		lags[i] = &lagRecorder{}
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			path := paths[i]
			running(path.name+" player", playLag(path.addr, path.stream, path.clock, timeline, counted, stop, lags[i]))
		}(i)
	}
	pauses, unpauses := &lagRecorder{}, &lagRecorder{}
	wg.Add(1)
	go func() {
		defer wg.Done()
		running("pause player", p.probePauses(clock, counted, end, stop, pauses, unpauses))
	}()
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	seconds := int((p.settle + p.window + time.Second/2) / time.Second)
	for second := 1; second <= seconds; second++ {
		select {
		case err := <-pubDone:
			if err == nil {
				err = errors.New("publisher stopped")
			}
			return delays{}, health{}, fmt.Errorf("publish: %w", err)
		case <-ticker.C:
		}
		var c delayCounters
		for _, rec := range lags {
			frames, bytes, maxLag := rec.take()
			c.Frames += frames
			c.Bytes += bytes
			if maxLag > c.LagMillis {
				c.LagMillis = maxLag
			}
		}
		if onSecond != nil {
			if err := onSecond(second, c); err != nil {
				return delays{}, health{}, err
			}
		}
	}
	stopAll()
	wg.Wait()
	select {
	case <-pubDone:
	case <-time.After(3 * time.Second):
		return delays{}, health{}, errors.New("publisher did not stop")
	}
	stats := make([]delayStat, len(paths))
	for i, rec := range lags {
		stats[i] = delayStatOf(rec.all())
	}
	added := func(s delayStat) float64 {
		if stats[0].Samples == 0 || s.Samples == 0 {
			return 0
		}
		return s.Mean - stats[0].Mean
	}
	d := delays{
		PauseMillis:            delayStatOf(pauses.all()),
		UnpauseMillis:          delayStatOf(unpauses.all()),
		OriginLagMillis:        stats[0],
		ForwardLagMillis:       stats[1],
		EdgePlayLagMillis:      stats[2],
		EdgePublishLagMillis:   stats[3],
		ForwardAddedMillis:     added(stats[1]),
		EdgePlayAddedMillis:    added(stats[2]),
		EdgePublishAddedMillis: added(stats[3]),
	}
	h := health{Healthy: true}
	if failure != "" {
		h = health{Reason: failure}
	} else if d.PauseMillis.Samples == 0 || d.UnpauseMillis.Samples == 0 {
		h = health{Reason: "no pause probe in the window"}
	} else {
		for i, s := range stats {
			if s.Samples == 0 {
				h = health{Reason: fmt.Sprintf("no video frame on the %s path in the window", paths[i].name)}
				break
			}
		}
	}
	return d, h, nil
}

// runDelayProbe runs the probe against SRS in the containers, sampling the
// origin's CPU each second, and gives its result: one probe step with the
// delays, and no capacity or CPU per client.
func runDelayProbe(cmd command, settle, window int, smoke bool) (result, error) {
	tags, flatness, err := loadSource(cmd.media)
	if err != nil {
		return result{}, err
	}
	timeline, err := lagTimeline(cmd.media, tags)
	if err != nil {
		return result{}, err
	}
	ticks, err := clockTicks()
	if err != nil {
		return result{}, err
	}
	srsPID := cmd.srsPID
	if srsPID == 0 {
		srsPID = 1
	}
	sampler := newProcSampler("/proc", "/sys/fs/cgroup", srsPID, nil, ticks)
	if _, err := sampler.sampleAt(time.Now()); err != nil {
		return result{}, err
	}
	probe := srsDelayProbe
	probe.settle, probe.window = time.Duration(settle)*time.Second, time.Duration(window)*time.Second
	var samples []sample
	d, h, err := probe.measure(tags, timeline, func(second int, c delayCounters) error {
		now := time.Now()
		usage, err := sampler.sampleAt(now)
		if err != nil {
			return err
		}
		samples = append(samples, sample{Second: second, UnixMillis: now.UnixMilli(), SRSCPUPercent: usage.srsCPUPercent, SRSCPUUsageUsec: usage.srsCPUUsageUsec, SRSRSSBytes: usage.srsRSSBytes, Frames: c.Frames, Bytes: c.Bytes, LagMillis: c.LagMillis})
		return nil
	})
	if err != nil {
		return result{}, err
	}
	s := setup{}
	if !smoke {
		s = captureSetup(cmd)
	}
	r := result{Setup: withSource(s, cmd.media, flatness), Scenario: scenarios[cmd.scenario], Health: h, Validity: validity{Valid: true}, Delays: &d}
	r.Scenario.Parameters = map[string]string{"mode": "probe", "settle": fmt.Sprint(settle), "window": fmt.Sprint(window), "clients": fmt.Sprint(delayClients), "media": cmd.media}
	r.Steps = []step{{Mode: "probe", Clients: delayClients, SettleSeconds: settle, Samples: samples, Health: h, Validity: validity{Valid: true}}}
	return summarizeResult(r, "probe"), nil
}

// runDelaySmoke runs the probe for a 5s settle and a 10s window and prints
// its result; it fails when the probe is unhealthy.
func runDelaySmoke(output io.Writer, cmd command) error {
	r, err := runDelayProbe(cmd, 5, 10, true)
	if err != nil {
		return err
	}
	if !r.Health.Healthy {
		return fmt.Errorf("delay probe unhealthy: %s", r.Health.Reason)
	}
	return json.NewEncoder(output).Encode(r)
}

// runDelayRun runs the probe once per repeat and writes each result under the
// output directory, printing its path, like a ramp or a hold.
func runDelayRun(output *os.File, cmd command) error {
	base := cmd.output
	if base == "" {
		base = "cmd/perf/out"
	}
	for repeat := 1; repeat <= cmd.repeat; repeat++ {
		r, err := runDelayProbe(cmd, cmd.settle, cmd.window, false)
		if err != nil {
			return err
		}
		name := fmt.Sprintf("%s-%s-%02d", time.Now().UTC().Format("20060102T150405Z"), cmd.scenario, repeat)
		path, err := writeRunJSON(base, name, r)
		if err != nil {
			return err
		}
		fmt.Fprintln(output, path)
	}
	return nil
}

// compareDelays shows each delay of two probes, old and new, and flags a
// change of 10% in the pause, the unpause and each added lag.
func compareDelays(output io.Writer, old, newer result) error {
	if old.Delays == nil || newer.Delays == nil {
		_, err := fmt.Fprintln(output, "delay probe without delays; no comparison")
		return err
	}
	o, n := old.Delays, newer.Delays
	g := func(v float64) string { return fmt.Sprintf("%.4g", v) }
	lines := []string{"delay probe, delays in ms (mean ± sd, n)"}
	for _, row := range []struct {
		name string
		a, b delayStat
	}{
		{"pause", o.PauseMillis, n.PauseMillis},
		{"unpause", o.UnpauseMillis, n.UnpauseMillis},
		{"origin lag", o.OriginLagMillis, n.OriginLagMillis},
		{"forward lag", o.ForwardLagMillis, n.ForwardLagMillis},
		{"edge play lag", o.EdgePlayLagMillis, n.EdgePlayLagMillis},
		{"edge publish lag", o.EdgePublishLagMillis, n.EdgePublishLagMillis},
	} {
		lines = append(lines, fmt.Sprintf("%s: %s ± %s (%d) -> %s ± %s (%d)", row.name, g(row.a.Mean), g(row.a.SD), row.a.Samples, g(row.b.Mean), g(row.b.SD), row.b.Samples))
	}
	added := []struct {
		name string
		a, b float64
	}{
		{"forward added", o.ForwardAddedMillis, n.ForwardAddedMillis},
		{"edge play added", o.EdgePlayAddedMillis, n.EdgePlayAddedMillis},
		{"edge publish added", o.EdgePublishAddedMillis, n.EdgePublishAddedMillis},
	}
	for _, row := range added {
		lines = append(lines, fmt.Sprintf("%s: %s -> %s", row.name, g(row.a), g(row.b)))
	}
	flags := []string{
		changeFlag(o.PauseMillis.Mean, n.PauseMillis.Mean, "pause", true),
		changeFlag(o.UnpauseMillis.Mean, n.UnpauseMillis.Mean, "unpause", true),
	}
	for _, row := range added {
		flags = append(flags, changeFlag(row.a, row.b, row.name+" lag", true))
	}
	count := 0
	for _, flag := range flags {
		if flag != "" {
			lines = append(lines, flag)
			count++
		}
	}
	if count == 0 {
		lines = append(lines, "no changes >= 10%")
	}
	for _, line := range lines {
		if _, err := fmt.Fprintln(output, line); err != nil {
			return err
		}
	}
	return nil
}
