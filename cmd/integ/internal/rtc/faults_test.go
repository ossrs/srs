// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"net"
	"sync"
	"testing"
	"time"
)

// faultPair is a UDP socket wrapped in f, connected to a peer socket the
// test reads and writes.
func faultPair(t *testing.T, f *Faults) (net.Conn, *net.UDPConn) {
	t.Helper()
	peer, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	c, err := net.DialUDP("udp4", nil, peer.LocalAddr().(*net.UDPAddr))
	if err != nil {
		t.Fatal(err)
	}
	fc := f.wrap(c)
	t.Cleanup(func() {
		fc.Close()
		peer.Close()
	})
	return fc, peer
}

// readFirstBytes reads datagrams from c until it is quiet for 200ms, and
// returns the first byte of each.
func readFirstBytes(t *testing.T, c interface {
	Read([]byte) (int, error)
	SetReadDeadline(time.Time) error
}) []byte {
	t.Helper()
	var got []byte
	b := make([]byte, 1500)
	for {
		c.SetReadDeadline(time.Now().Add(200 * time.Millisecond))
		n, err := c.Read(b)
		if err != nil {
			return got
		}
		if n > 0 {
			got = append(got, b[0])
		}
	}
}

// TestFaultsDrop drops the next STUN or DTLS datagrams each way, as many
// as asked, and lets the others and the media through.
func TestFaultsDrop(t *testing.T) {
	f := &Faults{}
	c, peer := faultPair(t, f)
	f.Drop(FaultOut, FaultSTUN, 2)
	f.Drop(FaultOut, FaultDTLS, 1)
	// STUN, STUN, DTLS, RTP, STUN, DTLS: the first two STUN and the first
	// DTLS are lost.
	for _, b := range []byte{0x00, 0x01, 0x16, 0x80, 0x01, 0x17} {
		if _, err := c.Write([]byte{b, 1, 2, 3}); err != nil {
			t.Fatal(err)
		}
	}
	if got := readFirstBytes(t, peer); string(got) != string([]byte{0x80, 0x01, 0x17}) {
		t.Errorf("the peer got %x, want 800117", got)
	}
	if n := f.Dropped(FaultOut, FaultSTUN); n != 2 {
		t.Errorf("dropped %v STUN out, want 2", n)
	}
	if n := f.Dropped(FaultOut, FaultDTLS); n != 1 {
		t.Errorf("dropped %v DTLS out, want 1", n)
	}

	f.Drop(FaultIn, FaultDTLS, 1)
	f.Drop(FaultIn, FaultSTUN, 1)
	for _, b := range []byte{0x16, 0x00, 0x90, 0x16, 0x00} {
		if _, err := peer.WriteTo([]byte{b, 1, 2, 3}, c.LocalAddr()); err != nil {
			t.Fatal(err)
		}
	}
	var got []byte
	b := make([]byte, 1500)
	for range 3 {
		n, err := c.Read(b)
		if err != nil || n == 0 {
			t.Fatalf("read %v, %v", n, err)
		}
		got = append(got, b[0])
	}
	if string(got) != string([]byte{0x90, 0x16, 0x00}) {
		t.Errorf("the session got %x, want 901600", got)
	}
	if f.Dropped(FaultIn, FaultDTLS) != 1 || f.Dropped(FaultIn, FaultSTUN) != 1 {
		t.Errorf("dropped DTLS in %v, STUN in %v, want 1 and 1", f.Dropped(FaultIn, FaultDTLS), f.Dropped(FaultIn, FaultSTUN))
	}
}

// TestFaultsLoss loses every STUN and DTLS datagram at a loss of 1, each
// way, and no media.
func TestFaultsLoss(t *testing.T) {
	f := &Faults{Loss: 1}
	c, peer := faultPair(t, f)
	for _, b := range []byte{0x00, 0x16, 0x80, 0x3f, 0xbf} {
		c.Write([]byte{b, 1})
	}
	if got := readFirstBytes(t, peer); string(got) != string([]byte{0x80, 0xbf}) {
		t.Errorf("the peer got %x, want 80bf", got)
	}
	if n := f.Dropped(FaultOut, FaultSTUN) + f.Dropped(FaultOut, FaultDTLS); n != 3 {
		t.Errorf("dropped %v out, want 3", n)
	}
	for _, b := range []byte{0x01, 0x14, 0x81} {
		peer.WriteTo([]byte{b, 1}, c.LocalAddr())
	}
	b := make([]byte, 1500)
	if n, err := c.Read(b); err != nil || n == 0 || b[0] != 0x81 {
		t.Errorf("the session read %x, %v, want the RTP", b[:n], err)
	}
	if n := f.Dropped(FaultIn, FaultSTUN) + f.Dropped(FaultIn, FaultDTLS); n != 2 {
		t.Errorf("dropped %v in, want 2", n)
	}
}

