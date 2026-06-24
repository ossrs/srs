// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package sync

import "sync"

type Map[K comparable, V any] interface {
	Delete(key K)
	Load(key K) (value V, ok bool)
	LoadAndDelete(key K) (value V, loaded bool)
	LoadOrStore(key K, value V) (actual V, loaded bool)
	Range(f func(key K, value V) bool)
	Store(key K, value V)
}

func NewMap[K comparable, V any]() Map[K, V] {
	return &mapImpl[K, V]{}
}

type mapImpl[K comparable, V any] struct {
	m sync.Map
}

func (m *mapImpl[K, V]) Delete(key K) {
	m.m.Delete(key)
}

func (m *mapImpl[K, V]) Load(key K) (value V, ok bool) {
	v, ok := m.m.Load(key)
	if !ok {
		return value, ok
	}
	return v.(V), ok
}

func (m *mapImpl[K, V]) LoadAndDelete(key K) (value V, loaded bool) {
	v, loaded := m.m.LoadAndDelete(key)
	if !loaded {
		return value, loaded
	}
	return v.(V), loaded
}

func (m *mapImpl[K, V]) LoadOrStore(key K, value V) (actual V, loaded bool) {
	a, loaded := m.m.LoadOrStore(key, value)
	return a.(V), loaded
}

func (m *mapImpl[K, V]) Range(f func(key K, value V) bool) {
	m.m.Range(func(key, value any) bool {
		return f(key.(K), value.(V))
	})
}

func (m *mapImpl[K, V]) Store(key K, value V) {
	m.m.Store(key, value)
}
