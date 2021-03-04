package client

import (
	"net"
	"sync"
	"sync/atomic"
)

type permState int32

const (
	permStateIdle permState = iota
	permStatePermitted
)

type permission struct {
	st    permState    // thread-safe (atomic op)
	mutex sync.RWMutex // thread-safe
}

func (p *permission) setState(state permState) {
	atomic.StoreInt32((*int32)(&p.st), int32(state))
}

func (p *permission) state() permState {
	return permState(atomic.LoadInt32((*int32)(&p.st)))
}

// Thread-safe permission map
type permissionMap struct {
	permMap map[string]*permission
	mutex   sync.RWMutex
}

func (m *permissionMap) insert(addr net.Addr, p *permission) bool {
	m.mutex.Lock()
	defer m.mutex.Unlock()

	udpAddr, ok := addr.(*net.UDPAddr)
	if !ok {
		return false
	}

	m.permMap[udpAddr.IP.String()] = p
	return true
}

func (m *permissionMap) find(addr net.Addr) (*permission, bool) {
	m.mutex.RLock()
	defer m.mutex.RUnlock()

	udpAddr, ok := addr.(*net.UDPAddr)
	if !ok {
		return nil, false
	}

	p, ok := m.permMap[udpAddr.IP.String()]
	return p, ok
}

func (m *permissionMap) delete(addr net.Addr) {
	m.mutex.Lock()
	defer m.mutex.Unlock()

	udpAddr, ok := addr.(*net.UDPAddr)
	if !ok {
		return
	}

	delete(m.permMap, udpAddr.IP.String())
}

func (m *permissionMap) addrs() []net.Addr {
	m.mutex.RLock()
	defer m.mutex.RUnlock()

	addrs := []net.Addr{}
	for k := range m.permMap {
		addrs = append(addrs, &net.UDPAddr{
			IP: net.ParseIP(k),
		})
	}
	return addrs
}

func newPermissionMap() *permissionMap {
	return &permissionMap{
		permMap: map[string]*permission{},
	}
}
