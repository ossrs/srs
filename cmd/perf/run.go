package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

// The ramp stops at this many clients when no -cap is given. It is the cap of
// the TCP protocols; each scenario's config admits it plus its publisher.
const defaultClientCap = 40000

const (
	// A ramp step lasts this long when no -seconds is given.
	defaultRampSeconds = 5
	// A hold lasts this long when no -seconds is given.
	defaultHoldSeconds = 30
	// The first seconds of a step let new clients connect and settle.
	settleSeconds = 2
	// At most this many of the last samples count in a long step or hold.
	maxSteadySamples = 20
)

func stepSeconds(mode string, seconds int) int {
	if seconds > 0 {
		return seconds
	}
	if mode == "hold" {
		return defaultHoldSeconds
	}
	return defaultRampSeconds
}

func evaluateRampStep(clients int, samples []sample, cap int) (step, bool) {
	s := step{Mode: "ramp", Clients: clients, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}
	if len(samples) == 0 {
		s.Health = health{Reason: "no samples"}
		return s, true
	}
	var cpu float64
	noFrames := 0
	for _, sample := range samples {
		if sample.Failures > 0 {
			s.Health = health{Reason: "player failure"}
		}
		if sample.LagMillis > 2000 {
			s.Health = health{Reason: "player lag exceeds 2s"}
		}
		if sample.Frames == 0 {
			noFrames++
		} else {
			noFrames = 0
		}
		if noFrames >= 5 {
			s.Health = health{Reason: "no frames for 5s"}
		}
	}
	cpu = steadyCPU(samples)
	for _, sample := range samples {
		if v := classifyWorkerLoad(!s.Health.Healthy, sample.WorkerCPUPercent); !v.Valid {
			s.Validity = v
			break
		}
	}
	stop := !s.Health.Healthy || cpu >= 90 || clients >= cap
	return s, stop
}

// steadySamples skips the settle seconds and keeps at most the last
// maxSteadySamples: the last 3 of a 5s ramp step, the last 20 of a hold.
func steadySamples(samples []sample) []sample {
	if len(samples) > settleSeconds {
		samples = samples[settleSeconds:]
	}
	if len(samples) > maxSteadySamples {
		return samples[len(samples)-maxSteadySamples:]
	}
	return samples
}

func steadyCPU(samples []sample) float64 {
	steady := steadySamples(samples)
	if len(steady) == 0 {
		return 0
	}
	var cpu float64
	for _, v := range steady {
		cpu += v.SRSCPUPercent
	}
	return cpu / float64(len(steady))
}

// holdCPUPerClient is the SRS CPU per client of a hold, so two holds at the
// same client count show the noise between identical runs.
func holdCPUPerClient(s step) float64 {
	if s.Clients <= 0 || len(s.Samples) == 0 {
		return 0
	}
	return steadyCPU(s.Samples) / float64(s.Clients)
}

func rampSummary(steps []step) (int, float64) {
	var capacity int
	var cpuSum float64
	var count int
	for _, s := range steps {
		if !s.Health.Healthy || !s.Validity.Valid || len(s.Samples) == 0 {
			break
		}
		cpu := steadyCPU(s.Samples)
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
	tags, err := loadFLV("doc/source.flv")
	if err != nil {
		return err
	}
	mode := cmd.mode
	if mode == "" {
		mode = "ramp"
	}
	cores := cmd.cores
	if cores == 0 {
		cores = runtime.NumCPU()
	}
	seconds := stepSeconds(mode, cmd.seconds)
	cap := cmd.cap
	if cap == 0 {
		cap = defaultClientCap
	}
	base := cmd.output
	if base == "" {
		base = "cmd/perf/out"
	}
	for repeat := 1; repeat <= cmd.repeat; repeat++ {
		runName := fmt.Sprintf("%s-%s-%02d", time.Now().UTC().Format("20060102T150405Z"), cmd.scenario, repeat)
		r := result{Setup: captureSetup(cmd), Scenario: scenarios[cmd.scenario], Health: health{Healthy: true}, Validity: validity{Valid: true}}
		clients := cmd.clients
		if clients == 0 {
			clients = 100
		}
		r.Scenario.Parameters = map[string]string{"seconds": fmt.Sprint(seconds), "mode": mode, "clients": fmt.Sprint(clients), "cap": fmt.Sprint(cap), "workers": fmt.Sprint(cores), "media": "doc/source.flv"}
		if cmd.noPublisher {
			r.Scenario.Parameters["media"] = "none"
		}
		for {
			s, err := measureStep(mode, clients, seconds, cores, cmd.srsPID, tags, cmd.noPublisher)
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
			if mode == "hold" || !s.Health.Healthy || !s.Validity.Valid || clients >= cap {
				break
			}
			if steadyCPU(s.Samples) >= 90 {
				break
			}
			clients *= 2
			if clients > cap {
				clients = cap
			}
		}
		if mode == "ramp" {
			r.Capacity, r.CPUPerClient = rampSummary(r.Steps)
		} else if len(r.Steps) > 0 {
			r.CPUPerClient = holdCPUPerClient(r.Steps[0])
		}
		path, err := writeRunJSON(base, runName, r)
		if err != nil {
			return err
		}
		fmt.Fprintln(output, path)
	}
	return nil
}

func measureStep(mode string, clients, seconds, cores, srsPID int, tags []rtmpMessage, noPublisher bool) (step, error) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	clock := &mediaClock{}
	if !noPublisher {
		stop := make(chan struct{})
		publisher, err := dialRTMP("127.0.0.1:1935", "live")
		if err != nil {
			return step{}, err
		}
		pubDone := make(chan error, 1)
		go func() { pubDone <- publishRTMP(publisher, "perf-run", tags, clock, stop) }()
		defer func() { close(stop); <-pubDone }()
		deadline := time.Now().Add(5 * time.Second)
		for clock.get() == 0 {
			if time.Now().After(deadline) {
				return step{}, fmt.Errorf("publisher sent no frames")
			}
			time.Sleep(10 * time.Millisecond)
		}
	}
	group, err := startWorkers(ctx, nil, "rtmp-rtmp", clients, cores)
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
	sampler := newProcSampler("/proc", srsPID, group.pids(), ticks)
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
		usage, err := sampler.sampleAt(time.Now())
		if err != nil {
			return step{}, err
		}
		all = append(all, sample{Second: second, SRSCPUPercent: usage.srsCPUPercent, SRSRSSBytes: usage.srsRSSBytes, WorkerCPUPercent: usage.workerCPUPercent, Frames: counters.Frames, Bytes: counters.Bytes, LagMillis: counters.LagMillis, Failures: counters.Failures})
	}
	evaluated, _ := evaluateRampStep(clients, all, defaultClientCap)
	evaluated.Mode = mode
	evaluated.Samples = all
	return evaluated, nil
}
