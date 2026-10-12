// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"flag"
	"fmt"
	"hash/fnv"
	"strconv"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// maxShards is the cap of live SRS processes; more shards than that can never run.
const maxShards = 64

// shards is how many identical Common servers the process starts. Four by
// default: the cases start at once, so one single-threaded server is saturated
// for the first part of a run, and the real-time cases near their limit wait
// on it, more so beside the segment cases that publish whole files at once.
// With three, a fast run still had Common cases at 570-600ms.
var shards = 4

func init() {
	flag.Func("integ.shards", "split the Common server into K identical servers, cases spread by name", func(v string) error {
		n, err := strconv.Atoi(v)
		if err != nil || n < 1 || n > maxShards {
			return fmt.Errorf("want a number from 1 to %d, got %q", maxShards, v)
		}
		shards = n
		return nil
	})
}

// shardOf returns which of k Common shards a case runs on, by its name.
func shardOf(name string, k int) int {
	if k <= 1 {
		return 0
	}
	h := fnv.New32a()
	h.Write([]byte(name))
	return int(h.Sum32() % uint32(k))
}

// procCount counts the SRS processes of the run: started in all, running now,
// and the most running at once.
type procCount struct {
	total, live, peak atomic.Int64
}

func (p *procCount) started() {
	p.total.Add(1)
	live := p.live.Add(1)
	for {
		peak := p.peak.Load()
		if live <= peak || p.peak.CompareAndSwap(peak, live) {
			return
		}
	}
}

func (p *procCount) exited() {
	p.live.Add(-1)
}

// cpuLine reports the CPU a server used in its wall time, and the share of
// one core that is, since SRS runs on one thread.
func cpuLine(name string, cpu, wall time.Duration) string {
	line := fmt.Sprintf("srs-integ: %s used %.2fs of CPU in %.2fs", name, cpu.Seconds(), wall.Seconds())
	if wall > 0 {
		line += fmt.Sprintf(", %d%% of one core", int(cpu*100/wall))
	}
	return line
}

// slots limits how many cases run a burst on one server at once.
type slots struct{ ch chan struct{} }

func newSlots(n int) *slots { return &slots{ch: make(chan struct{}, n)} }

// acquire waits for a slot, out of the case time when t is a case, and
// returns the func that frees it.
func (s *slots) acquire(t testing.TB) func() {
	untimed(t, func() { s.ch <- struct{}{} })
	var once sync.Once
	return func() { once.Do(func() { <-s.ch }) }
}

// burstSlots is how many cases may send a burst, such as a whole file published
// without pacing, to one server at once. SRS runs on one thread and shares it
// fairly, so many bursts at once make every one of them, and every real-time
// case beside them, as slow as all of them together.
const burstSlots = 2

// Burst waits for one of the server's burst slots, out of the case time, and
// returns the func that frees it. A case holds it while it sends its burst.
func (s *Server) Burst(t testing.TB) func() {
	return s.burst.acquire(t)
}
