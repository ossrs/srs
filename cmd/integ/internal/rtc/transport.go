// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"bufio"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"net/netip"
	"strings"
	"sync"
	"time"

	"github.com/pion/stun/v4"
	"github.com/pion/webrtc/v4"
)

// ErrClosed tells that the server closed the connection.
var ErrClosed = errors.New("the server closed the connection")

// A Binding is a STUN binding the server answered: the client's address,
// and the address the server saw, from the answer's XOR-MAPPED-ADDRESS.
type Binding struct {
	Local, Mapped netip.AddrPort
}

// Bind sends STUN binding requests for the session of an offer and its
// answer, as the client that made the offer, from a new socket, on
// opts.LocalPort when it is set, to the answer's loopback candidate: over TCP when opts.TCP is set and UDP
// otherwise, as an ICE-controlled agent when opts.Controlled is set. It
// returns the binding, ErrClosed when the server closes the connection, or
// the ctx error when no answer comes. The socket is closed when it returns:
// over TCP, a binding closes its end first and waits, until ctx ends, for
// the server to close the connection too, so the server has let it go.
func Bind(ctx context.Context, offer, answer string, opts *Options) (*Binding, error) {
	s := &RawSession{Session: Session{Offer: offer, Answer: answer, opts: opts}}
	defer s.Close()
	var err error
	if s.offer, err = parseAnswer(offer); err != nil {
		return nil, fmt.Errorf("offer: %w", err)
	}
	if s.answer, err = parseAnswer(answer); err != nil {
		return nil, fmt.Errorf("answer: %w", err)
	}
	if err := s.dial(ctx); err != nil {
		return nil, err
	}
	res, err := s.bind(ctx)
	if err != nil {
		return nil, err
	}
	var xa stun.XORMappedAddress
	if err := xa.GetFrom(res); err != nil {
		return nil, fmt.Errorf("the answer has no XOR-MAPPED-ADDRESS: %w", err)
	}
	b := &Binding{}
	if ip, ok := netip.AddrFromSlice(xa.IP); ok {
		b.Mapped = netip.AddrPortFrom(ip.Unmap(), uint16(xa.Port))
	}
	b.Local, _ = netip.ParseAddrPort(s.conn.LocalAddr().String())
	s.awaitClose(ctx)
	return b, nil
}

// Intrude sends STUN binding requests for the session of an offer and its
// answer, as Bind does from a new socket, every 50ms until ctx ends,
// and returns how many RTP and RTCP packets came to that socket: a server
// that moves the session there sends it the session's media.
func Intrude(ctx context.Context, offer, answer string, opts *Options) (int, error) {
	s := &RawSession{Session: Session{Offer: offer, Answer: answer, opts: opts}}
	defer s.Close()
	var err error
	if s.offer, err = parseAnswer(offer); err != nil {
		return 0, fmt.Errorf("offer: %w", err)
	}
	if s.answer, err = parseAnswer(answer); err != nil {
		return 0, fmt.Errorf("answer: %w", err)
	}
	if err := s.dial(ctx); err != nil {
		return 0, err
	}
	media := 0
	tick := time.NewTicker(50 * time.Millisecond)
	defer tick.Stop()
	for {
		req, err := bindingRequest(s.answer.ufrag, s.offer.ufrag, s.answer.pwd, false)
		if err != nil {
			return media, err
		}
		if _, err := s.conn.Write(req.Raw); err != nil {
			return media, err
		}
	wait:
		for {
			select {
			case <-s.mux.media:
				media++
			case <-s.mux.stun:
			case <-tick.C:
				break wait
			case <-ctx.Done():
				return media, nil
			}
		}
	}
}

// awaitClose closes the write side of a TCP connection and waits, until ctx
// ends, for the server to close the connection.
func (s *RawSession) awaitClose(ctx context.Context) {
	fc, ok := s.conn.(*framedConn)
	if !ok {
		return
	}
	cw, ok := fc.Conn.(interface{ CloseWrite() error })
	if !ok || cw.CloseWrite() != nil {
		return
	}
	select {
	case <-s.mux.done:
	case <-ctx.Done():
	}
}

