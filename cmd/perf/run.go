package main

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

// The ramp stops at this many clients when no -cap is given. It is the cap of
// the TCP protocols; each scenario's config admits it plus its publisher. A
// scenario may set a lower cap, as SRT does.
const defaultClientCap = 40000

// stepTiming gives a mode's default settle and window, in seconds. A step
// lasts both; only the window's samples count.
//   - ramp: a 5s step, 2s for new clients to connect, then 3 counted.
//   - hold: a measuring hold, 30s to settle, then a 60s window (180s for a verdict).
//   - profile: a hold for perf after the measuring holds, 30s to settle, then
//     75s for the 60s record and the 10s syscall count.
//   - probe: the delay probes, 10s for the forward and the edge to start, then
//     a 60s window.
func stepTiming(mode string) (settle, window int) {
	switch mode {
	case "hold":
		return 30, 60
	case "profile":
		return 30, 75
	case "probe":
		return 10, 60
	}
	return 2, 3
}

// evaluateRampStep judges a step by its scenario's rules: a failed player,
// lag over the scenario's limit, or no frame from any player for its gap.
func evaluateRampStep(sc scenario, clients, settle int, samples []sample, cap int) (step, bool) {
	s := step{Mode: "ramp", Clients: clients, SettleSeconds: settle, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}
	if len(samples) == 0 {
		s.Health = health{Reason: "no samples"}
		return s, true
	}
	var cpu float64
	lagLimit, gap := sc.lagLimit(), int(sc.gap()/time.Second)
	noFrames := 0
	for _, sample := range samples {
		if sample.Failures > 0 {
			s.Health = health{Reason: "player failure"}
		}
		if lagLimit > 0 && sample.LagMillis > lagLimit {
			s.Health = health{Reason: fmt.Sprintf("player lag exceeds %ds", lagLimit/1000)}
		}
		if sample.Frames == 0 {
			noFrames++
		} else {
			noFrames = 0
		}
		if noFrames >= gap {
			s.Health = health{Reason: fmt.Sprintf("no frames for %ds", gap)}
		}
	}
	cpu = steadyCPU(samples, settle)
	for _, sample := range samples {
		if v := classifyWorkerLoad(!s.Health.Healthy, sample.WorkerCPUPercent); !v.Valid {
			s.Validity = v
			break
		}
	}
	if originBound(samples, settle) {
		s.Bound = "origin-bound"
	}
	stop := !s.Health.Healthy || cpu >= 90 || s.Bound != "" || clients >= cap
	return s, stop
}

// steadyOriginCPU is each origin's mean CPU over the counted samples; nil
// for SRS, which has no origins.
func steadyOriginCPU(samples []sample, settle int) []float64 {
	steady := steadySamples(samples, settle)
	var sums []float64
	for _, s := range steady {
		for i, cpu := range s.OriginCPUPercent {
			if i >= len(sums) {
				sums = append(sums, 0)
			}
			sums[i] += cpu
		}
	}
	for i := range sums {
		sums[i] /= float64(len(steady))
	}
	return sums
}

// originBound tells whether an origin behind the proxy reached 90% CPU while
// the proxy did not: the step then measures the origin, not the proxy. When
// both reach it, the proxy bounds the step, as it does on its own.
func originBound(samples []sample, settle int) bool {
	if steadyCPU(samples, settle) >= 90 {
		return false
	}
	for _, cpu := range steadyOriginCPU(samples, settle) {
		if cpu >= 90 {
			return true
		}
	}
	return false
}

// steadySamples skips the settle seconds and keeps every sample after them:
// the last 3 of a 5s ramp step, the whole window of a hold. A step no longer
// than its settle keeps all of its samples.
func steadySamples(samples []sample, settle int) []sample {
	if len(samples) > settle {
		return samples[settle:]
	}
	return samples
}

func steadyCPU(samples []sample, settle int) float64 {
	steady := steadySamples(samples, settle)
	if len(steady) == 0 {
		return 0
	}
	var cpu float64
	for _, v := range steady {
		cpu += v.SRSCPUPercent
	}
	return cpu / float64(len(steady))
}

