// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"
)

// PortRange is an inclusive range of ports, such as 14000-14999.
type PortRange struct {
	Lo, Hi int
}

func (r PortRange) String() string {
	return fmt.Sprintf("%d-%d", r.Lo, r.Hi)
}

// ParseRange parses "lo-hi", the format of SRS_INTEG_PORTS.
func ParseRange(s string) (PortRange, error) {
	parts := strings.Split(strings.TrimSpace(s), "-")
	if len(parts) != 2 {
		return PortRange{}, fmt.Errorf("port range %q is not lo-hi", s)
	}
	lo, err1 := strconv.Atoi(parts[0])
	hi, err2 := strconv.Atoi(parts[1])
	if err1 != nil || err2 != nil || lo < 1 || hi > 65535 || lo > hi {
		return PortRange{}, fmt.Errorf("port range %q is not lo-hi with 1 <= lo <= hi <= 65535", s)
	}
	return PortRange{lo, hi}, nil
}

// Split returns the part of the range one process allocates from: the first
// four fifths for the main process and the last fifth for the timer process,
// so 14000-14999 gives 14000-14799 and 14800-14999.
func (r PortRange) Split(timer bool) PortRange {
	n := r.Hi - r.Lo + 1
	mid := r.Lo + n*4/5
	if timer {
		return PortRange{mid, r.Hi}
	}
	return PortRange{r.Lo, mid - 1}
}

// Allocator hands out ports from a range, each at most once in a run, and
// skips the ports that are in use.
type Allocator struct {
	r     PortRange
	free  func(port int) bool
	mu    sync.Mutex
	next  int
	count int
}

// NewAllocator allocates from r, using free to check that a port is not in
// use. A nil free uses PortFree.
func NewAllocator(r PortRange, free func(port int) bool) *Allocator {
	if free == nil {
		free = PortFree
	}
	return &Allocator{r: r, free: free, next: r.Lo}
}

// Alloc returns the next free port that was never handed out, or an error
// when the range is exhausted.
func (a *Allocator) Alloc() (int, error) {
	a.mu.Lock()
	defer a.mu.Unlock()

	for a.next <= a.r.Hi {
		port := a.next
		a.next++
		if a.free(port) {
			a.count++
			return port, nil
		}
	}
	return 0, fmt.Errorf("port range exhausted, widen SRS_INTEG_PORTS: %v, %d allocated", a.r, a.count)
}

// Allocated returns the number of ports handed out so far.
func (a *Allocator) Allocated() int {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.count
}

// PortFree reports whether port is free over both TCP and UDP.
func PortFree(port int) bool {
	p := strconv.Itoa(port)

	// Go listens with SO_REUSEADDR, so on macOS a bind of the wildcard address
	// succeeds beside a TCP listener on 127.0.0.1; a connect finds that one.
	if conn, err := net.DialTimeout("tcp", "127.0.0.1:"+p, 200*time.Millisecond); err == nil {
		conn.Close()
		return false
	}
	tcp, err := net.Listen("tcp", ":"+p)
	if err != nil {
		return false
	}
	defer tcp.Close()

	for _, addr := range []string{"127.0.0.1:" + p, ":" + p} {
		udp, err := net.ListenPacket("udp", addr)
		if err != nil {
			return false
		}
		udp.Close()
	}
	return true
}
