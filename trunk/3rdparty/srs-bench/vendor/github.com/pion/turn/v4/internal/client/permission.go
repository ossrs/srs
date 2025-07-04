// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package client

import (
	"net"
	"sync"
	"sync/atomic"

	"github.com/pion/turn/v4/internal/ipnet"
)

type permState int32

const (
	permStateIdle permState = iota
	permStatePermitted
)

type permission struct {
	addr  net.Addr
	st    permState    // Thread-safe (atomic op)
	mutex sync.RWMutex // Thread-safe
}

func (p *permission) setState(state permState) {
	atomic.StoreInt32((*int32)(&p.st), int32(state))
}

func (p *permission) state() permState {
	return permState(atomic.LoadInt32((*int32)(&p.st)))
}

// Thread-safe permission map.
type permissionMap struct {
	permMap map[string]*permission
	mutex   sync.RWMutex
}

func (m *permissionMap) insert(addr net.Addr, p *permission) bool {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	p.addr = addr
	m.permMap[ipnet.FingerprintAddr(addr)] = p

	return true
}

func (m *permissionMap) find(addr net.Addr) (*permission, bool) {
	m.mutex.RLock()
	defer m.mutex.RUnlock()
	p, ok := m.permMap[ipnet.FingerprintAddr(addr)]

	return p, ok
}

func (m *permissionMap) delete(addr net.Addr) {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	delete(m.permMap, ipnet.FingerprintAddr(addr))
}

func (m *permissionMap) addrs() []net.Addr {
	m.mutex.RLock()
	defer m.mutex.RUnlock()

	addrs := []net.Addr{}
	for _, p := range m.permMap {
		addrs = append(addrs, p.addr)
	}

	return addrs
}

func newPermissionMap() *permissionMap {
	return &permissionMap{
		permMap: map[string]*permission{},
	}
}
