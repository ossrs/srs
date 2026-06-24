// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package sync

import (
	"sort"
	"testing"
)

func TestNewMap_ReturnsEmpty(t *testing.T) {
	m := NewMap[string, int]()
	if m == nil {
		t.Fatal("NewMap returned nil")
	}
	if v, ok := m.Load("missing"); ok || v != 0 {
		t.Fatalf("Load(missing) = (%v, %v), want (0, false)", v, ok)
	}
}

func TestStore_AndLoad(t *testing.T) {
	m := NewMap[string, int]()
	m.Store("a", 1)

	v, ok := m.Load("a")
	if !ok || v != 1 {
		t.Fatalf("Load(a) = (%v, %v), want (1, true)", v, ok)
	}
}

func TestLoad_MissingReturnsZero(t *testing.T) {
	m := NewMap[string, int]()
	v, ok := m.Load("nope")
	if ok {
		t.Fatal("Load on missing key returned ok=true")
	}
	if v != 0 {
		t.Fatalf("Load on missing key returned %v, want zero", v)
	}
}

func TestDelete_RemovesKey(t *testing.T) {
	m := NewMap[string, int]()
	m.Store("a", 1)
	m.Delete("a")

	if _, ok := m.Load("a"); ok {
		t.Fatal("Load(a) returned ok=true after Delete")
	}
}

func TestDelete_MissingIsNoop(t *testing.T) {
	m := NewMap[string, int]()
	m.Delete("never-stored")
}

func TestLoadAndDelete_Present(t *testing.T) {
	m := NewMap[string, int]()
	m.Store("a", 42)

	v, loaded := m.LoadAndDelete("a")
	if !loaded {
		t.Fatal("LoadAndDelete returned loaded=false for present key")
	}
	if v != 42 {
		t.Fatalf("LoadAndDelete returned %v, want 42", v)
	}
	if _, ok := m.Load("a"); ok {
		t.Fatal("key still present after LoadAndDelete")
	}
}

func TestLoadAndDelete_Absent(t *testing.T) {
	m := NewMap[string, int]()
	v, loaded := m.LoadAndDelete("nope")
	if loaded {
		t.Fatal("LoadAndDelete returned loaded=true for absent key")
	}
	if v != 0 {
		t.Fatalf("LoadAndDelete on absent key returned %v, want zero", v)
	}
}

func TestLoadOrStore_StoresWhenAbsent(t *testing.T) {
	m := NewMap[string, int]()
	actual, loaded := m.LoadOrStore("a", 7)
	if loaded {
		t.Fatal("LoadOrStore returned loaded=true for absent key")
	}
	if actual != 7 {
		t.Fatalf("LoadOrStore returned %v, want 7", actual)
	}

	v, ok := m.Load("a")
	if !ok || v != 7 {
		t.Fatalf("Load after LoadOrStore = (%v, %v), want (7, true)", v, ok)
	}
}

func TestLoadOrStore_LoadsWhenPresent(t *testing.T) {
	m := NewMap[string, int]()
	m.Store("a", 1)

	actual, loaded := m.LoadOrStore("a", 999)
	if !loaded {
		t.Fatal("LoadOrStore returned loaded=false for present key")
	}
	if actual != 1 {
		t.Fatalf("LoadOrStore returned %v, want existing value 1", actual)
	}

	v, _ := m.Load("a")
	if v != 1 {
		t.Fatalf("LoadOrStore overwrote existing value: got %v, want 1", v)
	}
}

func TestRange_VisitsAllEntries(t *testing.T) {
	m := NewMap[string, int]()
	want := map[string]int{"a": 1, "b": 2, "c": 3}
	for k, v := range want {
		m.Store(k, v)
	}

	got := map[string]int{}
	m.Range(func(key string, value int) bool {
		got[key] = value
		return true
	})

	if len(got) != len(want) {
		t.Fatalf("Range visited %d entries, want %d", len(got), len(want))
	}
	for k, v := range want {
		if got[k] != v {
			t.Fatalf("Range got[%q] = %v, want %v", k, got[k], v)
		}
	}
}

func TestRange_EarlyStop(t *testing.T) {
	m := NewMap[string, int]()
	m.Store("a", 1)
	m.Store("b", 2)
	m.Store("c", 3)

	visited := 0
	m.Range(func(key string, value int) bool {
		visited++
		return false
	})

	if visited != 1 {
		t.Fatalf("Range visited %d entries after returning false, want 1", visited)
	}
}

func TestMap_PointerValueType(t *testing.T) {
	type entry struct{ n int }
	m := NewMap[string, *entry]()

	e := &entry{n: 5}
	m.Store("k", e)

	got, ok := m.Load("k")
	if !ok {
		t.Fatal("Load returned ok=false")
	}
	if got != e {
		t.Fatalf("Load returned different pointer: %p vs %p", got, e)
	}

	keys := []string{}
	m.Range(func(key string, value *entry) bool {
		keys = append(keys, key)
		return true
	})
	sort.Strings(keys)
	if len(keys) != 1 || keys[0] != "k" {
		t.Fatalf("Range keys = %v, want [k]", keys)
	}
}
