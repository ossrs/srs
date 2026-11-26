// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"encoding/binary"
	"errors"
	"net"
	"os"
	"reflect"
	"slices"
	"testing"
	"time"
)

// impairPeers is a relay between a client socket and a server socket on
// loopback. Each packet carries its number in its first 4 bytes.
type impairPeers struct {
	t      *testing.T
	relay  *Impair
	client *net.UDPConn
	server *net.UDPConn
	// to is the client address the server answers, as the last packet
	// from the relay showed it.
	to *net.UDPAddr
}

func newImpairPeers(t *testing.T, seed int64) *impairPeers {
	t.Helper()
	server, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { server.Close() })
	relay, err := NewImpair(server.LocalAddr().String(), seed)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { relay.Close() })
	client, err := net.DialUDP("udp4", nil, relay.Addr())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { client.Close() })
	return &impairPeers{t: t, relay: relay, client: client, server: server}
}

func impairPacket(n int) []byte {
	b := make([]byte, 16)
	binary.BigEndian.PutUint32(b, uint32(n))
	return b
}

// up sends packets first to first+count-1 from the client.
func (p *impairPeers) up(first, count int) {
	p.t.Helper()
	for i := first; i < first+count; i++ {
		if _, err := p.client.Write(impairPacket(i)); err != nil {
			p.t.Fatal(err)
		}
	}
}

// down sends packets first to first+count-1 from the server to the client,
// through the relay.
func (p *impairPeers) down(first, count int) {
	p.t.Helper()
	if p.to == nil {
		p.t.Fatal("the server has no client address yet")
	}
	for i := first; i < first+count; i++ {
		if _, err := p.server.WriteToUDP(impairPacket(i), p.to); err != nil {
			p.t.Fatal(err)
		}
	}
}

type impairGot struct {
	n    int
	from *net.UDPAddr
	at   time.Time
}

// readServer reads what comes to the server until nothing comes for wait.
func (p *impairPeers) readServer(wait time.Duration) []impairGot {
	p.t.Helper()
	got := impairRead(p.t, p.server, wait)
	if len(got) > 0 {
		p.to = got[len(got)-1].from
	}
	return got
}

func (p *impairPeers) readClient(wait time.Duration) []impairGot {
	p.t.Helper()
	return impairRead(p.t, p.client, wait)
}

func impairRead(t *testing.T, c *net.UDPConn, wait time.Duration) []impairGot {
	t.Helper()
	var got []impairGot
	b := make([]byte, 64)
	for {
		c.SetReadDeadline(time.Now().Add(wait))
		n, from, err := c.ReadFromUDP(b)
		if errors.Is(err, os.ErrDeadlineExceeded) {
			return got
		}
		if err != nil {
			t.Fatal(err)
		}
		if n < 4 {
			t.Fatalf("a packet of %v bytes", n)
		}
		got = append(got, impairGot{int(binary.BigEndian.Uint32(b)), from, time.Now()})
	}
}

func impairNumbers(got []impairGot) []int {
	var n []int
	for _, g := range got {
		n = append(n, g.n)
	}
	return n
}

func impairRange(first, count int) []int {
	var n []int
	for i := first; i < first+count; i++ {
		n = append(n, i)
	}
	return n
}

// TestImpair_PassesBothWays checks a relay with no impairment: every packet
// once and in order, each way, and the counters of each direction.
func TestImpair_PassesBothWays(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 1)
	p.up(0, 20)
	if got := impairNumbers(p.readServer(50 * time.Millisecond)); !reflect.DeepEqual(got, impairRange(0, 20)) {
		t.Fatalf("server got %v", got)
	}
	p.down(100, 10)
	if got := impairNumbers(p.readClient(50 * time.Millisecond)); !reflect.DeepEqual(got, impairRange(100, 10)) {
		t.Fatalf("client got %v", got)
	}
	if c := p.relay.Counters(Up); c != (ImpairCounters{Received: 20, Sent: 20}) {
		t.Errorf("up %+v", c)
	}
	if c := p.relay.Counters(Down); c != (ImpairCounters{Received: 10, Sent: 10}) {
		t.Errorf("down %+v", c)
	}
}

