// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/pion/stun/v4"

	"srs-integ/internal/report"
)

// spedSink records what a link sends raw.
type spedSink struct {
	mu   sync.Mutex
	sent [][]byte
}

func (s *spedSink) write(b []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.sent = append(s.sent, append([]byte(nil), b...))
	return len(b), nil
}

func (s *spedSink) count() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.sent)
}

// dtlsDatagram is a fake DTLS datagram of n bytes, its first byte a
// handshake record and the rest tag.
func dtlsDatagram(n int, tag byte) []byte {
	b := bytes.Repeat([]byte{tag}, n)
	b[0] = 0x16
	return b
}

// spedMessage builds a signed binding request or response with the link's
// attributes, or with the given ones when link is nil, and decodes it as a
// peer gets it.
func spedMessage(t *testing.T, typ stun.MessageType, setters ...stun.Setter) *stun.Message {
	t.Helper()
	all := append([]stun.Setter{stun.TransactionID, typ}, setters...)
	all = append(all, stun.NewShortTermIntegrity("pwd0123456789"), stun.Fingerprint)
	m, err := stun.Build(all...)
	if err != nil {
		t.Fatal(err)
	}
	got := &stun.Message{Raw: append([]byte(nil), m.Raw...)}
	if err := got.Decode(); err != nil {
		t.Fatal(err)
	}
	return got
}

// spedAcks is the value of an ACK attribute with crcs.
func spedAcks(crcs ...uint32) stun.RawAttribute {
	v := make([]byte, 4*len(crcs))
	for i, c := range crcs {
		binary.BigEndian.PutUint32(v[4*i:], c)
	}
	return stun.RawAttribute{Type: stun.AttrDtlsInStunAck, Value: v}
}

// spedAttrs returns a message's DATA, if any, and the CRCs of its ACK, and
// whether it has each.
func spedAttrs(t *testing.T, m *stun.Message) (data []byte, hasData bool, acks []uint32, hasAck bool) {
	t.Helper()
	if v, err := m.Get(stun.AttrDtlsInStun); err == nil {
		data, hasData = v, true
	}
	if v, err := m.Get(stun.AttrDtlsInStunAck); err == nil {
		hasAck = true
		if len(v)%4 != 0 {
			t.Fatalf("an ACK of %v bytes", len(v))
		}
		for i := 0; i < len(v); i += 4 {
			acks = append(acks, binary.BigEndian.Uint32(v[i:]))
		}
	}
	return
}

// TestSpedLinkRoundRobin embeds the datagrams of a flight one per message,
// in turn, until they are ACKed, and a new flight replaces the old one.
func TestSpedLinkRoundRobin(t *testing.T) {
	sink := &spedSink{}
	l := newSpedLink(sink.write, true)
	a, b := dtlsDatagram(300, 1), dtlsDatagram(200, 2)
	l.write(a)
	l.write(b)
	if sink.count() != 2 {
		t.Errorf("%v datagrams sent raw, want 2", sink.count())
	}
	var order [][]byte
	for range 3 {
		data, ok, _, hasAck := spedAttrs(t, spedMessage(t, stun.BindingRequest, l))
		if !ok || !hasAck {
			t.Fatalf("DATA %v, ACK %v, want both", ok, hasAck)
		}
		order = append(order, data)
	}
	if !bytes.Equal(order[0], a) || !bytes.Equal(order[1], b) || !bytes.Equal(order[2], a) {
		t.Errorf("embedded %v, %v, %v bytes, want a, b, a", len(order[0]), len(order[1]), len(order[2]))
	}

	// An ACK of a leaves b alone, which is then embedded every time.
	l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(crc32.ChecksumIEEE(a))))
	for range 2 {
		if data, _, _, _ := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); !bytes.Equal(data, b) {
			t.Errorf("embedded %v bytes, want b", len(data))
		}
	}
	// An unknown CRC and a second ACK of a change nothing.
	l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(0x12345678, crc32.ChecksumIEEE(a))))
	if data, _, _, _ := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); !bytes.Equal(data, b) {
		t.Errorf("embedded %v bytes, want b", len(data))
	}

	// A datagram received, then a write: a new flight.
	l.received(dtlsDatagram(100, 9), false)
	c := dtlsDatagram(150, 3)
	l.write(c)
	for range 2 {
		if data, _, _, _ := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); !bytes.Equal(data, c) {
			t.Errorf("embedded %v bytes, want the new flight", len(data))
		}
	}
	// All ACKed: an empty flight embeds no DATA, only the ACK.
	l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(crc32.ChecksumIEEE(c))))
	if _, hasData, _, hasAck := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); hasData || !hasAck {
		t.Errorf("DATA %v, ACK %v, want only the ACK", hasData, hasAck)
	}
}

