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
	stop := !s.Health.Healthy || cpu >= 90 || clients >= cap
	return s, stop
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
	for _, s := range steady {
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
	return window{
		Samples:          len(steady),
		SRSCPUPercent:    statOf(metrics[0]),
		SRSRSSBytes:      statOf(metrics[1]),
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
		if cpu >= 90 {
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
		for {
			s, err := measureStep(cmd.scenario, mode, clients, settle, window, cores, cmd.srsPID, tags, cmd.media, cmd.noPublisher)
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
			if steadyCPU(s.Samples, settle) >= 90 {
				break
			}
			clients *= 2
			if clients > cap {
				clients = cap
			}
		}
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

func measureStep(scenario, mode string, clients, settle, window, cores, srsPID int, tags []rtmpMessage, media string, noPublisher bool) (step, error) {
	seconds := settle + window
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	clock := &mediaClock{}
	// Players play the parent's one publisher; a publisher or many-streams
	// step has its publishers in the workers, each on a stream of its own.
	stream := stepStream(scenario, time.Now())
	inWorkers := scenarios[scenario].Unit != "clients"
	if !noPublisher && !inWorkers {
		stop := make(chan struct{})
		pubDone, err := startPublisher(scenarios[scenario].Publisher, stream, media, tags, clock, stop)
		if err != nil {
			close(stop)
			return step{}, err
		}
		defer func() { close(stop); <-pubDone }()
	}
	if noPublisher {
		media = ""
	}
	group, err := startWorkers(ctx, nil, scenario, media, stream, clients, cores)
	if err != nil {
		return step{}, err
	}
	defer group.close()
	if srsPID == 0 {
		srsPID = 1
	}
	ticks, err := clockTicks()
	if err != nil {
		return step{}, err
	}
	sampler := newProcSampler("/proc", "/sys/fs/cgroup", srsPID, group.pids(), ticks)
	if _, err := sampler.sampleAt(time.Now()); err != nil {
		return step{}, err
	}
	all := make([]sample, 0, seconds)
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for second := 1; second <= seconds; second++ {
		<-ticker.C
		counters, err := group.collectWithTimestamp(second, clock.get(), second == seconds)
		if err != nil {
			return step{}, err
		}
		now := time.Now()
		usage, err := sampler.sampleAt(now)
		if err != nil {
			return step{}, err
		}
		all = append(all, sample{Second: second, UnixMillis: now.UnixMilli(), SRSCPUPercent: usage.srsCPUPercent, SRSCPUUsageUsec: usage.srsCPUUsageUsec, SRSRSSBytes: usage.srsRSSBytes, WorkerCPUPercent: usage.workerCPUPercent, Frames: counters.Frames, Segments: counters.Segments, Bytes: counters.Bytes, LagMillis: counters.LagMillis, Failures: counters.Failures})
	}
	evaluated, _ := evaluateRampStep(scenarios[scenario], clients, settle, all, defaultClientCap)
	evaluated.Mode = mode
	evaluated.Samples = all
	return evaluated, nil
}
