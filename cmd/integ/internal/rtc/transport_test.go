// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"bytes"
	"context"
	"errors"
	"io"
	"net"
	"net/netip"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/pion/stun/v4"

	"srs-integ/internal/report"
)

// TestBindingRequestControlled checks the request of an ICE-controlled
// agent: its role attribute, and no nomination, which only the controlling
// agent sends.
func TestBindingRequestControlled(t *testing.T) {
	m, err := bindingRequest("srvu", "cliu", "srvpwd", true)
	if err != nil {
		t.Fatal(err)
	}
	d := &stun.Message{Raw: append([]byte(nil), m.Raw...)}
	if err := d.Decode(); err != nil {
		t.Fatal(err)
	}
	if !d.Contains(stun.AttrICEControlled) || d.Contains(stun.AttrICEControlling) || d.Contains(stun.AttrUseCandidate) {
		t.Errorf("attributes %v, want ICE-CONTROLLED without ICE-CONTROLLING and USE-CANDIDATE", d)
	}
	if err := stun.NewShortTermIntegrity("srvpwd").Check(d); err != nil {
		t.Errorf("integrity: %v", err)
	}
}

// TestFramedConn checks RFC 4571 framing: each packet after its length in 2
// bytes, one packet per read, and a packet longer than the buffer an error.
func TestFramedConn(t *testing.T) {
	a, b := net.Pipe()
	defer a.Close()
	defer b.Close()
	fa := newFramedConn(a)
	a.SetDeadline(time.Now().Add(time.Second))
	b.SetDeadline(time.Now().Add(time.Second))

	go func() {
		fa.Write([]byte{1, 2, 3})
		fa.Write([]byte{4})
	}()
	got := make([]byte, 8)
	if _, err := io.ReadFull(b, got); err != nil {
		t.Fatal(err)
	}
	if want := []byte{0, 3, 1, 2, 3, 0, 1, 4}; !bytes.Equal(got, want) {
		t.Errorf("written %v, want %v", got, want)
	}

	go b.Write([]byte{0, 2, 9, 8, 0, 3, 7, 6, 5, 0, 4, 1, 1, 1, 1})
	buf := make([]byte, 3)
	for _, want := range [][]byte{{9, 8}, {7, 6, 5}} {
		n, err := fa.Read(buf)
		if err != nil || !bytes.Equal(buf[:n], want) {
			t.Errorf("read %v %v, want %v", buf[:n], err, want)
		}
	}
	if _, err := fa.Read(buf); err == nil {
		t.Error("a packet of 4 bytes fit a buffer of 3")
	}
}

// TestBind checks a binding of a session: over UDP and TCP, the server sees
// the client's address and the username of the session, as a controlling or
// a controlled agent.
func TestBind(t *testing.T) {
	for _, tcp := range []bool{false, true} {
		for _, controlled := range []bool{false, true} {
			srv := newFakeRawOn(t, false, tcp)
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			offer, err := Offer(true, []string{"H264"}, []string{"opus"})
			if err != nil {
				t.Fatal(err)
			}
			s, err := Post(ctx, srv.URL+"/rtc/v1/whip/?app=live&stream=s", offer, nil)
			if err != nil {
				t.Fatal(err)
			}
			b, err := Bind(ctx, s.Offer, s.Answer, &Options{TCP: tcp, Controlled: controlled})
			if err != nil {
				t.Fatalf("TCP=%v controlled=%v: %v", tcp, controlled, err)
			}
			if !b.Mapped.IsValid() || b.Mapped != b.Local {
				t.Errorf("TCP=%v: mapped %v, local %v", tcp, b.Mapped, b.Local)
			}
			if got := srv.network(); got != map[bool]string{false: "udp", true: "tcp"}[tcp] {
				t.Errorf("the binding came over %v", got)
			}
			if u := srv.username(); !strings.HasPrefix(u, "srvu1234:") {
				t.Errorf("STUN username %q", u)
			}
			if srv.isControlled() != controlled {
				t.Errorf("controlled %v, want %v", srv.isControlled(), controlled)
			}
		}
	}
}

