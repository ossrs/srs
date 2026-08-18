// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"srsx/internal/env/envfakes"
	"srsx/internal/lb"
	"srsx/internal/lb/lbfakes"
	"srsx/internal/logger"
)

// encodeSRTStreamIDExt builds an SRT extension block carrying the given stream
// id as extension type 0x05. The wire format places the type and length (in
// 4-byte words) as big-endian uint16s, followed by the payload with each
// 4-byte word stored in little-endian byte order — the inverse of what
// SRTHandshakePacket.StreamID does on read.
func encodeSRTStreamIDExt(sid string) []byte {
	padded := []byte(sid)
	if rem := len(padded) % 4; rem != 0 {
		padded = append(padded, make([]byte, 4-rem)...)
	}

	swapped := make([]byte, len(padded))
	for i := 0; i < len(padded); i += 4 {
		swapped[i+0] = padded[i+3]
		swapped[i+1] = padded[i+2]
		swapped[i+2] = padded[i+1]
		swapped[i+3] = padded[i+0]
	}

	hdr := make([]byte, 4)
	binary.BigEndian.PutUint16(hdr[0:], 0x05)
	binary.BigEndian.PutUint16(hdr[2:], uint16(len(padded)/4))
	return append(hdr, swapped...)
}

func TestSRTHandshakePacket_FlagPredicates(t *testing.T) {
	cases := []struct {
		name        string
		flag        uint8
		ctype       uint16
		stype       uint16
		isData      bool
		isControl   bool
		isHandshake bool
	}{
		{"data-packet", 0x00, 0, 0, true, false, false},
		{"handshake", 0x80, 0, 0, false, true, true},
		{"control-not-handshake-by-ctype", 0x80, 1, 0, false, true, false},
		{"control-not-handshake-by-stype", 0x80, 0, 1, false, true, false},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			p := &SRTHandshakePacket{ControlFlag: c.flag, ControlType: c.ctype, SubType: c.stype}
			if got := p.IsData(); got != c.isData {
				t.Fatalf("IsData=%v, want %v", got, c.isData)
			}
			if got := p.IsControl(); got != c.isControl {
				t.Fatalf("IsControl=%v, want %v", got, c.isControl)
			}
			if got := p.IsHandshake(); got != c.isHandshake {
				t.Fatalf("IsHandshake=%v, want %v", got, c.isHandshake)
			}
		})
	}
}

func TestSRTHandshakePacket_String_ContainsKeyFields(t *testing.T) {
	p := &SRTHandshakePacket{
		ControlFlag: 0x80,
		SocketID:    0xdeadbeef,
		SRTSocketID: 0xcafebabe,
		PeerIP:      net.ParseIP("1.2.3.4"),
		ExtraData:   []byte{0, 1, 2, 3, 4},
	}
	s := p.String()
	for _, want := range []string{"Control=true", "SocketID=3735928559", "SRTSocketID=3405691582", "Peer=16B", "Extra=5B"} {
		if !strings.Contains(s, want) {
			t.Fatalf("String()=%q missing %q", s, want)
		}
	}
}

func TestSRTHandshakePacket_UnmarshalBinary_ShortBuffers(t *testing.T) {
	if err := (&SRTHandshakePacket{}).UnmarshalBinary([]byte{0x80}); err == nil {
		t.Fatal("expected error for <4 byte buffer")
	}
	if err := (&SRTHandshakePacket{}).UnmarshalBinary(make([]byte, 32)); err == nil {
		t.Fatal("expected error for <64 byte buffer")
	}
}

func TestSRTHandshakePacket_UnmarshalBinary_ParsesControlBits(t *testing.T) {
	b := make([]byte, 64)
	// First 16 bits: top bit = control flag (0x80), bottom 15 bits = ControlType (0x1234).
	binary.BigEndian.PutUint16(b[0:], 0x8000|0x1234)
	binary.BigEndian.PutUint16(b[2:], 0x5678) // SubType.

	p := &SRTHandshakePacket{}
	if err := p.UnmarshalBinary(b); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if p.ControlFlag != 0x80 {
		t.Fatalf("ControlFlag=0x%02x, want 0x80", p.ControlFlag)
	}
	if p.ControlType != 0x1234 {
		t.Fatalf("ControlType=0x%04x, want 0x1234", p.ControlType)
	}
	if p.SubType != 0x5678 {
		t.Fatalf("SubType=0x%04x, want 0x5678", p.SubType)
	}
}

func TestSRTHandshakePacket_UnmarshalBinary_PeerIPByteReversed(t *testing.T) {
	b := make([]byte, 64)
	// Wire bytes 48..51 are stored in reverse order; the parser flips them back
	// to produce IPv4(b[51], b[50], b[49], b[48]).
	b[48] = 4
	b[49] = 3
	b[50] = 2
	b[51] = 1

	p := &SRTHandshakePacket{}
	if err := p.UnmarshalBinary(b); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if want := net.ParseIP("1.2.3.4"); !p.PeerIP.Equal(want) {
		t.Fatalf("PeerIP=%v, want %v", p.PeerIP, want)
	}
}