// TestImpair_LossIsSeeded checks that a loss of 30% loses about that many,
// that the same seed loses the same packets, and that the other direction
// keeps every packet.
func TestImpair_LossIsSeeded(t *testing.T) {
	t.Parallel()
	lost := func() []int {
		p := newImpairPeers(t, 42)
		p.relay.Set(Up, Shape{Loss: 30})
		p.up(0, 200)
		got := impairNumbers(p.readServer(50 * time.Millisecond))
		c := p.relay.Counters(Up)
		if c.Received != 200 || c.Lost+c.Sent != 200 || c.Sent != len(got) {
			t.Errorf("up %+v, server got %v", c, len(got))
		}
		if c.Lost < 40 || c.Lost > 80 {
			t.Errorf("lost %v of 200 at 30%%", c.Lost)
		}
		p.down(0, 50)
		if got := p.readClient(50 * time.Millisecond); len(got) != 50 {
			t.Errorf("client got %v of 50, with no loss down", len(got))
		}
		seen := map[int]bool{}
		for _, n := range got {
			seen[n] = true
		}
		var l []int
		for i := 0; i < 200; i++ {
			if !seen[i] {
				l = append(l, i)
			}
		}
		return l
	}
	if a, b := lost(), lost(); !reflect.DeepEqual(a, b) {
		t.Errorf("seed 42 lost %v, then %v", a, b)
	}
}

// TestImpair_Delay checks that every packet waits the delay and keeps its
// order, and that jitter spreads the delay within its bounds.
func TestImpair_Delay(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 1)
	p.relay.Set(Up, Shape{Delay: 60 * time.Millisecond})
	sent := time.Now()
	p.up(0, 5)
	got := p.readServer(100 * time.Millisecond)
	if !reflect.DeepEqual(impairNumbers(got), impairRange(0, 5)) {
		t.Fatalf("server got %v", impairNumbers(got))
	}
	if d := got[0].at.Sub(sent); d < 60*time.Millisecond || d > 150*time.Millisecond {
		t.Errorf("the first packet came after %v, want the 60ms delay", d)
	}

	p.relay.Set(Up, Shape{Delay: 40 * time.Millisecond, Jitter: 30 * time.Millisecond})
	sent = time.Now()
	p.up(10, 40)
	got = p.readServer(100 * time.Millisecond)
	if len(got) != 40 {
		t.Fatalf("server got %v of 40", len(got))
	}
	early, late := time.Hour, time.Duration(0)
	for _, g := range got {
		d := g.at.Sub(sent)
		early, late = min(early, d), max(late, d)
	}
	if early < 10*time.Millisecond || late > 120*time.Millisecond || late-early < 15*time.Millisecond {
		t.Errorf("delays from %v to %v, want 40±30ms and spread", early, late)
	}
}

// TestImpair_Reorder checks that a held packet goes after the next one:
// with 100%, every pair swaps; and that a last held packet is sent alone.
func TestImpair_Reorder(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 1)
	p.relay.Set(Up, Shape{Reorder: 100})
	p.up(0, 5)
	got := impairNumbers(p.readServer(100 * time.Millisecond))
	if want := []int{1, 0, 3, 2, 4}; !reflect.DeepEqual(got, want) {
		t.Errorf("server got %v, want %v", got, want)
	}
	if c := p.relay.Counters(Up); c.Reordered != 3 || c.Sent != 5 {
		t.Errorf("up %+v, want 3 held and 5 sent", c)
	}
}

// TestImpair_Duplicate checks that each packet comes twice at 100%.
func TestImpair_Duplicate(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 1)
	p.up(0, 1)
	p.readServer(50 * time.Millisecond)
	p.relay.Set(Down, Shape{Duplicate: 100})
	p.down(0, 3)
	got := impairNumbers(p.readClient(50 * time.Millisecond))
	if want := []int{0, 0, 1, 1, 2, 2}; !reflect.DeepEqual(got, want) {
		t.Errorf("client got %v, want %v", got, want)
	}
	if c := p.relay.Counters(Down); c != (ImpairCounters{Received: 3, Sent: 6, Duplicated: 3}) {
		t.Errorf("down %+v", c)
	}
}

// TestImpair_Blackhole checks that a blackhole drops each way until it is
// off, as a dead link does, and counts apart from the loss.
func TestImpair_Blackhole(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 1)
	p.up(0, 1)
	p.readServer(50 * time.Millisecond)

	p.relay.Blackhole(true)
	p.up(1, 5)
	p.down(1, 5)
	if got := p.readServer(50 * time.Millisecond); len(got) != 0 {
		t.Errorf("server got %v in a blackhole", impairNumbers(got))
	}
	if got := p.readClient(10 * time.Millisecond); len(got) != 0 {
		t.Errorf("client got %v in a blackhole", impairNumbers(got))
	}
	p.relay.Blackhole(false)
	p.up(6, 2)
	if got := impairNumbers(p.readServer(50 * time.Millisecond)); !reflect.DeepEqual(got, []int{6, 7}) {
		t.Errorf("server got %v after the blackhole", got)
	}
	if c := p.relay.Counters(Up); c != (ImpairCounters{Received: 8, Sent: 3, Blackholed: 5}) {
		t.Errorf("up %+v", c)
	}
	if c := p.relay.Counters(Down); c != (ImpairCounters{Received: 5, Blackholed: 5}) {
		t.Errorf("down %+v", c)
	}
}