// TestSpedLinkFlightGap starts a new flight on a write long after the last
// one, as a DTLS retransmission is.
func TestSpedLinkFlightGap(t *testing.T) {
	l := newSpedLink((&spedSink{}).write, true)
	a := dtlsDatagram(100, 1)
	l.write(a)
	time.Sleep(spedFlightGap + 20*time.Millisecond)
	b := dtlsDatagram(100, 2)
	l.write(b)
	for range 2 {
		if data, _, _, _ := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); !bytes.Equal(data, b) {
			t.Errorf("embedded %v bytes, want the retransmission only", len(data))
		}
	}
}

// TestSpedLinkAcks ACKs each DTLS datagram received, embedded or raw, once,
// with at most the 4 newest, and drops non-DTLS data.
func TestSpedLinkAcks(t *testing.T) {
	l := newSpedLink((&spedSink{}).write, true)
	// The first message has an empty ACK.
	if _, _, acks, hasAck := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); !hasAck || len(acks) != 0 {
		t.Fatalf("ACK %v with %v, want an empty one", hasAck, acks)
	}
	var crcs []uint32
	for i := range 6 {
		d := dtlsDatagram(50+i, byte(i))
		crcs = append(crcs, crc32.ChecksumIEEE(d))
		if i%2 == 0 {
			l.received(d, false)
		} else {
			l.onMessage(spedMessage(t, stun.BindingSuccess, stun.RawAttribute{Type: stun.AttrDtlsInStun, Value: d}))
			select {
			case got := <-l.in:
				if !bytes.Equal(got, d) {
					t.Errorf("fed %v bytes to DTLS, want %v", len(got), len(d))
				}
			default:
				t.Errorf("the embedded datagram %v wasn't fed to DTLS", i)
			}
		}
		// The same datagram again is not ACKed twice.
		l.received(d, false)
	}
	_, _, acks, _ := spedAttrs(t, spedMessage(t, stun.BindingRequest, l))
	if fmt.Sprint(acks) != fmt.Sprint(crcs[2:]) {
		t.Errorf("ACK %x, want the 4 newest %x", acks, crcs[2:])
	}
	s := l.stats()
	if s.EmbeddedRecv != 3 || s.RawRecv != 9 {
		t.Errorf("embedded recv %v, raw recv %v, want 3 and 9", s.EmbeddedRecv, s.RawRecv)
	}

	// Data that is not DTLS is neither fed nor ACKed.
	l.onMessage(spedMessage(t, stun.BindingSuccess, stun.RawAttribute{Type: stun.AttrDtlsInStun, Value: []byte{0x80, 1, 2, 3}}))
	select {
	case <-l.in:
		t.Error("RTP fed to DTLS")
	default:
	}
	if _, _, got, _ := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); fmt.Sprint(got) != fmt.Sprint(crcs[2:]) {
		t.Errorf("ACK %x after non-DTLS data", got)
	}
}

// TestSpedLinkMalformedAck ignores an ACK whose length is not a multiple
// of 4 or that has more than 4 CRCs.
func TestSpedLinkMalformedAck(t *testing.T) {
	l := newSpedLink((&spedSink{}).write, true)
	a := dtlsDatagram(80, 1)
	l.write(a)
	crc := crc32.ChecksumIEEE(a)
	l.onMessage(spedMessage(t, stun.BindingSuccess, stun.RawAttribute{Type: stun.AttrDtlsInStunAck, Value: []byte{1, 2, 3}}))
	l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(1, 2, 3, 4, crc)))
	if data, _, _, _ := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); !bytes.Equal(data, a) {
		t.Errorf("embedded %v bytes, want the datagram still", len(data))
	}
	if st := l.state(); st != SpedConfirmed {
		t.Errorf("state %v, want confirmed", st)
	}
}