func TestSRTHandshakePacket_MarshalBinary_Layout(t *testing.T) {
	p := &SRTHandshakePacket{
		ControlFlag:     0x80,
		ControlType:     0x1234,
		SubType:         0x5678,
		AdditionalInfo:  0x11111111,
		Timestamp:       0x22222222,
		SocketID:        0x33333333,
		Version:         5,
		EncryptionField: 2,
		ExtensionField:  0x4A17,
		InitSequence:    0x44444444,
		MTU:             1500,
		FlowWindow:      8192,
		HandshakeType:   1,
		SRTSocketID:     0x55555555,
		SynCookie:       0x66666666,
		PeerIP:          net.ParseIP("10.20.30.40"),
		ExtraData:       []byte{0xaa, 0xbb},
	}

	b, err := p.MarshalBinary()
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if got, want := len(b), 64+len(p.ExtraData); got != want {
		t.Fatalf("len=%d, want %d", got, want)
	}
	if got := binary.BigEndian.Uint16(b[0:]); got != 0x8000|0x1234 {
		t.Fatalf("word0=0x%04x, want 0x9234", got)
	}
	if got := binary.BigEndian.Uint16(b[2:]); got != 0x5678 {
		t.Fatalf("SubType=0x%04x, want 0x5678", got)
	}
	// PeerIP is laid out in reversed octet order on the wire.
	if b[48] != 40 || b[49] != 30 || b[50] != 20 || b[51] != 10 {
		t.Fatalf("PeerIP bytes=[%d %d %d %d], want [40 30 20 10]", b[48], b[49], b[50], b[51])
	}
	if b[64] != 0xaa || b[65] != 0xbb {
		t.Fatalf("ExtraData not copied at offset 64")
	}
}

func TestSRTHandshakePacket_Roundtrip(t *testing.T) {
	orig := &SRTHandshakePacket{
		ControlFlag:     0x80,
		ControlType:     0x0001,
		SubType:         0x0002,
		AdditionalInfo:  0xa1a1a1a1,
		Timestamp:       0xb2b2b2b2,
		SocketID:        0xc3c3c3c3,
		Version:         5,
		EncryptionField: 0,
		ExtensionField:  0x4A17,
		InitSequence:    0xd4d4d4d4,
		MTU:             1500,
		FlowWindow:      8192,
		HandshakeType:   1,
		SRTSocketID:     0xe5e5e5e5,
		SynCookie:       0xf6f6f6f6,
		PeerIP:          net.ParseIP("192.168.1.42"),
		ExtraData:       encodeSRTStreamIDExt("#!::r=live/stream"),
	}

	b, err := orig.MarshalBinary()
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}

	got := &SRTHandshakePacket{}
	if err := got.UnmarshalBinary(b); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}

	if got.ControlFlag != orig.ControlFlag ||
		got.ControlType != orig.ControlType ||
		got.SubType != orig.SubType ||
		got.AdditionalInfo != orig.AdditionalInfo ||
		got.Timestamp != orig.Timestamp ||
		got.SocketID != orig.SocketID ||
		got.Version != orig.Version ||
		got.EncryptionField != orig.EncryptionField ||
		got.ExtensionField != orig.ExtensionField ||
		got.InitSequence != orig.InitSequence ||
		got.MTU != orig.MTU ||
		got.FlowWindow != orig.FlowWindow ||
		got.HandshakeType != orig.HandshakeType ||
		got.SRTSocketID != orig.SRTSocketID ||
		got.SynCookie != orig.SynCookie {
		t.Fatalf("scalar field mismatch\n got=%+v\nwant=%+v", got, orig)
	}
	if !got.PeerIP.Equal(orig.PeerIP) {
		t.Fatalf("PeerIP=%v, want %v", got.PeerIP, orig.PeerIP)
	}
	if sid, err := got.StreamID(); err != nil {
		t.Fatalf("StreamID: %v", err)
	} else if sid != "#!::r=live/stream" {
		t.Fatalf("StreamID=%q, want %q", sid, "#!::r=live/stream")
	}
}

