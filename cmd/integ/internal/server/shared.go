// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"fmt"
	"reflect"
	"sort"
	"sync"
	"testing"
)

// sharedSet holds the shared variant servers of a process, by name.
type sharedSet struct {
	mu sync.Mutex
	m  map[string]*sharedEntry
}

type sharedEntry struct {
	once   sync.Once
	cfg    Config
	server *Server
	err    error
}

// get returns the server of a name, started with start on the first call;
// the calls for other names don't wait for it. A later call must ask for the
// same config.
func (s *sharedSet) get(name string, cfg Config, start func(string, Config) (*Server, error)) (*Server, error) {
	s.mu.Lock()
	if s.m == nil {
		s.m = map[string]*sharedEntry{}
	}
	e, ok := s.m[name]
	if !ok {
		e = &sharedEntry{cfg: cfg}
		s.m[name] = e
	}
	s.mu.Unlock()
	if !reflect.DeepEqual(e.cfg, cfg) {
		return nil, fmt.Errorf("shared server %v: asked with another config than the one it started with", name)
	}
	e.once.Do(func() { e.server, e.err = start(name, cfg) })
	return e.server, e.err
}

// servers returns the servers that started, by name.
func (s *sharedSet) servers() []*Server {
	s.mu.Lock()
	defer s.mu.Unlock()
	names := make([]string, 0, len(s.m))
	for name := range s.m {
		names = append(names, name)
	}
	sort.Strings(names)
	var out []*Server
	for _, name := range names {
		if e := s.m[name]; e.server != nil {
			out = append(out, e.server)
		}
	}
	return out
}

// Shared returns a variant server that several groups share, like Common:
// the first case or group that asks for a name starts it with cfg, every
// later one must ask with the same cfg, and Main stops it. Groups of
// different features use it when one config serves all of them, so the
// suite runs fewer processes; each group still names its own cases.
func Shared(t testing.TB, name string, cfg Config) *Server {
	t.Helper()
	var s *Server
	var err error
	untimed(t, func() { s, err = run.shared.get(name, cfg, Start) })
	if err != nil {
		t.Fatalf("%v server: %v", name, err)
	}
	t.Cleanup(func() {
		if t.Failed() {
			s.logTo(t)
		}
	})
	return s
}
