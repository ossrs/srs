// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"flag"
	"fmt"
	"sync"
	"testing"
	"time"
)

func TestShardOf(t *testing.T) {
	if got := shardOf("TestRtmp_Common/PublishPlay", 1); got != 0 {
		t.Errorf("one shard: %d, want 0", got)
	}

	// The same name always goes to the same shard, and the copies of the
	// cases spread over every shard.
	const k, n = 4, 1000
	count := make([]int, k)
	for i := 0; i < n; i++ {
		name := fmt.Sprintf("TestRtmp_Common/PublishPlay-%d", i+1)
		s := shardOf(name, k)
		if s < 0 || s >= k {
			t.Fatalf("shardOf(%s, %d) = %d, out of range", name, k, s)
		}
		if again := shardOf(name, k); again != s {
			t.Fatalf("shardOf(%s) = %d then %d", name, s, again)
		}
		count[s]++
	}
	for i, c := range count {
		if c < n/k/2 || c > n/k*3/2 {
			t.Errorf("shard %d has %d of %d cases, want about %d: %v", i, c, n, n/k, count)
		}
	}
}

// TestShardsDefault checks Common is split in two by default: the cases start
// at once, and one single-threaded server makes the real-time ones wait.
func TestShardsDefault(t *testing.T) {
	if shards != 3 {
		t.Errorf("shards %d by default, want 3", shards)
	}
}

func TestShardsFlag(t *testing.T) {
	defer func(n int) { shards = n }(shards)

	f := flag.Lookup("integ.shards")
	if f == nil {
		t.Fatal("no -integ.shards flag")
	}
	if err := f.Value.Set("2"); err != nil || shards != 2 {
		t.Errorf("set 2: shards %d, err %v", shards, err)
	}
	for _, bad := range []string{"0", "-1", "x", "65"} {
		if err := f.Value.Set(bad); err == nil {
			t.Errorf("set %q has no error", bad)
		}
	}
}

func TestProcCount(t *testing.T) {
	var p procCount
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); p.started() }()
	}
	wg.Wait()
	for i := 0; i < 8; i++ {
		p.exited()
	}
	p.started()
	p.exited()

	if p.total.Load() != 9 || p.live.Load() != 0 || p.peak.Load() != 8 {
		t.Errorf("total %d, live %d, peak %d; want 9, 0, 8", p.total.Load(), p.live.Load(), p.peak.Load())
	}
}

func TestCPULine(t *testing.T) {
	got := cpuLine("common-2", 3200*time.Millisecond, 5*time.Second)
	want := "srs-integ: common-2 used 3.20s of CPU in 5.00s, 64% of one core"
	if got != want {
		t.Errorf("cpuLine = %q, want %q", got, want)
	}
	if got := cpuLine("common", time.Second, 0); got != "srs-integ: common used 1.00s of CPU in 0.00s" {
		t.Errorf("cpuLine with no wall time = %q", got)
	}
}

// untimedTB records the time a case leaves out of its time, like check.C.
type untimedTB struct {
	testing.TB
	mu      sync.Mutex
	untimed int
}

func (u *untimedTB) Untimed(fn func()) {
	u.mu.Lock()
	u.untimed++
	u.mu.Unlock()
	fn()
}

func TestBurstSlots(t *testing.T) {
	s := newSlots(2)
	tb := &untimedTB{TB: t}

	var mu sync.Mutex
	running, most := 0, 0
	var wg sync.WaitGroup
	for i := 0; i < 6; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			release := s.acquire(tb)
			mu.Lock()
			running++
			most = max(most, running)
			mu.Unlock()
			time.Sleep(10 * time.Millisecond)
			mu.Lock()
			running--
			mu.Unlock()
			release()
		}()
	}
	wg.Wait()

	if most != 2 {
		t.Errorf("at most %d bursts at once, want 2", most)
	}
	if tb.untimed != 6 {
		t.Errorf("%d waits left out of the case time, want 6", tb.untimed)
	}
}