func TestSRTHandshakePacket_StreamID(t *testing.T) {
	t.Run("single-extension-padded", func(t *testing.T) {
		p := &SRTHandshakePacket{ExtraData: encodeSRTStreamIDExt("abc")}
		sid, err := p.StreamID()
		if err != nil {
			t.Fatalf("StreamID: %v", err)
		}
		if sid != "abc" {
			t.Fatalf("StreamID=%q, want %q", sid, "abc")
		}
	})

	t.Run("multi-word-payload", func(t *testing.T) {
		p := &SRTHandshakePacket{ExtraData: encodeSRTStreamIDExt("abcdefgh")}
		sid, err := p.StreamID()
		if err != nil {
			t.Fatalf("StreamID: %v", err)
		}
		if sid != "abcdefgh" {
			t.Fatalf("StreamID=%q, want %q", sid, "abcdefgh")
		}
	})

	t.Run("skip-other-extensions", func(t *testing.T) {
		// First a non-0x05 extension of size 1 word, then the real stream id.
		other := []byte{0x00, 0x01, 0x00, 0x01, 0xde, 0xad, 0xbe, 0xef}
		p := &SRTHandshakePacket{ExtraData: append(other, encodeSRTStreamIDExt("live/stream")...)}
		sid, err := p.StreamID()
		if err != nil {
			t.Fatalf("StreamID: %v", err)
		}
		if sid != "live/stream" {
			t.Fatalf("StreamID=%q, want %q", sid, "live/stream")
		}
	})

	t.Run("trims-trailing-nuls", func(t *testing.T) {
		// "ab" → padded to "ab\x00\x00", wire-swapped to {0,0,'b','a'}, then
		// parsed back to "ab\x00\x00" and trimmed to "ab".
		p := &SRTHandshakePacket{ExtraData: encodeSRTStreamIDExt("ab")}
		sid, err := p.StreamID()
		if err != nil {
			t.Fatalf("StreamID: %v", err)
		}
		if sid != "ab" {
			t.Fatalf("StreamID=%q, want %q", sid, "ab")
		}
	})

	t.Run("empty-extra-returns-error", func(t *testing.T) {
		p := &SRTHandshakePacket{}
		if _, err := p.StreamID(); err == nil {
			t.Fatal("expected error for empty ExtraData")
		}
	})

	t.Run("declared-size-exceeds-buffer", func(t *testing.T) {
		// Extension type 0x05 claims 4 words (16 bytes) but only 4 bytes follow.
		p := &SRTHandshakePacket{ExtraData: []byte{0x00, 0x05, 0x00, 0x04, 0xaa, 0xbb, 0xcc, 0xdd}}
		if _, err := p.StreamID(); err == nil {
			t.Fatal("expected error when declared size exceeds buffer")
		}
	})

	t.Run("only-non-streamid-extension-returns-error", func(t *testing.T) {
		// One full extension that's not type 0x05; walker advances and then
		// runs out of bytes for the next header → error.
		p := &SRTHandshakePacket{ExtraData: []byte{0x00, 0x01, 0x00, 0x01, 0xde, 0xad, 0xbe, 0xef}}
		if _, err := p.StreamID(); err == nil {
			t.Fatal("expected error when no stream id extension is present")
		}
	})
}

// ---------------------------------------------------------------------------
// SRTConnection: fakes, fixture, and tests
// ---------------------------------------------------------------------------

// newHandshake0 builds a client INDUCTION handshake packet (SynCookie == 0).
func newHandshake0(srtSocketID uint32) *SRTHandshakePacket {
	return &SRTHandshakePacket{
		ControlFlag:   0x80,
		ControlType:   0,
		SubType:       0,
		MTU:           1500,
		FlowWindow:    8192,
		HandshakeType: 1,
		Version:       4,
		InitSequence:  0xdeadbeef,
		SRTSocketID:   srtSocketID,
		PeerIP:        net.ParseIP("127.0.0.1"),
	}
}

// newHandshake2 builds a client CONCLUSION handshake packet carrying the given
// stream id (SynCookie must be non-zero so it enters the handshake-2 branch).
func newHandshake2(srtSocketID uint32, cookie uint32, streamID string) *SRTHandshakePacket {
	return &SRTHandshakePacket{
		ControlFlag:   0x80,
		ControlType:   0,
		SubType:       0,
		Version:       5,
		HandshakeType: 0xFFFFFFFF, // CONCLUSION
		SRTSocketID:   srtSocketID,
		SynCookie:     cookie,
		PeerIP:        net.ParseIP("127.0.0.1"),
		ExtraData:     encodeSRTStreamIDExt(streamID),
	}
}

// marshalOrFatal marshals a handshake packet; fails the test on error.
func marshalOrFatal(t *testing.T, p *SRTHandshakePacket) []byte {
	t.Helper()
	b, err := p.MarshalBinary()
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	return b
}

// srtConnFixture wires an SRTConnection with fakes for the load balancer,
// listener, and backend dial seam.
type srtConnFixture struct {
	conn     *SRTConnection
	lb       *lbfakes.FakeOriginLoadBalancer
	listener *fakePacketConn
	backend  *fakeBackendUDP
	dialErr  error
	dialIP   string
	dialPort int
}