// TestSpedLinkStates walks the states: tentative, confirmed on the first
// message with an attribute, pending at handshake end, and complete on the
// last ACK; off on a first message without attributes.
func TestSpedLinkStates(t *testing.T) {
	t.Run("complete on the last ACK", func(t *testing.T) {
		l := newSpedLink((&spedSink{}).write, true)
		if l.state() != SpedTentative {
			t.Fatalf("state %v, want tentative", l.state())
		}
		hello := dtlsDatagram(120, 1)
		l.write(hello)
		l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(), stun.RawAttribute{Type: stun.AttrDtlsInStun, Value: dtlsDatagram(700, 2)}))
		if l.state() != SpedConfirmed {
			t.Fatalf("state %v, want confirmed", l.state())
		}
		last := dtlsDatagram(500, 3)
		l.write(last)
		l.handshakeDone()
		if l.state() != SpedPending {
			t.Fatalf("state %v, want pending", l.state())
		}
		// An ACK of an older flight keeps it pending.
		l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(crc32.ChecksumIEEE(hello))))
		if l.state() != SpedPending {
			t.Fatalf("state %v, want pending", l.state())
		}
		l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(crc32.ChecksumIEEE(last))))
		if l.state() != SpedComplete {
			t.Fatalf("state %v, want complete", l.state())
		}
		// Complete embeds nothing.
		if _, hasData, _, hasAck := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); hasData || hasAck {
			t.Errorf("DATA %v, ACK %v after complete", hasData, hasAck)
		}
	})
	t.Run("complete at handshake end with the flight ACKed", func(t *testing.T) {
		l := newSpedLink((&spedSink{}).write, true)
		last := dtlsDatagram(500, 3)
		l.write(last)
		l.onMessage(spedMessage(t, stun.BindingSuccess, spedAcks(crc32.ChecksumIEEE(last))))
		l.handshakeDone()
		if l.state() != SpedComplete {
			t.Fatalf("state %v, want complete", l.state())
		}
	})
	t.Run("complete on media while pending", func(t *testing.T) {
		l := newSpedLink((&spedSink{}).write, true)
		l.mediaReceived()
		if l.state() != SpedTentative {
			t.Fatalf("state %v after media before the handshake, want tentative", l.state())
		}
		l.write(dtlsDatagram(500, 3))
		l.handshakeDone()
		l.mediaReceived()
		if l.state() != SpedComplete {
			t.Fatalf("state %v, want complete", l.state())
		}
	})
	t.Run("complete on a message without attributes while pending", func(t *testing.T) {
		l := newSpedLink((&spedSink{}).write, true)
		l.write(dtlsDatagram(500, 3))
		l.handshakeDone()
		l.onMessage(spedMessage(t, stun.BindingSuccess))
		if l.state() != SpedComplete {
			t.Fatalf("state %v, want complete", l.state())
		}
	})
	t.Run("off on a first message without attributes", func(t *testing.T) {
		sink := &spedSink{}
		l := newSpedLink(sink.write, true)
		l.write(dtlsDatagram(120, 1))
		l.onMessage(spedMessage(t, stun.BindingSuccess))
		if l.state() != SpedOff {
			t.Fatalf("state %v, want off", l.state())
		}
		// Off sends raw, embeds nothing and ACKs nothing.
		l.write(dtlsDatagram(120, 2))
		if sink.count() != 2 {
			t.Errorf("%v sent raw, want 2", sink.count())
		}
		l.received(dtlsDatagram(80, 4), false)
		if _, hasData, _, hasAck := spedAttrs(t, spedMessage(t, stun.BindingRequest, l)); hasData || hasAck {
			t.Errorf("DATA %v, ACK %v when off", hasData, hasAck)
		}
		if s := l.stats(); s.RawRecv != 0 || s.EmbeddedSent != 0 {
			t.Errorf("stats %+v when off", s)
		}
	})
}

// TestSpedLinkHoldUntilWritable holds raw sends until the pair answers a
// check, then sends the flight raw at once, as Chrome does.
func TestSpedLinkHoldUntilWritable(t *testing.T) {
	sink := &spedSink{}
	l := newSpedLink(sink.write, false)
	l.write(dtlsDatagram(500, 1))
	l.write(dtlsDatagram(400, 2))
	select {
	case <-l.captured:
	default:
		t.Error("the first capture isn't signalled")
	}
	if sink.count() != 0 {
		t.Fatalf("%v sent raw before the pair is writable", sink.count())
	}
	l.setWritable()
	l.setWritable()
	if sink.count() != 2 {
		t.Errorf("%v sent raw once writable, want 2", sink.count())
	}
	l.write(dtlsDatagram(300, 3))
	if sink.count() != 3 {
		t.Errorf("%v sent raw, want 3", sink.count())
	}
}

