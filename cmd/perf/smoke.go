package main

import (
	"encoding/json"
	"fmt"
	"io"
	"sync"
	"time"
)

type playerFrame struct {
	id      int
	message rtmpMessage
}

func runRTMPSmoke(output io.Writer) error {
	const clients = 100
	tags, err := loadFLV("doc/source.flv")
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
	frames := make(chan playerFrame, clients*100)
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
					select {
					case frames <- playerFrame{id, m}:
					case <-stop:
						return
					}
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
	start := time.Now()
	last := make([]time.Time, clients)
	var frameCount, byteCount int64
	var maxLag int64
	for time.Since(start) < 10*time.Second {
		select {
		case f := <-frames:
			frameCount++
			byteCount += int64(len(f.message.payload))
			last[f.id] = time.Now()
			lag := int64(clock.get()) - int64(f.message.timestamp)
			if lag > maxLag {
				maxLag = lag
			}
		case err := <-errors:
			return err
		case <-time.After(100 * time.Millisecond):
		}
	}
	for i, at := range last {
		if at.IsZero() || time.Since(at) > 5*time.Second {
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
	r := result{Scenario: scenarios["rtmp-rtmp"], Steps: []step{{Mode: "smoke", Clients: clients, Samples: []sample{{Second: 10, Frames: frameCount, Bytes: byteCount, LagMillis: maxLag}}, Health: health{Healthy: true}, Validity: validity{Valid: true}}}, Health: health{Healthy: true}, Validity: validity{Valid: true}}
	return json.NewEncoder(output).Encode(r)
}