func newSRTConnFixture() *srtConnFixture {
	f := &srtConnFixture{
		lb:       &lbfakes.FakeOriginLoadBalancer{},
		listener: newFakePacketConn(),
		backend:  newFakeBackendUDP(),
	}
	f.conn = NewSRTConnection(func(c *SRTConnection) {
		c.ctx = logger.WithContext(context.Background())
		c.loadBalancer = f.lb
		c.listenerUDP = f.listener
		c.start = time.Now()
		c.dialBackendUDP = func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
			f.dialIP, f.dialPort = ip, port
			if f.dialErr != nil {
				return nil, f.dialErr
			}
			return f.backend, nil
		}
	})
	return f
}

func TestNewSRTConnection(t *testing.T) {
	t.Run("defaults dialBackendUDP", func(t *testing.T) {
		c := NewSRTConnection()
		if c.dialBackendUDP == nil {
			t.Fatal("expected dialBackendUDP to be defaulted")
		}
	})

	t.Run("applies functional options", func(t *testing.T) {
		c := NewSRTConnection(func(c *SRTConnection) {
			c.socketID = 0xabc
		})
		if c.socketID != 0xabc {
			t.Fatalf("socketID=%x, want 0xabc", c.socketID)
		}
	})

	t.Run("options override default dialBackendUDP", func(t *testing.T) {
		called := false
		dial := func(ctx context.Context, ip string, port int) (io.ReadWriteCloser, error) {
			called = true
			return nil, nil
		}
		c := NewSRTConnection(func(c *SRTConnection) { c.dialBackendUDP = dial })
		_, _ = c.dialBackendUDP(context.Background(), "", 0)
		if !called {
			t.Fatal("expected overridden dialBackendUDP to be invoked")
		}
	})
}

func TestSRTConnection_HandlePacket_NoHandshake(t *testing.T) {
	t.Run("noop when backendUDP not set", func(t *testing.T) {
		f := newSRTConnFixture()
		f.conn.socketID = 42

		sid, err := f.conn.HandlePacket(nil, &net.UDPAddr{}, []byte("payload"))
		if err != nil {
			t.Fatalf("unexpected err=%v", err)
		}
		if sid != 42 {
			t.Fatalf("socketID=%d, want 42", sid)
		}
	})

	t.Run("writes data to backend", func(t *testing.T) {
		f := newSRTConnFixture()
		f.conn.backendUDP = f.backend
		f.conn.socketID = 7

		sid, err := f.conn.HandlePacket(nil, &net.UDPAddr{}, []byte("payload"))
		if err != nil {
			t.Fatalf("unexpected err=%v", err)
		}
		if sid != 7 {
			t.Fatalf("socketID=%d, want 7", sid)
		}

		select {
		case got := <-f.backend.writes:
			if string(got) != "payload" {
				t.Fatalf("backend got %q, want %q", got, "payload")
			}
		case <-time.After(time.Second):
			t.Fatal("timeout waiting for backend write")
		}
	})

	t.Run("propagates backend write error", func(t *testing.T) {
		f := newSRTConnFixture()
		f.conn.backendUDP = f.backend
		f.backend.writeErr = errors.New("write-fail")

		_, err := f.conn.HandlePacket(nil, &net.UDPAddr{}, []byte("payload"))
		if err == nil || !strings.Contains(err.Error(), "write-fail") {
			t.Fatalf("expected write-fail err, got %v", err)
		}
	})
}

func TestSRTConnection_HandleHandshake_Step0(t *testing.T) {
	t.Run("replies handshake 1 with proxy cookie", func(t *testing.T) {
		f := newSRTConnFixture()
		client := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 9000}

		hs0 := newHandshake0(0x11111111)
		if _, err := f.conn.HandlePacket(hs0, client, marshalOrFatal(t, hs0)); err != nil {
			t.Fatalf("HandlePacket err=%v", err)
		}

		if f.conn.handshake0 != hs0 {
			t.Fatal("handshake0 was not saved on the connection")
		}
		if f.conn.handshake1 == nil {
			t.Fatal("handshake1 was not built")
		}
		// Proxy always replies INDUCTION with its own fixed cookie and the
		// SRT magic ExtensionField, per the RFC induction message format.
		if f.conn.handshake1.SynCookie != 0x418d5e4e {
			t.Fatalf("handshake1.SynCookie=0x%08x, want 0x418d5e4e", f.conn.handshake1.SynCookie)
		}
		if f.conn.handshake1.ExtensionField != 0x4A17 {
			t.Fatalf("handshake1.ExtensionField=0x%04x, want 0x4A17", f.conn.handshake1.ExtensionField)
		}

		select {
		case got := <-f.listener.writes:
			if got.addr != client {
				t.Fatalf("listener got addr=%v, want %v", got.addr, client)
			}
			parsed := &SRTHandshakePacket{}
			if err := parsed.UnmarshalBinary(got.data); err != nil {
				t.Fatalf("unmarshal listener write: %v", err)
			}
			if parsed.SynCookie != 0x418d5e4e {
				t.Fatalf("on-wire SynCookie=0x%08x, want 0x418d5e4e", parsed.SynCookie)
			}
		case <-time.After(time.Second):
			t.Fatal("timeout waiting for listener write")
		}
	})

	t.Run("listener write error is propagated", func(t *testing.T) {
		f := newSRTConnFixture()
		f.listener.writeErr = errors.New("listen-write-fail")

		hs0 := newHandshake0(0x11111111)
		_, err := f.conn.HandlePacket(hs0, &net.UDPAddr{}, marshalOrFatal(t, hs0))
		if err == nil || !strings.Contains(err.Error(), "listen-write-fail") {
			t.Fatalf("expected propagated listener err, got %v", err)
		}
	})
}

