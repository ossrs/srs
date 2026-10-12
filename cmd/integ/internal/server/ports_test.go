// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"net"
	"strings"
	"testing"
)

func TestParseRange(t *testing.T) {
	good := map[string]PortRange{
		"14000-14999":   {14000, 14999},
		" 20000-20009 ": {20000, 20009},
		"30000-30000":   {30000, 30000},
	}
	for s, want := range good {
		got, err := ParseRange(s)
		if err != nil || got != want {
			t.Errorf("ParseRange(%q) = %v, %v; want %v", s, got, err, want)
		}
	}

	for _, s := range []string{"", "14000", "14000-", "-14999", "a-b", "14999-14000", "0-10", "65000-65536", "1-2-3"} {
		if got, err := ParseRange(s); err == nil {
			t.Errorf("ParseRange(%q) = %v, want an error", s, got)
		}
	}
}

func TestRangeSplit(t *testing.T) {
	cases := []struct {
		r           PortRange
		main, timer PortRange
	}{
		{PortRange{14000, 14999}, PortRange{14000, 14799}, PortRange{14800, 14999}},
		{PortRange{20000, 21999}, PortRange{20000, 21599}, PortRange{21600, 21999}},
		{PortRange{30000, 30009}, PortRange{30000, 30007}, PortRange{30008, 30009}},
	}
	for _, c := range cases {
		if got := c.r.Split(false); got != c.main {
			t.Errorf("%v.Split(main) = %v, want %v", c.r, got, c.main)
		}
		if got := c.r.Split(true); got != c.timer {
			t.Errorf("%v.Split(timer) = %v, want %v", c.r, got, c.timer)
		}
	}
}

func TestAllocatorSkipsBusyAndNeverRepeats(t *testing.T) {
	busy := map[int]bool{100: true, 102: true}
	a := NewAllocator(PortRange{100, 104}, func(p int) bool { return !busy[p] })

	var got []int
	for i := 0; i < 3; i++ {
		p, err := a.Alloc()
		if err != nil {
			t.Fatalf("Alloc %d: %v", i, err)
		}
		got = append(got, p)
	}
	if got[0] != 101 || got[1] != 103 || got[2] != 104 {
		t.Errorf("allocated %v, want [101 103 104]", got)
	}
	if a.Allocated() != 3 {
		t.Errorf("Allocated() = %d, want 3", a.Allocated())
	}

	// A busy port that becomes free later is never handed out: the allocator only moves forward.
	busy = map[int]bool{}
	if p, err := a.Alloc(); err == nil {
		t.Errorf("Alloc after the end = %d, want an error", p)
	} else if !strings.Contains(err.Error(), "port range exhausted, widen SRS_INTEG_PORTS") {
		t.Errorf("error %q, want it to say the range is exhausted", err)
	}
}

func TestAllocatorConcurrent(t *testing.T) {
	a := NewAllocator(PortRange{1000, 1999}, func(int) bool { return true })
	type result struct {
		port int
		err  error
	}
	ch := make(chan result, 1000)
	for i := 0; i < 10; i++ {
		go func() {
			for j := 0; j < 100; j++ {
				p, err := a.Alloc()
				ch <- result{p, err}
			}
		}()
	}
	seen := map[int]bool{}
	for i := 0; i < 1000; i++ {
		r := <-ch
		p := r.port
		if r.err != nil {
			t.Fatal(r.err)
		}
		if seen[p] {
			t.Fatalf("port %d handed out twice", p)
		}
		seen[p] = true
	}
}

func TestPortFree(t *testing.T) {
	tcp, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer tcp.Close()
	if PortFree(tcp.Addr().(*net.TCPAddr).Port) {
		t.Error("a port with a TCP listener is reported free")
	}

	udp, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := udp.LocalAddr().(*net.UDPAddr).Port
	if PortFree(port) {
		t.Error("a port with a UDP listener is reported free")
	}
	udp.Close()
	if !PortFree(port) {
		t.Errorf("port %d is reported busy after its listener closed", port)
	}
}