func statOf(values []float64) stat {
	if len(values) == 0 {
		return stat{}
	}
	var sum float64
	for _, v := range values {
		sum += v
	}
	mean := sum / float64(len(values))
	if len(values) == 1 {
		return stat{Mean: mean}
	}
	var squares float64
	for _, v := range values {
		squares += (v - mean) * (v - mean)
	}
	return stat{Mean: mean, SD: math.Sqrt(squares / float64(len(values)-1))}
}

// windowOf gives the mean and sd of each metric over a step's counted
// samples, the same ones its CPU decisions use.
func windowOf(samples []sample, settle int) window {
	steady := steadySamples(samples, settle)
	metrics := make([][]float64, 7)
	var origins [][]float64
	for _, s := range steady {
		for i, cpu := range s.OriginCPUPercent {
			if i >= len(origins) {
				origins = append(origins, nil)
			}
			origins[i] = append(origins[i], cpu)
		}
		var workers float64
		for _, cpu := range s.WorkerCPUPercent {
			workers += cpu
		}
		if len(s.WorkerCPUPercent) > 0 {
			workers /= float64(len(s.WorkerCPUPercent))
		}
		for i, v := range []float64{s.SRSCPUPercent, float64(s.SRSRSSBytes), workers, float64(s.Frames), float64(s.Bytes), float64(s.LagMillis), float64(s.Failures)} {
			metrics[i] = append(metrics[i], v)
		}
	}
	var originStats []stat
	for _, values := range origins {
		originStats = append(originStats, statOf(values))
	}
	return window{
		Samples:          len(steady),
		SRSCPUPercent:    statOf(metrics[0]),
		SRSRSSBytes:      statOf(metrics[1]),
		OriginCPUPercent: originStats,
		WorkerCPUPercent: statOf(metrics[2]),
		Frames:           statOf(metrics[3]),
		Bytes:            statOf(metrics[4]),
		LagMillis:        statOf(metrics[5]),
		Failures:         statOf(metrics[6]),
	}
}

// summarizeResult gives every step its window, and the result its capacity
// and CPU per client; a hold's result carries its one step's window. A profile
// hold's CPU includes perf, and a delay probe measures delays, so their
// results have neither.
func summarizeResult(r result, mode string) result {
	for i := range r.Steps {
		w := windowOf(r.Steps[i].Samples, r.Steps[i].SettleSeconds)
		r.Steps[i].Window = &w
		if r.Steps[i].Bound != "" {
			r.Bound = r.Steps[i].Bound
		}
	}
	if mode == "ramp" {
		r.Capacity, r.CPUPerClient = rampSummary(r.Steps)
	} else if len(r.Steps) > 0 && mode != "profile" && mode != "probe" {
		r.CPUPerClient = holdCPUPerClient(r.Steps[0])
		r.Window = r.Steps[0].Window
	}
	return r
}

// holdCPUPerClient is the SRS CPU per client of a hold, so two holds at the
// same client count show the noise between identical runs.
func holdCPUPerClient(s step) float64 {
	if s.Clients <= 0 || len(s.Samples) == 0 {
		return 0
	}
	return steadyCPU(s.Samples, s.SettleSeconds) / float64(s.Clients)
}

func rampSummary(steps []step) (int, float64) {
	var capacity int
	var cpuSum float64
	var count int
	for _, s := range steps {
		if !s.Health.Healthy || !s.Validity.Valid || len(s.Samples) == 0 {
			break
		}
		cpu := steadyCPU(s.Samples, s.SettleSeconds)
		if cpu >= 90 || originBound(s.Samples, s.SettleSeconds) {
			break
		}
		capacity = s.Clients
		cpuSum += cpu / float64(s.Clients)
		count++
	}
	if count == 0 {
		return 0, 0
	}
	return capacity, cpuSum / float64(count)
}

func writeRunJSON(base, name string, value result) (string, error) {
	dir := filepath.Join(base, name)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return "", err
	}
	path := filepath.Join(dir, "result.json")
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return "", err
	}
	data = append(data, '\n')
	return path, os.WriteFile(path, data, 0644)
}

func parseCalibrationGap(value string) float64 {
	for _, line := range strings.Split(value, "\n") {
		if !strings.HasPrefix(line, "gap_percent=") {
			continue
		}
		percent, err := strconv.ParseFloat(strings.TrimPrefix(line, "gap_percent="), 64)
		if err == nil {
			return percent / 100
		}
	}
	return 0
}