func TestSRTConnection_HandleHandshake_Step2_StreamIDError(t *testing.T) {
	f := newSRTConnFixture()
	// Cookie != 0 puts us on the handshake-2 path; no 0x05 extension means
	// StreamID() returns an error before we ever touch the load balancer.
	pkt := &SRTHandshakePacket{
		ControlFlag:   0x80,
		HandshakeType: 0xFFFFFFFF,
		SRTSocketID:   1,
		SynCookie:     0x418d5e4e,
		PeerIP:        net.ParseIP("127.0.0.1"),
	}
	_, err := f.conn.HandlePacket(pkt, &net.UDPAddr{}, marshalOrFatal(t, pkt))
	if err == nil || !strings.Contains(err.Error(), "parse stream id") {
		t.Fatalf("expected parse-stream-id err, got %v", err)
	}
	if f.lb.PickCallCount() != 0 {
		t.Fatal("expected Pick not to be called when stream id parse fails")
	}
}

func TestSRTConnection_HandleHandshake_Step2_FullFlow(t *testing.T) {
	f := newSRTConnFixture()
	f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", SRT: []string{"20080"}}, nil)
	client := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 9000}

	// Step 0 first, to populate handshake0 and the proxy's handshake1 (cookie
	// 0x418d5e4e). The listener write for hs1 is drained so it does not block
	// later assertions.
	hs0 := newHandshake0(0x11111111)
	if _, err := f.conn.HandlePacket(hs0, client, marshalOrFatal(t, hs0)); err != nil {
		t.Fatalf("hs0 HandlePacket err=%v", err)
	}
	<-f.listener.writes

	// Pre-feed backend's hs1 (with its own cookie) and hs3 (with its own
	// socket id) so the synchronous Reads inside handleHandshake unblock.
	const backendCookie uint32 = 0x12345678
	const backendSocketID uint32 = 0xabcd1234
	f.backend.reads <- marshalOrFatal(t, &SRTHandshakePacket{
		ControlFlag: 0x80, SynCookie: backendCookie, PeerIP: net.ParseIP("127.0.0.1"),
	})
	f.backend.reads <- marshalOrFatal(t, &SRTHandshakePacket{
		ControlFlag: 0x80, SRTSocketID: backendSocketID, SynCookie: backendCookie, PeerIP: net.ParseIP("127.0.0.1"),
	})

	hs2 := newHandshake2(0x11111111, 0x418d5e4e, "#!::r=live/stream")
	sid, err := f.conn.HandlePacket(hs2, client, marshalOrFatal(t, hs2))
	if err != nil {
		t.Fatalf("hs2 HandlePacket err=%v", err)
	}
	if sid != backendSocketID {
		t.Fatalf("returned socketID=0x%08x, want 0x%08x", sid, backendSocketID)
	}
	if f.conn.socketID != backendSocketID {
		t.Fatalf("conn.socketID=0x%08x, want 0x%08x", f.conn.socketID, backendSocketID)
	}
	if f.dialIP != "127.0.0.1" || f.dialPort != 20080 {
		t.Fatalf("dial got ip=%q port=%d, want 127.0.0.1:20080", f.dialIP, f.dialPort)
	}

	// First backend write is the raw hs0 from the client; second is hs2 with
	// the cookie rewritten to the backend's value (not the proxy's).
	got0 := drainBackendWrite(t, f.backend)
	parsed0 := &SRTHandshakePacket{}
	if err := parsed0.UnmarshalBinary(got0); err != nil {
		t.Fatalf("unmarshal hs0 sent to backend: %v", err)
	}
	if parsed0.SynCookie != 0 {
		t.Fatalf("hs0 forwarded with SynCookie=0x%08x, want 0", parsed0.SynCookie)
	}

	got2 := drainBackendWrite(t, f.backend)
	parsed2 := &SRTHandshakePacket{}
	if err := parsed2.UnmarshalBinary(got2); err != nil {
		t.Fatalf("unmarshal hs2 sent to backend: %v", err)
	}
	if parsed2.SynCookie != backendCookie {
		t.Fatalf("hs2 to backend SynCookie=0x%08x, want 0x%08x", parsed2.SynCookie, backendCookie)
	}

	// hs3 to the client must carry the proxy's cookie, not the backend's.
	got3 := drainListenerWrite(t, f.listener, client)
	parsed3 := &SRTHandshakePacket{}
	if err := parsed3.UnmarshalBinary(got3); err != nil {
		t.Fatalf("unmarshal hs3 sent to client: %v", err)
	}
	if parsed3.SynCookie != 0x418d5e4e {
		t.Fatalf("hs3 to client SynCookie=0x%08x, want 0x418d5e4e", parsed3.SynCookie)
	}
	if parsed3.SRTSocketID != backendSocketID {
		t.Fatalf("hs3 to client SRTSocketID=0x%08x, want 0x%08x", parsed3.SRTSocketID, backendSocketID)
	}

	// Cleanly terminate the background backend→client forwarder goroutine.
	_ = f.backend.Close()
}