// TestSpedLinkBodyLimit embeds a datagram only while the STUN body before
// MESSAGE-INTEGRITY stays within 1200 - 24 - 8 bytes.
func TestSpedLinkBodyLimit(t *testing.T) {
	for _, tc := range []struct {
		size  int
		embed bool
	}{{900, true}, {1100, true}, {1200, false}} {
		t.Run(fmt.Sprint(tc.size), func(t *testing.T) {
			l := newSpedLink((&spedSink{}).write, true)
			l.write(dtlsDatagram(tc.size, 1))
			m := spedMessage(t, stun.BindingRequest, stun.NewUsername("srvu1234:cliu5678"), l)
			_, hasData, _, hasAck := spedAttrs(t, m)
			if hasData != tc.embed || !hasAck {
				t.Errorf("DATA %v, ACK %v, want DATA %v and the ACK", hasData, hasAck, tc.embed)
			}
			// The body before MESSAGE-INTEGRITY (24) and FINGERPRINT (8).
			if body := int(m.Length) - 24 - 8; tc.embed && body > spedMaxBody {
				t.Errorf("a body of %v bytes, over %v", body, spedMaxBody)
			}
			if s := l.stats(); s.EmbeddedSent != map[bool]int{true: 1}[tc.embed] {
				t.Errorf("embedded sent %v", s.EmbeddedSent)
			}
		})
	}
}

// TestWithIceOption adds a token to the offer's session-level ICE options,
// and to each media's, once.
func TestWithIceOption(t *testing.T) {
	plain := "v=0\r\nt=0 0\r\nm=audio 9 UDP 111\r\na=mid:0\r\nm=video 9 UDP 96\r\na=mid:1\r\n"
	got := withIceOption(plain, "sped")
	if !before(got, "a=ice-options:sped\r\n", "m=audio") || strings.Count(got, "a=ice-options") != 1 {
		t.Errorf("offer %q, want one session-level a=ice-options:sped", got)
	}
	media := "v=0\r\nt=0 0\r\nm=audio 9 UDP 111\r\na=ice-options:trickle\r\nm=video 9 UDP 96\r\na=ice-options:trickle\r\n"
	got = withIceOption(media, "sped")
	if strings.Count(got, "a=ice-options:trickle sped\r\n") != 2 || !before(got, "a=ice-options:sped\r\n", "m=audio") {
		t.Errorf("offer %q, want sped at the session and on each media", got)
	}
	session := "v=0\r\nt=0 0\r\na=ice-options:trickle\r\nm=audio 9 UDP 111\r\n"
	if got = withIceOption(session, "sped"); strings.Count(got, "a=ice-options") != 1 || !strings.Contains(got, "a=ice-options:trickle sped\r\n") {
		t.Errorf("offer %q, want sped added to the session line", got)
	}
	if again := withIceOption(got, "sped"); again != got {
		t.Errorf("offer %q, want it unchanged", again)
	}
}

// TestParseAnswerSped reads sped from the answer's ICE options as a whole
// token, at the session or a media level.
func TestParseAnswerSped(t *testing.T) {
	for _, tc := range []struct {
		line string
		sped bool
	}{{"", false}, {"a=ice-options:sped\r\n", true}, {"a=ice-options:trickle sped\r\n", true}, {"a=ice-options:googspedv1\r\n", false}} {
		a, err := parseAnswer(strings.Replace(srsAnswer, "a=ice-lite\r\n", "a=ice-lite\r\n"+tc.line, 1))
		if err != nil {
			t.Fatal(err)
		}
		if a.sped != tc.sped {
			t.Errorf("%q: sped %v, want %v", tc.line, a.sped, tc.sped)
		}
	}
}

