// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"net"
	"testing"
	"time"

	"github.com/pion/transport/v4/stdnet"
)

// TestRebinder checks a session's socket as a server sees it: packets from
// the first port, from a new one after Rebind, and from the first again
// after Use(0); no STUN while it is held; and the media the server sends
// back counted on the port it comes to, and read by pion from either.
func TestRebinder(t *testing.T) {
	server, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	server.SetDeadline(time.Now().Add(time.Second))

	n, err := stdnet.NewNet()
	if err != nil {
		t.Fatal(err)
	}
	r := &Rebinder{}
	if _, err := r.Rebind(); err == nil {
		t.Error("Rebind before the session has a socket")
	}
	conn, err := (&rebindNet{Net: n, r: r}).ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	if _, err := (&rebindNet{Net: n, r: r}).ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)}); err == nil {
		t.Error("a second socket for one Rebinder")
	}

	rtp, stun := []byte{0x80, 96, 0, 1}, []byte{0, 1, 0, 0}
	// send writes p from the session and returns the port the server saw
	// it from; the server echoes it back there.
	send := func(p []byte) int {
		t.Helper()
		if _, err := conn.WriteTo(p, server.LocalAddr()); err != nil {
			t.Fatal(err)
		}
		b := make([]byte, 16)
		_, from, err := server.ReadFromUDP(b)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := server.WriteToUDP(b[:len(p)], from); err != nil {
			t.Fatal(err)
		}
		got := make([]byte, 16)
		if _, _, err := conn.ReadFrom(got); err != nil {
			t.Fatal(err)
		}
		return from.Port
	}

	first := send(rtp)
	if want := conn.LocalAddr().(*net.UDPAddr).Port; first != want {
		t.Errorf("first port %v, want the socket's %v", first, want)
	}
	if i, err := r.Rebind(); err != nil || i != 1 {
		t.Fatalf("Rebind %v %v, want socket 1", i, err)
	}
	second := send(rtp)
	if second == first {
		t.Errorf("port %v after Rebind, want another than %v", second, first)
	}
	if conn.LocalAddr().(*net.UDPAddr).Port != first {
		t.Errorf("local address %v after Rebind, want the first port %v, as pion sees it", conn.LocalAddr(), first)
	}
	send(stun)
	if r.Media(0) != 1 || r.Media(1) != 1 {
		t.Errorf("media %v and %v, want 1 on each port: STUN isn't media", r.Media(0), r.Media(1))
	}

	r.HoldSTUN(true)
	if _, err := conn.WriteTo(stun, server.LocalAddr()); err != nil {
		t.Fatal(err)
	}
	r.HoldSTUN(false)
	if send(rtp) != second {
		t.Error("the packet after a held STUN came from another port")
	}
	if r.Media(1) != 2 {
		t.Errorf("media on the second port %v, want 2: the held STUN reached the server", r.Media(1))
	}

	if err := r.Use(0); err != nil {
		t.Fatal(err)
	}
	if p := send(rtp); p != first {
		t.Errorf("port %v after Use(0), want the first %v", p, first)
	}
	if r.Use(2) == nil {
		t.Error("Use of a socket that doesn't exist")
	}
}