func drainBackendWrite(t *testing.T, b *fakeBackendUDP) []byte {
	t.Helper()
	select {
	case got := <-b.writes:
		return got
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for backend write")
		return nil
	}
}

func drainListenerWrite(t *testing.T, l *fakePacketConn, wantAddr net.Addr) []byte {
	t.Helper()
	select {
	case got := <-l.writes:
		if got.addr != wantAddr {
			t.Fatalf("listener addr=%v, want %v", got.addr, wantAddr)
		}
		return got.data
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for listener write")
		return nil
	}
}

func TestSRTConnection_ConnectBackend(t *testing.T) {
	t.Run("noop when already connected", func(t *testing.T) {
		f := newSRTConnFixture()
		f.conn.backendUDP = f.backend
		if err := f.conn.connectBackend(context.Background(), "#!::r=live/stream"); err != nil {
			t.Fatalf("unexpected err=%v", err)
		}
		if f.lb.PickCallCount() != 0 {
			t.Fatal("expected Pick not to be called when already connected")
		}
	})

	t.Run("propagates ParseSRTStreamID error", func(t *testing.T) {
		f := newSRTConnFixture()
		err := f.conn.connectBackend(context.Background(), "no-resource-key")
		if err == nil || !strings.Contains(err.Error(), "parse stream id") {
			t.Fatalf("expected parse-stream-id err, got %v", err)
		}
	})

	t.Run("propagates Pick error", func(t *testing.T) {
		f := newSRTConnFixture()
		f.lb.PickReturns(nil, errors.New("pick-fail"))
		err := f.conn.connectBackend(context.Background(), "#!::r=live/stream")
		if err == nil || !strings.Contains(err.Error(), "pick-fail") {
			t.Fatalf("expected pick err, got %v", err)
		}
	})

	t.Run("errors when backend has no SRT endpoints", func(t *testing.T) {
		f := newSRTConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1"}, nil)
		err := f.conn.connectBackend(context.Background(), "#!::r=live/stream")
		if err == nil || !strings.Contains(err.Error(), "no udp server") {
			t.Fatalf("expected no-udp-server err, got %v", err)
		}
	})

	t.Run("propagates ParseListenEndpoint error", func(t *testing.T) {
		f := newSRTConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", SRT: []string{"not-a-port"}}, nil)
		err := f.conn.connectBackend(context.Background(), "#!::r=live/stream")
		if err == nil || !strings.Contains(err.Error(), "parse udp port") {
			t.Fatalf("expected parse-udp-port err, got %v", err)
		}
	})

	t.Run("propagates dial error", func(t *testing.T) {
		f := newSRTConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "127.0.0.1", SRT: []string{"20080"}}, nil)
		f.dialErr = errors.New("dial-fail")
		err := f.conn.connectBackend(context.Background(), "#!::r=live/stream")
		if err == nil || !strings.Contains(err.Error(), "dial-fail") {
			t.Fatalf("expected dial err, got %v", err)
		}
		if f.conn.backendUDP != nil {
			t.Fatal("backendUDP should remain nil on dial failure")
		}
	})

	t.Run("success sets backendUDP and forwards ip/port", func(t *testing.T) {
		f := newSRTConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.5", SRT: []string{"20080"}}, nil)
		if err := f.conn.connectBackend(context.Background(), "#!::r=live/stream"); err != nil {
			t.Fatalf("unexpected err=%v", err)
		}
		if f.conn.backendUDP != f.backend {
			t.Fatal("backendUDP not set to dialed connection")
		}
		if f.dialIP != "10.0.0.5" || f.dialPort != 20080 {
			t.Fatalf("dial got ip=%q port=%d, want 10.0.0.5:20080", f.dialIP, f.dialPort)
		}
	})

	t.Run("defaults host to localhost when stream id has no h=", func(t *testing.T) {
		f := newSRTConnFixture()
		f.lb.PickReturns(&lb.OriginServer{IP: "10.0.0.5", SRT: []string{"20080"}}, nil)
		if err := f.conn.connectBackend(context.Background(), "#!::r=live/stream"); err != nil {
			t.Fatalf("unexpected err=%v", err)
		}
		// Pick is called with a stream URL built from "srt://localhost/live/stream";
		// BuildStreamURL normalizes hostnames without a "." to __defaultVhost__.
		_, gotURL := f.lb.PickArgsForCall(0)
		if !strings.Contains(gotURL, "__defaultVhost__") {
			t.Fatalf("Pick streamURL=%q, want default-vhost form", gotURL)
		}
	})
}

