// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"encoding/binary"
	"net"
	"slices"
	"testing"
	"time"

	"github.com/pion/transport/v4/stdnet"
)

// TestDropper checks what a server and the session see of a socket with a
// Dropper: the next packets of the stream it drops are lost, each resend of
// them until it lost them as many times as asked, and nothing else, not
// another stream, RTCP or STUN; the same for packets the session receives.
func TestDropper(t *testing.T) {
	server, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()

	n, err := stdnet.NewNet()
	if err != nil {
		t.Fatal(err)
	}
	d := &Dropper{}
	conn, err := (&dropNet{Net: n, d: d}).ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()

	const a, b = 0xa, 0xb
	rtp := func(ssrc uint32, seq uint16) []byte {
		p := make([]byte, 12)
		p[0], p[1] = 0x80, 96
		binary.BigEndian.PutUint16(p[2:], seq)
		binary.BigEndian.PutUint32(p[8:], ssrc)
		return p
	}
	// An RTCP receiver report, whose reportee is at the offset of an RTP
	// packet's SSRC, and a STUN binding request.
	rtcp := []byte{0x81, 201, 0, 7, 0, 0, 0, 1, 0, 0, 0, a, 0, 0, 0, 0}
	stun := []byte{0, 1, 0, 0, 0x21, 0x12, 0xa4, 0x42, 0, 0, 0, 0}

	// sent sends each packet from the session, then a last one, and returns
	// what the server got before the last.
	sent := func(pkts ...[]byte) [][]byte {
		t.Helper()
		last := []byte{0xff}
		for _, p := range append(pkts, last) {
			if _, err := conn.WriteTo(p, server.LocalAddr()); err != nil {
				t.Fatal(err)
			}
		}
		server.SetDeadline(time.Now().Add(time.Second))
		var got [][]byte
		for {
			buf := make([]byte, 64)
			n, _, err := server.ReadFromUDP(buf)
			if err != nil {
				t.Fatal(err)
			}
			if slices.Equal(buf[:n], last) {
				return got
			}
			got = append(got, buf[:n])
		}
	}
	check := func(what string, got [][]byte, want ...[]byte) {
		t.Helper()
		if !slices.EqualFunc(got, want, slices.Equal[[]byte]) {
			t.Errorf("%v: got %x, want %x", what, got, want)
		}
	}

	check("no drop", sent(rtp(a, 1), rtcp, stun), rtp(a, 1), rtcp, stun)

	d.Drop(a, 2, 1)
	check("drop 2 of a", sent(rtp(b, 1), rtcp, stun, rtp(a, 2), rtp(a, 3), rtp(a, 2), rtp(a, 4)),
		rtp(b, 1), rtcp, stun, rtp(a, 2), rtp(a, 4))
	if got := d.Dropped(a); !slices.Equal(got, []uint16{2, 3}) {
		t.Errorf("dropped %v, want [2 3]", got)
	}

	d.Drop(a, 1, 2)
	check("drop 1 of a twice", sent(rtp(a, 10), rtp(a, 11), rtp(a, 10), rtp(a, 10)), rtp(a, 11), rtp(a, 10))
	if got := d.Dropped(a); !slices.Equal(got, []uint16{2, 3, 10, 10}) {
		t.Errorf("dropped %v, want [2 3 10 10]", got)
	}
	if got := d.Dropped(b); len(got) != 0 {
		t.Errorf("dropped %v of b, want none", got)
	}

	d.Drop(b, 1, 1)
	for _, p := range [][]byte{rtp(b, 20), rtp(b, 21)} {
		if _, err := server.WriteToUDP(p, conn.LocalAddr().(*net.UDPAddr)); err != nil {
			t.Fatal(err)
		}
	}
	conn.SetDeadline(time.Now().Add(time.Second))
	buf := make([]byte, 64)
	nr, _, err := conn.ReadFrom(buf)
	if err != nil {
		t.Fatal(err)
	}
	check("received", [][]byte{buf[:nr]}, rtp(b, 21))
	if got := d.Dropped(b); !slices.Equal(got, []uint16{20}) {
		t.Errorf("dropped %v of b, want [20]", got)
	}
}
