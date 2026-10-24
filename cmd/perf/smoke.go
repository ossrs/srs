package main

import (
	"encoding/json"
	"fmt"
	"io"
	"sync"
	"time"
)

// runSmoke publishes the source with the scenario's publisher and plays it
// with 100 players of the scenario's protocol for 10s, or for three segment
// durations, by the scenario's gap and lag rules.
func runSmoke(output io.Writer, cmd command) error {
	const clients = 100
	s := scenarios[cmd.scenario]
	if s.Unit == "probes" {
		return runDelaySmoke(output, cmd)
	}
	if s.Unit != "clients" {
		return runPublisherSmoke(output, cmd, clients)
	}
	dial, err := playerDialer(s.Player)
	if err != nil {
		return err
	}
	tags, flatness, err := loadSource(cmd.media)
	if err != nil {
		return err
	}
	// SRS, or the proxy with its origins beside it, is pid 1 of the load
	// container's pid namespace.
	measured, origins, err := targetSetup(withSource(setup{}, cmd.media, flatness), cmd.target, proxyAPI, "/proc", 1)
	if err != nil {
		return err
	}
	// One stream on SRS, or one on each origin behind the proxy, with the
	// players split over them.
	apis := originAPIs(origins)
	published, err := startStepStreams(scenarios[cmd.scenario].Publisher, "perf-smoke", cmd.media, tags, apis)
	if err != nil {
		return err
	}
	stop := make(chan struct{})
	var stopOnce sync.Once
	var stopErr error
	stopAll := func() { stopOnce.Do(func() { close(stop); stopErr = stopStreams(published) }) }
	// A publisher that does not stop must not hold up an error's return.
	defer func() { go stopAll() }()
	if measured.Shares, err = targetShares(s, apis, published, "perf-smoke", clients); err != nil {
		return err
	}
	names := streamNames(published)
	timeline, err := lagTimeline(cmd.media, tags)
	if err != nil {
		return err
	}
	players := newWorkerPlayers(clients, timeline)
	players.gap = s.gap()
	players.streams = playerStreamIndex(0, clients, len(names))
	errors := make(chan error, clients)
	var wg sync.WaitGroup
	for i := 0; i < clients; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			c, err := dial()
			if err != nil {
				errors <- fmt.Errorf("player %d dial: %w", id, err)
				return
			}
			report := make(chan rtmpMessage, 128)
			go func() {
				for m := range report {
					players.received(id, m)
				}
			}()
			if err := c.play(names[players.streams[id]], stop, report); err != nil {
				select {
				case <-stop:
				default:
					errors <- fmt.Errorf("player %d: %w", id, err)
				}
			}
			close(report)
		}(i)
	}
	// SRS shares the load container's pid namespace as pid 1.
	ticks, err := clockTicks()
	if err != nil {
		return err
	}
	sampler := newProcSampler("/proc", "/sys/fs/cgroup", 1, nil, ticks).withOrigins(origins)
	if _, err := sampler.sampleAt(time.Now()); err != nil {
		return err
	}
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	var samples []sample
	var maxLag int64
	for len(samples) < s.smokeSeconds() {
		select {
		case err := <-errors:
			return err
		case now := <-ticker.C:
			counters := players.sample(streamTimestamps(published), false)
			usage, err := sampler.sampleAt(now)
			if err != nil {
				return err
			}
			second := sample{Second: len(samples) + 1, Frames: counters.Frames, Segments: counters.Segments, Bytes: counters.Bytes, LagMillis: counters.LagMillis}
			second.SRSCPUPercent, second.SRSCPUUsageUsec, second.SRSRSSBytes = usage.srsCPUPercent, usage.srsCPUUsageUsec, usage.srsRSSBytes
			second.OriginCPUPercent, second.OriginCPUUsageUsec = usage.originCPUPercent, usage.originCPUUsageUsec
			samples = append(samples, second)
			if counters.LagMillis > maxLag {
				maxLag = counters.LagMillis
			}
		}
	}
	for i := range players.last {
		at := players.last[i].Load()
		if at == 0 || time.Since(time.Unix(0, at)) > s.gap() {
			return fmt.Errorf("player %d has no recent frame", i)
		}
	}
	if limit := s.lagLimit(); limit > 0 && maxLag > limit {
		return fmt.Errorf("player lag %dms exceeds %dms", maxLag, limit)
	}
	stopped := make(chan struct{})
	go func() { stopAll(); close(stopped) }()
	select {
	case <-stopped:
		if stopErr != nil {
			return stopErr
		}
	case <-time.After(3 * time.Second):
		return fmt.Errorf("publisher did not stop")
	}
	done := make(chan struct{})
	go func() { wg.Wait(); close(done) }()
	select {
	case <-time.After(3 * time.Second):
		return fmt.Errorf("players did not stop")
	case <-done:
	}
	r := result{Setup: measured, Scenario: scenarios[cmd.scenario], Steps: []step{{Mode: "smoke", Clients: clients, SettleSeconds: 2, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}}, Health: health{Healthy: true}, Validity: validity{Valid: true}}
	return json.NewEncoder(output).Encode(summarizeResult(r, "smoke"))
}