// ---------------------------------------------------------------------------
// srsSRTProxyServer: fixture and tests
// ---------------------------------------------------------------------------

// srtServerFixture wires a srsSRTProxyServer with fake env, lb, and listener.
// The default listenUDP returns the fixture's blocking listener so tests can
// drive Run() through it; tests that exercise handleClientUDP directly can
// instead set v.listener to f.listener without ever calling Run().
type srtServerFixture struct {
	env      *envfakes.FakeProxyEnvironment
	lb       *lbfakes.FakeOriginLoadBalancer
	listener *blockingUDPListener
	server   *srsSRTProxyServer
}

func newSRTServerFixture() *srtServerFixture {
	f := &srtServerFixture{
		env:      &envfakes.FakeProxyEnvironment{},
		lb:       &lbfakes.FakeOriginLoadBalancer{},
		listener: newBlockingUDPListener(),
	}
	f.env.SRTServerReturns("20080")
	f.server = NewSRSSRTProxyServer(f.env, f.lb, func(v *srsSRTProxyServer) {
		v.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			return f.listener, nil
		}
	})
	return f
}

func TestNewSRSSRTProxyServer_SetsDefaults(t *testing.T) {
	v := NewSRSSRTProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	if v.listenUDP == nil {
		t.Fatal("listenUDP should default to a non-nil factory")
	}
	if v.start.IsZero() {
		t.Fatal("start should be initialized to time.Now()")
	}
}

func TestNewSRSSRTProxyServer_AppliesOptions(t *testing.T) {
	called := false
	listenUDP := func(ctx context.Context, endpoint string) (net.PacketConn, error) {
		called = true
		return nil, errors.New("test")
	}
	v := NewSRSSRTProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{},
		func(s *srsSRTProxyServer) { s.listenUDP = listenUDP })
	_, _ = v.listenUDP(context.Background(), "")
	if !called {
		t.Fatal("expected overridden listenUDP to be invoked")
	}
}

func TestSRSSRTProxyServer_Close_NilListener(t *testing.T) {
	// Close before Run must not panic, must not hang, and must not error.
	v := NewSRSSRTProxyServer(&envfakes.FakeProxyEnvironment{}, &lbfakes.FakeOriginLoadBalancer{})
	done := make(chan error, 1)
	go func() { done <- v.Close() }()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("Close: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Close hung with no listener")
	}
}

func TestSRSSRTProxyServer_Run_ListenError(t *testing.T) {
	envFake := &envfakes.FakeProxyEnvironment{}
	envFake.SRTServerReturns("20080")
	v := NewSRSSRTProxyServer(envFake, &lbfakes.FakeOriginLoadBalancer{}, func(s *srsSRTProxyServer) {
		s.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			return nil, errors.New("permission denied")
		}
	})

	err := v.Run(context.Background())
	if err == nil || !strings.Contains(err.Error(), "listen udp") {
		t.Fatalf("expected listen-udp err, got %v", err)
	}
}

func TestSRSSRTProxyServer_Run_EndpointWithoutColon(t *testing.T) {
	// A bare port like "20080" must be normalized to ":20080".
	envFake := &envfakes.FakeProxyEnvironment{}
	envFake.SRTServerReturns("20080")
	listener := newBlockingUDPListener()
	var captured atomic.Value
	v := NewSRSSRTProxyServer(envFake, &lbfakes.FakeOriginLoadBalancer{}, func(s *srsSRTProxyServer) {
		s.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			captured.Store(endpoint)
			return listener, nil
		}
	})

	if err := v.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	defer v.Close()

	if got := captured.Load(); got != ":20080" {
		t.Fatalf("listenUDP endpoint=%v, want :20080", got)
	}
}

func TestSRSSRTProxyServer_Run_EndpointWithColon(t *testing.T) {
	envFake := &envfakes.FakeProxyEnvironment{}
	envFake.SRTServerReturns("127.0.0.1:20080")
	listener := newBlockingUDPListener()
	var captured atomic.Value
	v := NewSRSSRTProxyServer(envFake, &lbfakes.FakeOriginLoadBalancer{}, func(s *srsSRTProxyServer) {
		s.listenUDP = func(ctx context.Context, endpoint string) (net.PacketConn, error) {
			captured.Store(endpoint)
			return listener, nil
		}
	})

	if err := v.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}
	defer v.Close()

	if got := captured.Load(); got != "127.0.0.1:20080" {
		t.Fatalf("listenUDP endpoint=%v, want 127.0.0.1:20080", got)
	}
}

