// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"errors"
	"strings"
	"sync"
	"testing"
)

// TestSharedStartsOnce checks the groups that share a server start it once,
// even when they ask at the same time, and get the same server.
func TestSharedStartsOnce(t *testing.T) {
	var set sharedSet
	var mu sync.Mutex
	var names []string
	start := func(name string, cfg Config) (*Server, error) {
		mu.Lock()
		defer mu.Unlock()
		names = append(names, name)
		return &Server{Dir: name}, nil
	}

	cfg := RemuxConfig("flv")
	got := make([]*Server, 8)
	var wg sync.WaitGroup
	for i := range got {
		wg.Add(1)
		go func() {
			defer wg.Done()
			s, err := set.get("Plain", cfg, start)
			if err != nil {
				t.Errorf("get: %v", err)
			}
			got[i] = s
		}()
	}
	wg.Wait()

	if len(names) != 1 || names[0] != "Plain" {
		t.Errorf("started %v, want Plain once", names)
	}
	for i, s := range got {
		if s == nil || s != got[0] {
			t.Errorf("call %v got %p, want %p", i, s, got[0])
		}
	}
	if servers := set.servers(); len(servers) != 1 || servers[0] != got[0] {
		t.Errorf("servers %v, want the one started", servers)
	}
}

// TestSharedOtherName checks another name is another server.
func TestSharedOtherName(t *testing.T) {
	var set sharedSet
	start := func(name string, cfg Config) (*Server, error) { return &Server{Dir: name}, nil }
	a, _ := set.get("A", RemuxConfig("flv"), start)
	b, _ := set.get("B", RemuxConfig("flv"), start)
	if a == nil || b == nil || a == b {
		t.Errorf("A %p and B %p, want two servers", a, b)
	}
	if n := len(set.servers()); n != 2 {
		t.Errorf("%v servers, want 2", n)
	}
}

// TestSharedConfigMustMatch checks a group that asks for a shared server
// with another config fails, rather than running on a server it didn't ask
// for.
func TestSharedConfigMustMatch(t *testing.T) {
	var set sharedSet
	start := func(name string, cfg Config) (*Server, error) { return &Server{Dir: name}, nil }
	if _, err := set.get("Plain", RemuxConfig("flv"), start); err != nil {
		t.Fatalf("get: %v", err)
	}
	_, err := set.get("Plain", RemuxConfig("ts"), start)
	if err == nil || !strings.Contains(err.Error(), "another config") {
		t.Errorf("err %v, want another config", err)
	}
}

// TestSharedStartError checks a failed start is returned to every group
// that asks, and not tried again.
func TestSharedStartError(t *testing.T) {
	var set sharedSet
	calls := 0
	start := func(name string, cfg Config) (*Server, error) {
		calls++
		return nil, errors.New("no binary")
	}
	for range 2 {
		if _, err := set.get("Plain", RemuxConfig("flv"), start); err == nil || !strings.Contains(err.Error(), "no binary") {
			t.Errorf("err %v, want no binary", err)
		}
	}
	if calls != 1 {
		t.Errorf("%v starts, want 1", calls)
	}
	if n := len(set.servers()); n != 0 {
		t.Errorf("%v servers, want none", n)
	}
}