func captureSetup(cmd command) setup {
	machine := cmd.machine
	if machine == "" {
		machine = runtime.GOARCH
	}
	version := cmd.dockerVersion
	if version == "" {
		version = "unknown"
	}
	s := setup{Machine: machine, Cores: runtime.NumCPU(), DockerVersion: version}
	if output, err := exec.Command("uname", "-r").Output(); err == nil {
		s.Kernel = strings.TrimSpace(string(output))
	}
	if data, err := os.ReadFile("/proc/meminfo"); err == nil {
		for _, line := range strings.Split(string(data), "\n") {
			fields := strings.Fields(line)
			if len(fields) == 3 && fields[0] == "MemTotal:" && fields[2] == "kB" {
				if kb, err := strconv.ParseInt(fields[1], 10, 64); err == nil {
					s.MemoryBytes = kb * 1024
				}
				break
			}
		}
	}
	if data, err := os.ReadFile("/out/env-check/core-calibration.txt"); err == nil {
		s.CoreCalibrationGap = parseCalibrationGap(string(data))
	}
	return s
}

func clockTicks() (float64, error) {
	output, err := exec.Command("getconf", "CLK_TCK").Output()
	if err != nil {
		return 0, fmt.Errorf("read CLK_TCK: %w", err)
	}
	ticks, err := strconv.ParseFloat(strings.TrimSpace(string(output)), 64)
	if err != nil || ticks <= 0 {
		return 0, fmt.Errorf("invalid CLK_TCK %q", strings.TrimSpace(string(output)))
	}
	return ticks, nil
}

func runPerformance(output *os.File, cmd command) error {
	if scenarios[cmd.scenario].Unit == "probes" {
		return runDelayRun(output, cmd)
	}
	// Players with no publisher need no media, so the cap check needs no source.
	var tags []rtmpMessage
	var flatness json.RawMessage
	if !cmd.noPublisher {
		var err error
		if tags, flatness, err = loadSource(cmd.media); err != nil {
			return err
		}
	}
	mode := cmd.mode
	if mode == "" {
		mode = "ramp"
	}
	cores := cmd.cores
	if cores == 0 {
		cores = runtime.NumCPU()
	}
	settle, window := cmd.settle, cmd.window
	if window == 0 {
		settle, window = scenarios[cmd.scenario].stepTiming(mode)
	}
	cap := cmd.cap
	if cap == 0 {
		cap = scenarios[cmd.scenario].rampCap()
	}
	base := cmd.output
	if base == "" {
		base = "cmd/perf/out"
	}
	for repeat := 1; repeat <= cmd.repeat; repeat++ {
		runName := fmt.Sprintf("%s-%s-%02d", time.Now().UTC().Format("20060102T150405Z"), cmd.scenario, repeat)
		r := result{Setup: captureSetup(cmd), ABRun: cmd.abRun, Scenario: scenarios[cmd.scenario], Health: health{Healthy: true}, Validity: validity{Valid: true}}
		clients := cmd.clients
		if clients == 0 {
			clients = scenarios[cmd.scenario].rampStart()
		}
		r.Scenario.Parameters = map[string]string{"settle": fmt.Sprint(settle), "window": fmt.Sprint(window), "mode": mode, "clients": fmt.Sprint(clients), "cap": fmt.Sprint(cap), "workers": fmt.Sprint(cores), "media": cmd.media}
		if cmd.noPublisher {
			r.Scenario.Parameters["media"] = "none"
		} else {
			r.Setup = withSource(r.Setup, cmd.media, flatness)
		}
		var origins []int
		var err error
		if r.Setup, origins, err = targetSetup(r.Setup, cmd.target, proxyAPI, "/proc", subjectPID(cmd.srsPID)); err != nil {
			return err
		}
		for {
			s, err := measureStep(cmd.scenario, mode, clients, settle, window, cores, cmd.srsPID, origins, tags, cmd.media, cmd.noPublisher)
			if err != nil {
				return err
			}
			r.Steps = append(r.Steps, s)
			if !s.Health.Healthy {
				r.Health = s.Health
			}
			if !s.Validity.Valid {
				r.Validity = s.Validity
			}
			if mode != "ramp" || !s.Health.Healthy || !s.Validity.Valid || clients >= cap {
				break
			}
			if steadyCPU(s.Samples, settle) >= 90 || s.Bound != "" {
				break
			}
			clients *= 2
			if clients > cap {
				clients = cap
			}
		}
		r.Setup.Shares = r.Steps[len(r.Steps)-1].Shares
		r = summarizeResult(r, mode)
		path, err := writeRunJSON(base, runName, r)
		if err != nil {
			return err
		}
		fmt.Fprintln(output, path)
	}
	return nil
}