func TestSRSSRTProxyServer_Run_CloseStopsReadLoop(t *testing.T) {
	// Start Run with an idle listener (no packets queued). The read goroutine
	// blocks in ReadFrom. Close must unblock it via the "closed network
	// connection" error and allow the wait group to drain.
	f := newSRTServerFixture()
	if err := f.server.Run(context.Background()); err != nil {
		t.Fatalf("Run: %v", err)
	}

	done := make(chan error, 1)
	go func() { done <- f.server.Close() }()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("Close: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Close hung — read loop did not exit on listener close")
	}
}

// ---------------------------------------------------------------------------
// srsSRTProxyServer.handleClientUDP — routing only
// ---------------------------------------------------------------------------

// buildNonHandshakeUDPPayload assembles a UDP payload whose first 4 bytes do
// NOT match the SRT handshake magic (so utils.SrtIsHandshake returns false)
// but whose destination socket ID at offset 12..15 equals the given id.
func buildNonHandshakeUDPPayload(destSocketID uint32, tail []byte) []byte {
	out := make([]byte, 16+len(tail))
	// data[0]=0x00 — top bit clear, so SrtIsHandshake is false.
	binary.BigEndian.PutUint32(out[12:16], destSocketID)
	copy(out[16:], tail)
	return out
}

func TestSRSSRTProxyServer_HandleClientUDP_RoutesNonHandshakeToExistingConn(t *testing.T) {
	f := newSRTServerFixture()
	// handleClientUDP wires v.listener into newly-created connections, but for
	// this test the existing conn already has its own backend, so v.listener is
	// only relevant to satisfy the LoadOrStore path (and never read from).
	f.server.listener = f.listener

	backend := newFakeBackendUDP()
	existing := NewSRTConnection(func(c *SRTConnection) {
		c.ctx = logger.WithContext(context.Background())
		c.backendUDP = backend
		c.socketID = 0x12345678
	})
	f.server.sockets.Store(0x12345678, existing)

	payload := buildNonHandshakeUDPPayload(0x12345678, []byte("media-bytes"))
	if err := f.server.handleClientUDP(context.Background(), &net.UDPAddr{}, payload); err != nil {
		t.Fatalf("handleClientUDP err=%v", err)
	}

	select {
	case got := <-backend.writes:
		// The full datagram is forwarded as-is.
		if string(got) != string(payload) {
			t.Fatalf("backend got %q, want %q", got, payload)
		}
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for backend write")
	}
}

func TestSRSSRTProxyServer_HandleClientUDP_HandshakeCreatesConnection(t *testing.T) {
	f := newSRTServerFixture()
	f.server.listener = f.listener

	const srtSocketID uint32 = 0xaabbccdd
	hs0 := newHandshake0(srtSocketID)
	data := marshalOrFatal(t, hs0)
	// hs0 has SocketID(dest)=0 on the wire, so handleClientUDP must fall back
	// to pkt.SRTSocketID to key the sockets map.

	client := &net.UDPAddr{IP: net.ParseIP("1.2.3.4"), Port: 9000}
	if err := f.server.handleClientUDP(context.Background(), client, data); err != nil {
		t.Fatalf("handleClientUDP err=%v", err)
	}

	if _, ok := f.server.sockets.Load(srtSocketID); !ok {
		t.Fatalf("expected sockets map to have entry under 0x%08x", srtSocketID)
	}

	// hs1 reply must have been written back to the client via the listener.
	select {
	case got := <-f.listener.writes:
		if got.addr != client {
			t.Fatalf("listener addr=%v, want %v", got.addr, client)
		}
		parsed := &SRTHandshakePacket{}
		if err := parsed.UnmarshalBinary(got.data); err != nil {
			t.Fatalf("unmarshal hs1: %v", err)
		}
		if parsed.SynCookie != 0x418d5e4e {
			t.Fatalf("hs1 SynCookie=0x%08x, want 0x418d5e4e", parsed.SynCookie)
		}
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for hs1 listener write")
	}
}

func TestSRSSRTProxyServer_HandleClientUDP_BadHandshakeUnmarshalError(t *testing.T) {
	f := newSRTServerFixture()
	f.server.listener = f.listener

	// First 4 bytes match the SRT handshake magic so SrtIsHandshake returns
	// true, but the buffer is shorter than 64 bytes so UnmarshalBinary errors.
	bad := []byte{0x80, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04}
	err := f.server.handleClientUDP(context.Background(), &net.UDPAddr{}, bad)
	if err == nil || !strings.Contains(err.Error(), "Invalid packet length") {
		t.Fatalf("expected unmarshal err, got %v", err)
	}
}