// TestRawSped does the DTLS handshake inside STUN with a fake SPED server,
// with every raw DTLS datagram lost both ways, over UDP and TCP, and plays.
func TestRawSped(t *testing.T) {
	for _, tcp := range []bool{false, true} {
		t.Run(fmt.Sprintf("TCP=%v", tcp), func(t *testing.T) {
			t.Parallel()
			srv := newFakeSped(t, tcp, false)
			f := &Faults{}
			f.Drop(FaultIn, FaultDTLS, 1000)
			f.Drop(FaultOut, FaultDTLS, 1000)
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{TCP: tcp, SPED: true, Faults: f}, true)
			if err != nil {
				t.Fatal(err)
			}
			defer p.Close()
			if !strings.Contains(p.Offer, "a=ice-options:sped") || !p.DTLS {
				t.Fatalf("DTLS %v, offer %q", p.DTLS, p.Offer)
			}
			if f.Dropped(FaultOut, FaultDTLS) == 0 || f.Dropped(FaultIn, FaultDTLS) == 0 {
				t.Errorf("dropped %v DTLS out and %v in, want some each way", f.Dropped(FaultOut, FaultDTLS), f.Dropped(FaultIn, FaultDTLS))
			}
			waitSped(t, "client", p.SPED, SpedComplete)
			waitSped(t, "server", srv.link.stats, SpedComplete)
			c, s := p.SPED(), srv.link.stats()
			// The client embeds its two flights, and the server its two.
			if c.EmbeddedSent < 2 || c.EmbeddedRecv < 2 || s.EmbeddedSent < 2 || s.EmbeddedRecv < 2 {
				t.Errorf("client %+v, server %+v, want at least 2 embedded each way", c, s)
			}
			if c.RawRecv != 0 {
				t.Errorf("client %+v, want no raw DTLS", c)
			}
			srv.sendSamples(t, readSamples(t, time.Second))
			if _, err := p.Until(ctx, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 10}); err != nil {
				t.Fatal(err)
			}
		})
	}
}

// TestEmbeddedHandshake reads the handshake type of the first DTLS record a
// STUN message embeds, and nothing from a message without DATA, a DATA
// that is not a handshake, or a datagram that is not STUN.
func TestEmbeddedHandshake(t *testing.T) {
	// A DTLS 1.2 record header, epoch 0, sequence 1, then a handshake
	// message of a type.
	record := func(content, typ byte) []byte {
		b := []byte{content, 0xfe, 0xfd, 0, 0, 0, 0, 0, 0, 0, 1, 0, 12, typ}
		return append(b, make([]byte, 11)...)
	}
	embed := func(v []byte) []byte {
		return spedMessage(t, stun.BindingRequest, stun.RawAttribute{Type: stun.AttrDtlsInStun, Value: v}).Raw
	}
	for _, tc := range []struct {
		name string
		b    []byte
		typ  byte
		ok   bool
	}{
		{"certificate", embed(record(0x16, HandshakeCertificate)), HandshakeCertificate, true},
		{"client hello", embed(record(0x16, 1)), 1, true},
		{"change cipher spec", embed(record(0x14, 1)), 0, false},
		{"short record", embed(record(0x16, 1)[:13]), 0, false},
		{"ACK only", spedMessage(t, stun.BindingRequest, spedAcks(1)).Raw, 0, false},
		{"raw DTLS", record(0x16, HandshakeCertificate), 0, false},
		{"not a message", []byte{0x00, 0x01}, 0, false},
	} {
		if typ, ok := EmbeddedHandshake(tc.b); typ != tc.typ || ok != tc.ok {
			t.Errorf("%v: got %v, %v, want %v, %v", tc.name, typ, ok, tc.typ, tc.ok)
		}
	}
}

// TestEmbeddedDTLS gets the DTLS datagram a check or a response embeds, as
// SRS embeds its last flight, and nothing from an ACK-only message, raw
// DTLS, or a datagram that is not STUN.
func TestEmbeddedDTLS(t *testing.T) {
	ccs := []byte{0x14, 0xfe, 0xfd, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1}
	embed := func(typ stun.MessageType, v []byte) []byte {
		return spedMessage(t, typ, stun.RawAttribute{Type: stun.AttrDtlsInStun, Value: v}).Raw
	}
	for _, tc := range []struct {
		name string
		b    []byte
		want []byte
		ok   bool
	}{
		{"response", embed(stun.BindingSuccess, ccs), ccs, true},
		{"check", embed(stun.BindingRequest, ccs), ccs, true},
		{"ACK only", spedMessage(t, stun.BindingSuccess, spedAcks(1)).Raw, nil, false},
		{"raw DTLS", ccs, nil, false},
		{"not a message", []byte{0x00, 0x01}, nil, false},
	} {
		got, ok := EmbeddedDTLS(tc.b)
		if string(got) != string(tc.want) || ok != tc.ok {
			t.Errorf("%v: got %x, %v, want %x, %v", tc.name, got, ok, tc.want, tc.ok)
		}
	}
}