// stepStream is the stream of a step that starts at, new to the step, so a
// step's publisher is never refused as busy while SRS has yet to close the
// last step's publisher of that stream: an SRT or WHIP publisher's close
// reaches SRS over UDP, after the next step's connect. A player scenario
// plays perf-run-<ms>; a publisher or many-streams scenario's workers add
// each client's index to perf-<ms>.
func stepStream(scenario string, at time.Time) string {
	if scenarios[scenario].Unit != "clients" {
		return fmt.Sprintf("perf-%d", at.UnixMilli())
	}
	return fmt.Sprintf("perf-run-%d", at.UnixMilli())
}

// subjectPID is the measured server's pid: SRS, or the proxy, is pid 1 in the
// load container's pid namespace unless -srs-pid says otherwise.
func subjectPID(pid int) int {
	if pid == 0 {
		return 1
	}
	return pid
}

func measureStep(scenario, mode string, clients, settle, window, cores, srsPID int, origins []int, tags []rtmpMessage, media string, noPublisher bool) (step, error) {
	seconds := settle + window
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	// Players play the parent's publishers, one on SRS or one on each origin
	// behind the proxy; a publisher or many-streams step has its publishers in
	// the workers, each on a stream of its own.
	stream := stepStream(scenario, time.Now())
	names := []string{stream}
	apis := originAPIs(origins)
	inWorkers := scenarios[scenario].Unit != "clients"
	var published []placedStream
	if !noPublisher && !inWorkers {
		var err error
		if published, err = startStepStreams(scenarios[scenario].Publisher, stream, media, tags, apis); err != nil {
			return step{}, err
		}
		defer stopStreams(published)
		names = streamNames(published)
	}
	if noPublisher {
		media = ""
	}
	group, err := startWorkers(ctx, nil, scenario, media, names, clients, cores)
	if err != nil {
		return step{}, err
	}
	defer group.close()
	ticks, err := clockTicks()
	if err != nil {
		return step{}, err
	}
	sampler := newProcSampler("/proc", "/sys/fs/cgroup", subjectPID(srsPID), group.pids(), ticks).withOrigins(origins)
	if _, err := sampler.sampleAt(time.Now()); err != nil {
		return step{}, err
	}
	all := make([]sample, 0, seconds)
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for second := 1; second <= seconds; second++ {
		<-ticker.C
		counters, err := group.collectWithTimestamp(second, streamTimestamps(published), second == seconds)
		if err != nil {
			return step{}, err
		}
		now := time.Now()
		usage, err := sampler.sampleAt(now)
		if err != nil {
			return step{}, err
		}
		all = append(all, sample{Second: second, UnixMillis: now.UnixMilli(), SRSCPUPercent: usage.srsCPUPercent, SRSCPUUsageUsec: usage.srsCPUUsageUsec, SRSRSSBytes: usage.srsRSSBytes, OriginCPUPercent: usage.originCPUPercent, OriginCPUUsageUsec: usage.originCPUUsageUsec, WorkerCPUPercent: usage.workerCPUPercent, Frames: counters.Frames, Segments: counters.Segments, Bytes: counters.Bytes, LagMillis: counters.LagMillis, Failures: counters.Failures})
	}
	evaluated, _ := evaluateRampStep(scenarios[scenario], clients, settle, all, defaultClientCap)
	evaluated.Mode = mode
	evaluated.Samples = all
	if evaluated.Shares, err = targetShares(scenarios[scenario], apis, published, stream, clients); err != nil {
		return step{}, err
	}
	return evaluated, nil
}
