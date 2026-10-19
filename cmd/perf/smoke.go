package main

import (
	"encoding/json"
	"fmt"
	"io"
	"sync"
	"time"
)

func runRTMPSmoke(output io.Writer, cmd command) error {
	const clients = 100
	tags, flatness, err := loadSource(cmd.media)
	if err != nil {
		return err
	}
	clock := &mediaClock{}
	stop := make(chan struct{})
	var stopOnce sync.Once
	stopAll := func() { stopOnce.Do(func() { close(stop) }) }
	defer stopAll()
	pub, err := dialRTMP("127.0.0.1:1935", "live")
	if err != nil {
		return err
	}
	pubDone := make(chan error, 1)
	go func() { pubDone <- publishRTMP(pub, "perf-smoke", tags, clock, stop) }()
	deadline := time.After(5 * time.Second)
	for clock.get() == 0 {
		select {
		case err := <-pubDone:
			return fmt.Errorf("publish: %w", err)
		case <-deadline:
			return fmt.Errorf("publisher sent no frames")
		case <-time.After(10 * time.Millisecond):
		}
	}
	timeline, err := lagTimeline(cmd.media, tags)
	if err != nil {
		return err
	}
	players := newWorkerPlayers(clients, timeline)
	errors := make(chan error, clients)
	var wg sync.WaitGroup
	for i := 0; i < clients; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			c, err := dialRTMP("127.0.0.1:1935", "live")
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
			if err := playRTMP(c, "perf-smoke", stop, report); err != nil {
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
	sampler := newProcSampler("/proc", "/sys/fs/cgroup", 1, nil, ticks)
	if _, err := sampler.sampleAt(time.Now()); err != nil {
		return err
	}
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	var samples []sample
	var maxLag int64
	for len(samples) < 10 {
		select {
		case err := <-errors:
			return err
		case now := <-ticker.C:
			counters := players.sample(clock.get(), false)
			usage, err := sampler.sampleAt(now)
			if err != nil {
				return err
			}
			second := sample{Second: len(samples) + 1, Frames: counters.Frames, Bytes: counters.Bytes, LagMillis: counters.LagMillis}
			second.SRSCPUPercent, second.SRSCPUUsageUsec, second.SRSRSSBytes = usage.srsCPUPercent, usage.srsCPUUsageUsec, usage.srsRSSBytes
			samples = append(samples, second)
			if counters.LagMillis > maxLag {
				maxLag = counters.LagMillis
			}
		}
	}
	for i := range players.last {
		at := players.last[i].Load()
		if at == 0 || time.Since(time.Unix(0, at)) > 5*time.Second {
			return fmt.Errorf("player %d has no recent frame", i)
		}
	}
	if maxLag > 2000 {
		return fmt.Errorf("player lag %dms exceeds 2s", maxLag)
	}
	stopAll()
	select {
	case err := <-pubDone:
		if err != nil {
			return err
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
	r := result{Setup: withSource(setup{}, cmd.media, flatness), Scenario: scenarios["rtmp-rtmp"], Steps: []step{{Mode: "smoke", Clients: clients, SettleSeconds: 2, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}}, Health: health{Healthy: true}, Validity: validity{Valid: true}}
	return json.NewEncoder(output).Encode(summarizeResult(r, "smoke"))
}