// TestHasChangeCipherSpec finds a ChangeCipherSpec record in a DTLS 1.2
// datagram, after any records before it, as in a server's last flight with
// a NewSessionTicket first, and none in a first flight, a datagram whose
// record is cut short, or one that is not DTLS.
func TestHasChangeCipherSpec(t *testing.T) {
	// A DTLS 1.2 record of a content type, epoch 0, with a body of n bytes.
	record := func(content byte, n int) []byte {
		b := []byte{content, 0xfe, 0xfd, 0, 0, 0, 0, 0, 0, 0, 1, byte(n >> 8), byte(n)}
		return append(b, make([]byte, n)...)
	}
	join := func(rs ...[]byte) []byte {
		var b []byte
		for _, r := range rs {
			b = append(b, r...)
		}
		return b
	}
	for _, tc := range []struct {
		name string
		b    []byte
		want bool
	}{
		{"ccs and finished", join(record(0x14, 1), record(0x16, 40)), true},
		{"ticket, ccs and finished", join(record(0x16, 200), record(0x14, 1), record(0x16, 40)), true},
		{"first flight", join(record(0x16, 100), record(0x16, 12)), false},
		{"record cut short", join(record(0x16, 100)[:50], record(0x14, 1)), false},
		{"rtp", append([]byte{0x80}, make([]byte, 20)...), false},
		{"empty", nil, false},
	} {
		if got := HasChangeCipherSpec(tc.b); got != tc.want {
			t.Errorf("%v: got %v, want %v", tc.name, got, tc.want)
		}
	}
}

// TestRawSpedLostResponse completes the handshake inside STUN when the
// first response is lost: the next check carries the ClientHello again.
func TestRawSpedLostResponse(t *testing.T) {
	srv := newFakeSped(t, false, false)
	f := &Faults{}
	f.Drop(FaultIn, FaultSTUN, 1)
	f.Drop(FaultIn, FaultDTLS, 1000)
	f.Drop(FaultOut, FaultDTLS, 1000)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	p, err := RawPublish(ctx, srv.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{SPED: true, Faults: f}, true)
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	if f.Dropped(FaultIn, FaultSTUN) != 1 {
		t.Errorf("dropped %v responses, want 1", f.Dropped(FaultIn, FaultSTUN))
	}
	waitSped(t, "client", p.SPED, SpedComplete)
	if s := srv.link.stats(); s.EmbeddedRecv < 3 {
		t.Errorf("server %+v, want the ClientHello twice and the last flight", s)
	}
}

// TestRawSpedFallback does the handshake raw when the answer has no sped,
// and when the server's response has no SPED attribute.
func TestRawSpedFallback(t *testing.T) {
	t.Run("answer without sped", func(t *testing.T) {
		t.Parallel()
		srv := newFakeRaw(t, true)
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{SPED: true}, true)
		if err != nil {
			t.Fatal(err)
		}
		defer p.Close()
		if !strings.Contains(p.Offer, "a=ice-options:sped") || !p.DTLS {
			t.Errorf("DTLS %v, offer %q", p.DTLS, p.Offer)
		}
		if s := p.SPED(); s.State != SpedNone {
			t.Errorf("SPED %+v, want none", s)
		}
	})
	t.Run("response without attributes", func(t *testing.T) {
		t.Parallel()
		srv := newFakeSped(t, false, true)
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{SPED: true}, true)
		if err != nil {
			t.Fatal(err)
		}
		defer p.Close()
		if !p.DTLS {
			t.Error("no DTLS")
		}
		if s := p.SPED(); s.State != SpedOff || s.EmbeddedRecv != 0 {
			t.Errorf("SPED %+v, want off", s)
		}
	})
}

// TestRawSpedCheckInterval sends a check about every 50ms while the server
// doesn't answer.
func TestRawSpedCheckInterval(t *testing.T) {
	srv := newFakeSped(t, false, false)
	f := &Faults{}
	f.Drop(FaultIn, FaultSTUN, 1000)
	// The server flight goes raw too, and is lost, so the handshake can't
	// complete.
	f.Drop(FaultIn, FaultDTLS, 1000)
	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()
	if _, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{SPED: true, Faults: f}, true); err == nil {
		t.Fatal("connected without a response")
	}
	// 500ms at 50ms is 10 checks; allow for a slow start and a busy host.
	if n := srv.requestCount(); n < 5 || n > 13 {
		t.Errorf("%v checks in 500ms, want about 10", n)
	}
	if s := srv.link.stats(); s.EmbeddedRecv < 5 {
		t.Errorf("server %+v, want the ClientHello in every check", s)
	}
}

// waitSped waits until a SPED side reaches a state.
func waitSped(t *testing.T, who string, stats func() SpedStats, want SpedState) {
	t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if stats().State == want {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("%v SPED %+v, want %v", who, stats(), want)
}
