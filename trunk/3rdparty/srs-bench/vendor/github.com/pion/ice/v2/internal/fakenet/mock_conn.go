// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package fakenet

import (
	"net"
	"time"
)

// MockPacketConn for tests
type MockPacketConn struct{}

func (m *MockPacketConn) ReadFrom([]byte) (n int, addr net.Addr, err error) { return 0, nil, nil } //nolint:revive
func (m *MockPacketConn) WriteTo([]byte, net.Addr) (n int, err error)       { return 0, nil }      //nolint:revive
func (m *MockPacketConn) Close() error                                      { return nil }         //nolint:revive
func (m *MockPacketConn) LocalAddr() net.Addr                               { return nil }         //nolint:revive
func (m *MockPacketConn) SetDeadline(time.Time) error                       { return nil }         //nolint:revive
func (m *MockPacketConn) SetReadDeadline(time.Time) error                   { return nil }         //nolint:revive
func (m *MockPacketConn) SetWriteDeadline(time.Time) error                  { return nil }         //nolint:revive