// TestBindLocalPort checks two bindings over UDP from the same local port,
// the second after the first closed its socket.
func TestBindLocalPort(t *testing.T) {
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	port := uint16(udp.LocalAddr().(*net.UDPAddr).Port)
	udp.Close()

	srv := newFakeRawOn(t, false, false)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	for i := 0; i < 2; i++ {
		offer, err := Offer(true, []string{"H264"}, []string{"opus"})
		if err != nil {
			t.Fatal(err)
		}
		s, err := Post(ctx, srv.URL+"/rtc/v1/whip/?app=live&stream=s", offer, nil)
		if err != nil {
			t.Fatal(err)
		}
		b, err := Bind(ctx, s.Offer, s.Answer, &Options{LocalPort: port})
		if err != nil {
			t.Fatalf("binding %v: %v", i, err)
		}
		if b.Local.Port() != port || b.Mapped != b.Local {
			t.Errorf("binding %v: local %v, mapped %v, want port %v", i, b.Local, b.Mapped, port)
		}
	}
}

// TestBindClosed checks a TCP server that closes the connection without an
// answer, and a UDP server that never answers.
func TestBindClosed(t *testing.T) {
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	go func() {
		c, err := ln.Accept()
		if err != nil {
			return
		}
		io.ReadFull(c, make([]byte, 2))
		c.Close()
	}()
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer udp.Close()
	answer := strings.ReplaceAll(srsAnswer, " 14101 ", " "+portOf(ln.Addr())+" ")
	answer = strings.ReplaceAll(answer, " 14100 ", " "+portOf(udp.LocalAddr())+" ")
	offer, err := Offer(true, []string{"H264"}, []string{"opus"})
	if err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := Bind(ctx, offer, answer, &Options{TCP: true}); !errors.Is(err, ErrClosed) {
		t.Errorf("TCP: %v, want ErrClosed", err)
	}
	short, cancel2 := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel2()
	if _, err := Bind(short, offer, answer, nil); !errors.Is(err, context.DeadlineExceeded) {
		t.Errorf("UDP: %v, want the deadline", err)
	}
}

// TestIntrude checks the binding requests go on until ctx ends, every 50ms,
// and the media packets that come back are counted, not the STUN.
func TestIntrude(t *testing.T) {
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer udp.Close()
	requests := make(chan int, 1)
	go func() {
		n := 0
		defer func() { requests <- n }()
		b := make([]byte, 1500)
		for {
			udp.SetReadDeadline(time.Now().Add(300 * time.Millisecond))
			_, from, err := udp.ReadFromUDP(b)
			if err != nil {
				return
			}
			n++
			udp.WriteToUDP([]byte{1, 1, 0, 0}, from)
			udp.WriteToUDP([]byte{0x80, 96, 0, 1}, from)
		}
	}()
	answer := strings.ReplaceAll(srsAnswer, " 14100 ", " "+portOf(udp.LocalAddr())+" ")
	offer, err := Offer(true, []string{"H264"}, []string{"opus"})
	if err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 180*time.Millisecond)
	defer cancel()
	media, err := Intrude(ctx, offer, answer, nil)
	if err != nil {
		t.Fatal(err)
	}
	n := <-requests
	if n < 3 || n > 5 {
		t.Errorf("%v binding requests in 180ms, want one every 50ms", n)
	}
	if media != n {
		t.Errorf("%v media packets, want one for each of the %v requests", media, n)
	}
}

// portOf is the port of a socket's address.
func portOf(a net.Addr) string {
	return strconv.Itoa(int(netip.MustParseAddrPort(a.String()).Port()))
}

// TestRawAbort checks Abort closes a TCP session's connection.
func TestRawAbort(t *testing.T) {
	srv := newFakeRawOn(t, true, true)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	p, err := RawPublish(ctx, srv.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{TCP: true}, true)
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	srv.waitReady(t)
	p.Abort()
	srv.mu.Lock()
	m := srv.mux
	srv.mu.Unlock()
	select {
	case <-m.done:
	case <-time.After(time.Second):
		t.Error("the server's connection is still open after Abort")
	}
}