// runPublisherSmoke runs a publisher or many-streams scenario's clients in
// this process, as a worker runs them, for 10s: every publisher keeps its
// pace, every player gets frames within the lag limit, and none fails, the
// last second judging a client that never sent or got anything.
func runPublisherSmoke(output io.Writer, cmd command, clients int) error {
	s := scenarios[cmd.scenario]
	tags, flatness, err := loadSource(cmd.media)
	if err != nil {
		return err
	}
	measured, origins, err := targetSetup(withSource(setup{}, cmd.media, flatness), cmd.target, proxyAPI, "/proc", 1)
	if err != nil {
		return err
	}
	timeline, err := lagTimeline(cmd.media, tags)
	if err != nil {
		return err
	}
	runtime, err := startClients(s, clients, []string{"perf-smoke"}, 0, cmd.media, tags, timeline)
	if err != nil {
		return err
	}
	closed := false
	defer func() {
		if !closed {
			runtime.close()
		}
	}()
	ticks, err := clockTicks()
	if err != nil {
		return err
	}
	sampler := newProcSampler("/proc", "/sys/fs/cgroup", 1, nil, ticks).withOrigins(origins)
	if _, err := sampler.sampleAt(time.Now()); err != nil {
		return err
	}
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	var samples []sample
	for len(samples) < s.smokeSeconds() {
		now := <-ticker.C
		counters := runtime.sample(nil, len(samples)+1 == s.smokeSeconds())
		usage, err := sampler.sampleAt(now)
		if err != nil {
			return err
		}
		second := sample{Second: len(samples) + 1, Frames: counters.Frames, Bytes: counters.Bytes, LagMillis: counters.LagMillis, Failures: counters.Failures}
		second.SRSCPUPercent, second.SRSCPUUsageUsec, second.SRSRSSBytes = usage.srsCPUPercent, usage.srsCPUUsageUsec, usage.srsRSSBytes
		second.OriginCPUPercent, second.OriginCPUUsageUsec = usage.originCPUPercent, usage.originCPUUsageUsec
		samples = append(samples, second)
		if counters.Failures > 0 {
			return fmt.Errorf("second %d: %d %s failed", second.Second, counters.Failures, s.Unit)
		}
		if limit := s.lagLimit(); limit > 0 && counters.LagMillis > limit {
			return fmt.Errorf("second %d: lag %dms exceeds %dms", second.Second, counters.LagMillis, limit)
		}
	}
	if measured.Shares, err = targetShares(s, originAPIs(origins), nil, "perf-smoke", clients); err != nil {
		return err
	}
	done := make(chan struct{})
	go func() { runtime.close(); close(done) }()
	closed = true
	select {
	case <-time.After(3 * time.Second):
		return fmt.Errorf("%s did not stop", s.Unit)
	case <-done:
	}
	r := result{Setup: measured, Scenario: s, Steps: []step{{Mode: "smoke", Clients: clients, SettleSeconds: 2, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}}, Health: health{Healthy: true}, Validity: validity{Valid: true}}
	return json.NewEncoder(output).Encode(summarizeResult(r, "smoke"))
}