// TestFaultsLose loses the STUN and DTLS datagrams Lose picks, each way,
// counts them as dropped, and never asks it about media.
func TestFaultsLose(t *testing.T) {
	var mu sync.Mutex
	var asked []byte
	f := &Faults{Lose: func(dir FaultDirection, b []byte) bool {
		mu.Lock()
		defer mu.Unlock()
		if dir == FaultOut {
			asked = append(asked, b[0])
		}
		return b[1] == 0xff
	}}
	c, peer := faultPair(t, f)
	// STUN picked, STUN, DTLS picked, RTP marked like a pick, DTLS.
	for _, b := range [][]byte{{0x00, 0xff}, {0x01, 1}, {0x16, 0xff}, {0x80, 0xff}, {0x17, 1}} {
		if _, err := c.Write(b); err != nil {
			t.Fatal(err)
		}
	}
	if got := readFirstBytes(t, peer); string(got) != string([]byte{0x01, 0x80, 0x17}) {
		t.Errorf("the peer got %x, want 018017", got)
	}
	if f.Dropped(FaultOut, FaultSTUN) != 1 || f.Dropped(FaultOut, FaultDTLS) != 1 {
		t.Errorf("dropped STUN out %v, DTLS out %v, want 1 and 1", f.Dropped(FaultOut, FaultSTUN), f.Dropped(FaultOut, FaultDTLS))
	}
	mu.Lock()
	if string(asked) != string([]byte{0x00, 0x01, 0x16, 0x17}) {
		t.Errorf("Lose was asked about %x going out, want 00011617", asked)
	}
	mu.Unlock()

	peer.WriteTo([]byte{0x16, 0xff}, c.LocalAddr())
	peer.WriteTo([]byte{0x16, 1}, c.LocalAddr())
	b := make([]byte, 1500)
	if n, err := c.Read(b); err != nil || n != 2 || b[1] != 1 {
		t.Errorf("the session read %x, %v, want the DTLS not picked", b[:n], err)
	}
	if n := f.Dropped(FaultIn, FaultDTLS); n != 1 {
		t.Errorf("dropped %v DTLS in, want 1", n)
	}
}

// TestFaultsSeen tells Seen of each datagram that is not lost, media too,
// each way in order, and of none that is lost.
func TestFaultsSeen(t *testing.T) {
	var mu sync.Mutex
	var out, in []byte
	f := &Faults{
		Lose: func(dir FaultDirection, b []byte) bool { return b[1] == 0xff },
		Seen: func(dir FaultDirection, b []byte) {
			mu.Lock()
			defer mu.Unlock()
			if dir == FaultOut {
				out = append(out, b[0])
			} else {
				in = append(in, b[0])
			}
		},
	}
	c, peer := faultPair(t, f)
	// STUN, DTLS lost, RTP, DTLS.
	for _, b := range [][]byte{{0x00, 1}, {0x16, 0xff}, {0x80, 1}, {0x17, 1}} {
		if _, err := c.Write(b); err != nil {
			t.Fatal(err)
		}
	}
	if got := readFirstBytes(t, peer); string(got) != string([]byte{0x00, 0x80, 0x17}) {
		t.Errorf("the peer got %x, want 008017", got)
	}

	// RTP, DTLS lost, RTCP, STUN.
	for _, b := range [][]byte{{0x80, 1}, {0x16, 0xff}, {0x81, 1}, {0x01, 1}} {
		peer.WriteTo(b, c.LocalAddr())
	}
	b := make([]byte, 1500)
	for range 3 {
		if _, err := c.Read(b); err != nil {
			t.Fatal(err)
		}
	}

	mu.Lock()
	defer mu.Unlock()
	if string(out) != string([]byte{0x00, 0x80, 0x17}) {
		t.Errorf("Seen got %x going out, want 008017", out)
	}
	if string(in) != string([]byte{0x80, 0x81, 0x01}) {
		t.Errorf("Seen got %x coming in, want 808101", in)
	}
}

// TestFaultsDelay delays every datagram by the one-way delay each way, in
// order.
func TestFaultsDelay(t *testing.T) {
	const delay = 80 * time.Millisecond
	f := &Faults{Delay: delay}
	c, peer := faultPair(t, f)
	start := time.Now()
	for _, b := range []byte{0x00, 0x16, 0x80} {
		if _, err := c.Write([]byte{b}); err != nil {
			t.Fatal(err)
		}
	}
	b := make([]byte, 1500)
	var got []byte
	for range 3 {
		peer.SetReadDeadline(time.Now().Add(time.Second))
		n, err := peer.Read(b)
		if err != nil || n == 0 {
			t.Fatalf("read %v, %v", n, err)
		}
		got = append(got, b[0])
	}
	if d := time.Since(start); d < delay {
		t.Errorf("the peer got them after %v, want at least %v", d, delay)
	}
	if string(got) != string([]byte{0x00, 0x16, 0x80}) {
		t.Errorf("the peer got %x, want 001680", got)
	}

	start = time.Now()
	peer.WriteTo([]byte{0x16}, c.LocalAddr())
	if n, err := c.Read(b); err != nil || n != 1 || b[0] != 0x16 {
		t.Fatalf("read %x, %v", b[:n], err)
	}
	if d := time.Since(start); d < delay {
		t.Errorf("the session got it after %v, want at least %v", d, delay)
	}
}

// TestFaultsClose ends a read on the wrapped socket once it closes.
func TestFaultsClose(t *testing.T) {
	c, _ := faultPair(t, &Faults{Delay: 10 * time.Millisecond})
	done := make(chan error, 1)
	go func() {
		_, err := c.Read(make([]byte, 1500))
		done <- err
	}()
	c.Close()
	select {
	case err := <-done:
		if err == nil {
			t.Error("the read ended without an error")
		}
	case <-time.After(time.Second):
		t.Fatal("the read didn't end")
	}
}