// TestWithSetup checks the offer a session posts with a DTLS setup.
func TestWithSetup(t *testing.T) {
	offer := "v=0\r\na=setup:actpass\r\nm=audio\r\na=setup:actpass\r\n"
	if got := withSetup(offer, ""); got != offer {
		t.Errorf("no setup changed the offer: %q", got)
	}
	if got := withSetup(offer, "passive"); strings.Contains(got, "actpass") || strings.Count(got, "a=setup:passive") != 2 {
		t.Errorf("setup passive: %q", got)
	}
}

// TestPublishPlaySetup publishes and plays through the fake server with
// each DTLS setup in the offer: it gets the offer's setup, answers the other
// role, and the media flows.
func TestPublishPlaySetup(t *testing.T) {
	for _, setup := range []string{"active", "passive"} {
		t.Run(setup, func(t *testing.T) {
			sfu := newFakeSFU(t)
			publishPlay(t, sfu, &Options{Setup: setup})
			for _, r := range sfu.requests() {
				if r.Method == "POST" && !strings.Contains(r.Body, "a=setup:"+setup) {
					t.Errorf("posted offer has no setup %v", setup)
				}
			}
		})
	}
}

// TestPublishPlayTCP publishes and plays through a fake server that takes
// ICE only over TCP: the sessions select a TCP pair, and the media flows.
func TestPublishPlayTCP(t *testing.T) {
	sfu := newFakeSFUTCP(t)
	pub, player := publishPlay(t, sfu, &Options{TCP: true})
	for _, s := range []*Session{&pub.Session, &player.Session} {
		if got := s.Transport(); got != "tcp" {
			t.Errorf("transport %q, want tcp", got)
		}
	}
	if !strings.Contains(pub.Offer, "a=setup:actpass") {
		t.Error("an offer without a setup option isn't actpass")
	}
}

// TestTransportUDP checks a UDP session's selected pair.
func TestTransportUDP(t *testing.T) {
	sfu := newFakeSFU(t)
	pub, player := publishPlay(t, sfu, nil)
	for _, s := range []*Session{&pub.Session, &player.Session} {
		if got := s.Transport(); got != "udp" {
			t.Errorf("transport %q, want udp", got)
		}
	}
}

// publishPlay connects a player and a publisher to the fake server with
// opts, sends 2s and checks the player got them.
func publishPlay(t *testing.T, sfu *fakeSFU, opts *Options) (*Publisher, *Player) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", opts)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { player.Close() })
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", opts)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { pub.Close() })
	if err := pub.Send(ctx, readSamples(t, 2*time.Second), burstPacer(opts != nil && opts.TCP)); err != nil {
		t.Fatal(err)
	}
	r, err := player.Until(ctx, report.Until{VideoHeader: true, Keyframes: 2, VideoFrames: 50, AudioFrames: 100})
	if err != nil {
		t.Fatal(err)
	}
	checkTwoSeconds(t, r)
	return pub, player
}

// TestBindTCPWaitsForClose checks a TCP binding returns only once the server
// has closed the connection after the client: the server has let it go, so
// the session takes the next connection.
func TestBindTCPWaitsForClose(t *testing.T) {
	srv := newFakeRawOn(t, false, true)
	srv.mu.Lock()
	srv.closeDelay = 100 * time.Millisecond
	srv.mu.Unlock()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	offer, err := Offer(true, []string{"H264"}, []string{"opus"})
	if err != nil {
		t.Fatal(err)
	}
	s, err := Post(ctx, srv.URL+"/rtc/v1/whip/?app=live&stream=s", offer, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Bind(ctx, s.Offer, s.Answer, &Options{TCP: true}); err != nil {
		t.Fatal(err)
	}
	end := time.Now()
	srv.mu.Lock()
	closedAt := srv.closedAt
	srv.mu.Unlock()
	if closedAt.IsZero() || end.Before(closedAt) {
		t.Errorf("Bind returned at %v, before the server closed at %v", end, closedAt)
	}
}