// dial connects the session's socket to the answer's loopback candidate,
// over TCP or UDP as the options say.
func (s *RawSession) dial(ctx context.Context) error {
	network, candidates := "udp4", s.answer.candidates
	if s.opts != nil && s.opts.TCP {
		network, candidates = "tcp4", s.answer.tcpCandidates
	}
	to, ok := pickCandidate(candidates)
	if !ok {
		return fmt.Errorf("the answer has no IPv4 %v candidate", network[:3])
	}
	var d net.Dialer
	if s.opts != nil && s.opts.LocalPort != 0 {
		if network == "tcp4" {
			d.LocalAddr = &net.TCPAddr{Port: int(s.opts.LocalPort)}
		} else {
			d.LocalAddr = &net.UDPAddr{Port: int(s.opts.LocalPort)}
		}
	}
	conn, err := d.DialContext(ctx, network, to.String())
	if err != nil {
		return err
	}
	if network == "tcp4" {
		conn = newFramedConn(conn)
	}
	s.conn, s.mux = conn, newMux(conn)
	return nil
}

// pickCandidate returns the loopback IPv4 candidate, where the test servers
// are, or the first IPv4 one.
func pickCandidate(candidates []netip.AddrPort) (netip.AddrPort, bool) {
	var to netip.AddrPort
	for _, c := range candidates {
		if c.Addr().Is4() && (!to.IsValid() || c.Addr().IsLoopback() && !to.Addr().IsLoopback()) {
			to = c
		}
	}
	return to, to.IsValid()
}

// A framedConn is a TCP connection that carries packets, each after its
// length in 2 bytes, as RFC 4571 says for RTP and ICE over TCP. Each Read
// returns one packet, and each Write sends one.
type framedConn struct {
	net.Conn
	r  *bufio.Reader
	mu sync.Mutex
}

func newFramedConn(c net.Conn) *framedConn {
	return &framedConn{Conn: c, r: bufio.NewReader(c)}
}

// Read reads one packet. A packet longer than b is an error, and the rest of
// the stream can't be read.
func (c *framedConn) Read(b []byte) (int, error) {
	var h [2]byte
	if _, err := io.ReadFull(c.r, h[:]); err != nil {
		return 0, err
	}
	n := int(binary.BigEndian.Uint16(h[:]))
	if n > len(b) {
		return 0, fmt.Errorf("a packet of %v bytes, longer than the buffer of %v: %w", n, len(b), io.ErrShortBuffer)
	}
	if _, err := io.ReadFull(c.r, b[:n]); err != nil {
		return 0, err
	}
	return n, nil
}

// Write sends b as one packet.
func (c *framedConn) Write(b []byte) (int, error) {
	if len(b) > 0xffff {
		return 0, fmt.Errorf("a packet of %v bytes doesn't fit the 2-byte length", len(b))
	}
	p := make([]byte, 2+len(b))
	binary.BigEndian.PutUint16(p, uint16(len(b)))
	copy(p[2:], b)
	c.mu.Lock()
	defer c.mu.Unlock()
	if _, err := c.Conn.Write(p); err != nil {
		return 0, err
	}
	return len(b), nil
}

// Abort closes the session's socket without a DTLS close_notify alert, as a
// client that is gone.
func (s *RawSession) Abort() {
	if s.mux != nil {
		s.mux.close()
	}
}

// withSetup returns the offer with the DTLS setup in place of actpass, or
// the offer as it is when setup is empty.
func withSetup(offer, setup string) string {
	if setup == "" {
		return offer
	}
	return strings.ReplaceAll(offer, "a=setup:actpass", "a=setup:"+setup)
}

func (o *Options) setup() string {
	if o == nil {
		return ""
	}
	return o.Setup
}

// Transport is the protocol, udp or tcp, of the local candidate of the pair
// ICE selected, or "" before it selects one.
func (s *Session) Transport() string {
	if s.PC == nil {
		return ""
	}
	for _, tr := range s.PC.GetTransceivers() {
		var dt *webrtc.DTLSTransport
		if r := tr.Receiver(); r != nil {
			dt = r.Transport()
		}
		if dt == nil && tr.Sender() != nil {
			dt = tr.Sender().Transport()
		}
		if dt == nil {
			continue
		}
		if pair, err := dt.ICETransport().GetSelectedCandidatePair(); err == nil && pair != nil {
			return pair.Local.Protocol.String()
		}
	}
	return ""
}
