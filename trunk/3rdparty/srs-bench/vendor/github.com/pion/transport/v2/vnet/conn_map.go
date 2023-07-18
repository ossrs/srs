// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"errors"
	"net"
	"sync"
)

var (
	errAddressAlreadyInUse       = errors.New("address already in use")
	errNoSuchUDPConn             = errors.New("no such UDPConn")
	errCannotRemoveUnspecifiedIP = errors.New("cannot remove unspecified IP by the specified IP")
)

type udpConnMap struct {
	portMap map[int][]*UDPConn
	mutex   sync.RWMutex
}

func newUDPConnMap() *udpConnMap {
	return &udpConnMap{
		portMap: map[int][]*UDPConn{},
	}
}

func (m *udpConnMap) insert(conn *UDPConn) error {
	m.mutex.Lock()
	defer m.mutex.Unlock()

	udpAddr := conn.LocalAddr().(*net.UDPAddr) //nolint:forcetypeassert

	// check if the port has a listener
	conns, ok := m.portMap[udpAddr.Port]
	if ok {
		if udpAddr.IP.IsUnspecified() {
			return errAddressAlreadyInUse
		}

		for _, conn := range conns {
			laddr := conn.LocalAddr().(*net.UDPAddr) //nolint:forcetypeassert
			if laddr.IP.IsUnspecified() || laddr.IP.Equal(udpAddr.IP) {
				return errAddressAlreadyInUse
			}
		}

		conns = append(conns, conn)
	} else {
		conns = []*UDPConn{conn}
	}

	m.portMap[udpAddr.Port] = conns
	return nil
}

func (m *udpConnMap) find(addr net.Addr) (*UDPConn, bool) {
	m.mutex.Lock() // could be RLock, but we have delete() op
	defer m.mutex.Unlock()

	udpAddr := addr.(*net.UDPAddr) //nolint:forcetypeassert

	if conns, ok := m.portMap[udpAddr.Port]; ok {
		if udpAddr.IP.IsUnspecified() {
			// pick the first one appears in the iteration
			if len(conns) == 0 {
				// This can't happen!
				delete(m.portMap, udpAddr.Port)
				return nil, false
			}
			return conns[0], true
		}

		for _, conn := range conns {
			laddr := conn.LocalAddr().(*net.UDPAddr) //nolint:forcetypeassert
			if laddr.IP.IsUnspecified() || laddr.IP.Equal(udpAddr.IP) {
				return conn, ok
			}
		}
	}

	return nil, false
}

func (m *udpConnMap) delete(addr net.Addr) error {
	m.mutex.Lock()
	defer m.mutex.Unlock()

	udpAddr := addr.(*net.UDPAddr) //nolint:forcetypeassert

	conns, ok := m.portMap[udpAddr.Port]
	if !ok {
		return errNoSuchUDPConn
	}

	if udpAddr.IP.IsUnspecified() {
		// remove all from this port
		delete(m.portMap, udpAddr.Port)
		return nil
	}

	newConns := []*UDPConn{}

	for _, conn := range conns {
		laddr := conn.LocalAddr().(*net.UDPAddr) //nolint:forcetypeassert
		if laddr.IP.IsUnspecified() {
			// This can't happen!
			return errCannotRemoveUnspecifiedIP
		}

		if laddr.IP.Equal(udpAddr.IP) {
			continue
		}

		newConns = append(newConns, conn)
	}

	if len(newConns) == 0 {
		delete(m.portMap, udpAddr.Port)
	} else {
		m.portMap[udpAddr.Port] = newConns
	}

	return nil
}

// size returns the number of UDPConns (UDP listeners)
func (m *udpConnMap) size() int {
	m.mutex.RLock()
	defer m.mutex.RUnlock()

	n := 0
	for _, conns := range m.portMap {
		n += len(conns)
	}

	return n
}