// TestImpair_PortChange checks that after Rebind the server sees the client
// from a new port, its answers to the new port reach the client, and those
// to the old port still do, as late packets of a moved client.
func TestImpair_PortChange(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 1)
	p.up(0, 1)
	first := p.readServer(50 * time.Millisecond)
	if len(first) != 1 {
		t.Fatalf("server got %v", impairNumbers(first))
	}
	old := first[0].from
	if old.Port != p.relay.UpstreamAddr().Port {
		t.Errorf("server saw port %v, the relay says %v", old.Port, p.relay.UpstreamAddr().Port)
	}

	if err := p.relay.Rebind(); err != nil {
		t.Fatal(err)
	}
	p.up(1, 1)
	second := p.readServer(50 * time.Millisecond)
	if len(second) != 1 || second[0].from.Port == old.Port || second[0].from.Port != p.relay.UpstreamAddr().Port {
		t.Fatalf("server got %v after Rebind, from %v, want a new port than %v", impairNumbers(second), second, old.Port)
	}
	p.down(10, 1)
	p.to = old
	p.down(11, 1)
	// Each socket reads on its own, so the two may come in either order.
	got := impairNumbers(p.readClient(50 * time.Millisecond))
	slices.Sort(got)
	if !reflect.DeepEqual(got, []int{10, 11}) {
		t.Errorf("client got %v from the new and the old port", got)
	}
}

// TestImpair_Close checks that a closed relay forwards nothing and a
// second Close is harmless.
func TestImpair_Close(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 1)
	p.relay.Set(Up, Shape{Delay: 30 * time.Millisecond})
	p.up(0, 3)
	time.Sleep(5 * time.Millisecond)
	if err := p.relay.Close(); err != nil {
		t.Fatal(err)
	}
	if got := p.readServer(60 * time.Millisecond); len(got) != 0 {
		t.Errorf("server got %v after Close", impairNumbers(got))
	}
	if err := p.relay.Close(); err != nil {
		t.Errorf("second Close: %v", err)
	}
	if p.relay.Rebind() == nil {
		t.Error("Rebind after Close")
	}
}

// impairSRTPacket is a packet of the SRT header's kinds, numbered n in the
// rest of its first word: a data packet, a retransmitted one (bit R of the
// second word), or a control packet (the first bit).
func impairSRTPacket(n int, kind string) []byte {
	b := make([]byte, 16)
	switch kind {
	case "control":
		binary.BigEndian.PutUint32(b, uint32(n)|1<<31)
	case "retrans":
		binary.BigEndian.PutUint32(b, uint32(n))
		binary.BigEndian.PutUint32(b[4:], 1<<26)
	default:
		binary.BigEndian.PutUint32(b, uint32(n))
	}
	return b
}

// TestImpair_DataCounters checks the SRT data counters at a loss of 30%:
// the data packets and the retransmitted ones that came in and that the
// loss dropped, each matching what the server got; control packets and
// packets too short for an SRT header are not data.
func TestImpair_DataCounters(t *testing.T) {
	t.Parallel()
	p := newImpairPeers(t, 7)
	p.relay.Set(Up, Shape{Loss: 30})
	kinds := []string{"data", "retrans", "control"}
	sent := map[string]int{}
	for i := 0; i < 300; i++ {
		kind := kinds[i%3]
		sent[kind]++
		if _, err := p.client.Write(impairSRTPacket(i, kind)); err != nil {
			t.Fatal(err)
		}
	}
	// Number 999, too short for a header.
	if _, err := p.client.Write([]byte{0, 0, 0x03, 0xe7}); err != nil {
		t.Fatal(err)
	}
	got := map[string]int{}
	for _, g := range p.readServer(50 * time.Millisecond) {
		switch {
		case g.n == 999:
		case g.n&(1<<31) != 0:
			got["control"]++
		case kinds[g.n%3] == "retrans":
			got["retrans"]++
		default:
			got["data"]++
		}
	}

	d := p.relay.DataCounters(Up)
	want := ImpairData{
		Received:    sent["data"] + sent["retrans"],
		Lost:        sent["data"] + sent["retrans"] - got["data"] - got["retrans"],
		Retrans:     sent["retrans"],
		RetransLost: sent["retrans"] - got["retrans"],
	}
	if d != want {
		t.Errorf("up data %+v, want %+v (server got %v)", d, want, got)
	}
	if d.Lost == 0 || d.RetransLost == 0 || d.Lost == d.RetransLost {
		t.Errorf("up data %+v at 30%% loss, want both kinds lost", d)
	}
	if c := p.relay.Counters(Up); c.Received != 301 {
		t.Errorf("up %+v, want 301 packets in", c)
	}
	if d := p.relay.DataCounters(Down); d != (ImpairData{}) {
		t.Errorf("down data %+v, want none", d)
	}
}
