// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package srt is the suite's SRT client, built on the pure-Go gosrt: a caller
// that publishes or plays a stream by its streamid, a sender that sends an
// MPEG-TS file in SRT messages timed by its PCR, and a player that fills the
// stream report from the TS it gets.
package srt

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"

	gosrt "github.com/datarhei/gosrt"

	"srs-integ/internal/report"
	"srs-integ/internal/ts"
)

// Options are the settings of a connection.
type Options struct {
	// StreamID is the SRTO_STREAMID the caller sends, such as
	// #!::r=live/stream,m=publish; empty sends none.
	StreamID string
	// Passphrase encrypts the connection; empty is no encryption.
	Passphrase string
	// PBKeyLen is the key length in bytes, 16, 24 or 32, with a passphrase;
	// zero is 16.
	PBKeyLen int
	// Latency is the caller's receiver latency and the latency it asks the
	// peer for; zero asks for none, so the peer's own latency applies.
	Latency time.Duration
	// Timeout bounds the handshake; zero is 3s.
	Timeout time.Duration
	// PeerIdleTimeout closes the connection when the peer sends nothing for
	// that long; zero is gosrt's 5s.
	PeerIdleTimeout time.Duration
}

// ParseURL splits an SRT URL, srt://host:port?key=value&..., into the address
// and the options: streamid (taken as it is, so #, !, : and , need no
// escaping, though a %-escaped one is decoded too), passphrase, pbkeylen and
// latency in milliseconds, like srt-live-transmit. Other keys, such as mode,
// are ignored: the client is always a caller.
func ParseURL(u string) (addr string, opts Options, err error) {
	rest, ok := strings.CutPrefix(u, "srt://")
	if !ok {
		return "", opts, fmt.Errorf("%q is not an srt:// URL", u)
	}
	hostport, query, _ := strings.Cut(rest, "?")
	hostport = strings.TrimSuffix(hostport, "/")
	host, port, err := net.SplitHostPort(hostport)
	if err != nil || host == "" || port == "" {
		return "", opts, fmt.Errorf("%q has no host and port", u)
	}

	for _, kv := range strings.Split(query, "&") {
		if kv == "" {
			continue
		}
		k, v, _ := strings.Cut(kv, "=")
		if strings.Contains(v, "%") {
			if v, err = url.QueryUnescape(v); err != nil {
				return "", opts, fmt.Errorf("%q: %v: %w", u, k, err)
			}
		}
		switch k {
		case "streamid":
			opts.StreamID = v
		case "passphrase":
			opts.Passphrase = v
		case "pbkeylen":
			n, err := strconv.Atoi(v)
			if err != nil || (n != 16 && n != 24 && n != 32) {
				return "", opts, fmt.Errorf("%q: pbkeylen must be 16, 24 or 32", u)
			}
			opts.PBKeyLen = n
		case "latency":
			ms, err := strconv.Atoi(v)
			if err != nil || ms < 0 {
				return "", opts, fmt.Errorf("%q: latency must be milliseconds", u)
			}
			opts.Latency = time.Duration(ms) * time.Millisecond
		}
	}
	return net.JoinHostPort(host, port), opts, nil
}

// A Conn is an SRT connection in live mode.
type Conn struct {
	c gosrt.Conn
}

// Dial connects to an SRT listener as a caller. A listener that refuses the
// handshake gives a *RejectError.
func Dial(ctx context.Context, addr string, opts Options) (*Conn, error) {
	cfg := gosrt.DefaultConfig()
	cfg.StreamId = opts.StreamID
	cfg.Passphrase = opts.Passphrase
	if opts.PBKeyLen != 0 {
		cfg.PBKeylen = opts.PBKeyLen
	}
	cfg.Latency = opts.Latency
	if opts.PeerIdleTimeout != 0 {
		cfg.PeerIdleTimeout = opts.PeerIdleTimeout
	}
	cfg.ConnectionTimeout = opts.Timeout
	if cfg.ConnectionTimeout == 0 {
		cfg.ConnectionTimeout = 3 * time.Second
	}
	if d, ok := ctx.Deadline(); ok && time.Until(d) < cfg.ConnectionTimeout {
		cfg.ConnectionTimeout = max(time.Until(d), time.Millisecond)
	}

	if err := ctx.Err(); err != nil {
		return nil, err
	}
	c, err := gosrt.Dial("srt", addr, cfg)
	if err != nil {
		return nil, rejectOf(err)
	}
	return &Conn{c: c}, nil
}

// rejectedRE is how gosrt reports a refused handshake: the handshake type, in
// base 32, is the reason.
var rejectedRE = regexp.MustCompile(`connection rejected: REJECT \(([0-9a-v]+)\)`)

// rejectOf returns a *RejectError for gosrt's error of a refused handshake,
// and err otherwise.
func rejectOf(err error) error {
	m := rejectedRE.FindStringSubmatch(err.Error())
	if m == nil {
		return err
	}
	reason, perr := strconv.ParseUint(m[1], 32, 32)
	if perr != nil {
		return err
	}
	return &RejectError{Reason: int(reason)}
}

// Read reads the next message, up to 1316 bytes.
func (c *Conn) Read(p []byte) (int, error) { return c.c.Read(p) }

// Write sends p as one message, at most 1316 bytes.
func (c *Conn) Write(p []byte) (int, error) { return c.c.Write(p) }

// Close closes the connection, telling the peer.
func (c *Conn) Close() error { return c.c.Close() }

// TSBPD returns the TSBPD delays the handshake agreed: recv, how long this end
// holds what it receives, and send, how long the peer holds what this end
// sends, which is the peer's receive latency.
func (c *Conn) TSBPD() (recv, send time.Duration) {
	var s gosrt.Statistics
	c.c.Stats(&s)
	return time.Duration(s.Instantaneous.MsRecvTsbPdDelay) * time.Millisecond,
		time.Duration(s.Instantaneous.MsSendTsbPdDelay) * time.Millisecond
}

// WaitClosed reads and drops what the peer sends until it closes the
// connection, and returns nil then, or the error of ctx, when it closes the
// connection itself: gosrt's read deadline doesn't end a read.
func (c *Conn) WaitClosed(ctx context.Context) error {
	after := context.AfterFunc(ctx, func() { c.c.Close() })
	defer after()
	b := make([]byte, 2048)
	for {
		if _, err := c.c.Read(b); err != nil {
			if ctx.Err() != nil {
				return ctx.Err()
			}
			return nil
		}
	}
}

// WaitClosedProbing is WaitClosed for a peer whose shutdown the client may
// miss: gosrt drops one that arrives while Dial is still returning. It probes
// the peer every interval, as Probe does.
func (c *Conn) WaitClosedProbing(ctx context.Context, every time.Duration) error {
	probe, stop := context.WithCancel(ctx)
	defer stop()
	go c.Probe(probe, every)
	return c.WaitClosed(ctx)
}

// Probe sends a null TS packet every interval until ctx ends or a write
// fails. A live peer acknowledges it, so with a PeerIdleTimeout the
// connection stays while the peer is there, even if it sends nothing, and a
// peer that closed, whose shutdown the client missed, ends it by its silence.
func (c *Conn) Probe(ctx context.Context, every time.Duration) {
	null := make([]byte, ts.PacketSize)
	null[0], null[1], null[2], null[3] = 0x47, 0x1f, 0xff, 0x10
	t := time.NewTicker(every)
	defer t.Stop()
	for {
		if _, err := c.c.Write(null); err != nil {
			return
		}
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}
	}
}

// Analyze reads the TS stream the peer sends and fills the report until u, or
// until ctx ends; it closes the connection when ctx ends. It always returns
// the report, partial when it fails.
func (c *Conn) Analyze(ctx context.Context, u report.Until) (*report.Report, error) {
	after := context.AfterFunc(ctx, func() { c.c.Close() })
	defer after()
	r, err := ts.Analyze(ts.NewDemuxer(&messages{c: c.c}), u)
	if err != nil && ctx.Err() != nil {
		return r, fmt.Errorf("%w before %v: %v", ctx.Err(), u, r)
	}
	return r, err
}

// messages reads the stream of messages as bytes: a read that asks for less
// than a message gets the rest on the next reads, so nothing is lost.
type messages struct {
	c    gosrt.Conn
	buf  []byte
	left []byte
}

func (m *messages) Read(p []byte) (int, error) {
	if len(m.left) == 0 {
		if m.buf == nil {
			m.buf = make([]byte, 2048)
		}
		n, err := m.c.Read(m.buf)
		if err != nil {
			if errors.Is(err, io.EOF) {
				return 0, io.EOF
			}
			return 0, err
		}
		m.left = m.buf[:n]
	}
	n := copy(p, m.left)
	m.left = m.left[n:]
	return n, nil
}

// Play connects to addr and analyzes the stream until u, or until ctx ends.
func Play(ctx context.Context, addr string, opts Options, u report.Until) (*report.Report, error) {
	c, err := Dial(ctx, addr, opts)
	if err != nil {
		return nil, err
	}
	defer c.Close()
	return c.Analyze(ctx, u)
}

// The reasons a listener refuses a handshake, from libsrt's SRT_REJECT_REASON.
const (
	RejectUnknown    = 1000
	RejectSystem     = 1001
	RejectPeer       = 1002
	RejectResource   = 1003
	RejectRogue      = 1004
	RejectBacklog    = 1005
	RejectIPE        = 1006
	RejectClose      = 1007
	RejectVersion    = 1008
	RejectRdvCookie  = 1009
	RejectBadSecret  = 1010
	RejectUnsecure   = 1011
	RejectMessageAPI = 1012
	RejectCongestion = 1013
	RejectFilter     = 1014
	RejectGroup      = 1015
	RejectTimeout    = 1016
)

var rejectNames = map[int]string{
	RejectUnknown: "unknown", RejectSystem: "system", RejectPeer: "peer", RejectResource: "resource",
	RejectRogue: "rogue", RejectBacklog: "backlog", RejectIPE: "internal error", RejectClose: "close",
	RejectVersion: "version", RejectRdvCookie: "rendezvous cookie", RejectBadSecret: "bad secret",
	RejectUnsecure: "unsecure", RejectMessageAPI: "message API", RejectCongestion: "congestion",
	RejectFilter: "filter", RejectGroup: "group", RejectTimeout: "timeout",
}

// A RejectError is a handshake the listener refused, with its reason.
type RejectError struct {
	Reason int
}

func (e *RejectError) Error() string {
	if name, ok := rejectNames[e.Reason]; ok {
		return fmt.Sprintf("SRT handshake rejected: %v (%v)", e.Reason, name)
	}
	return fmt.Sprintf("SRT handshake rejected: %v", e.Reason)
}

// MessageSize is the payload of an SRT message in live mode: 7 TS packets.
const MessageSize = 7 * ts.PacketSize

// A Sender sends an MPEG-TS stream in messages of 7 packets, each timed by
// the latest PCR up to its end, since the first PCR of the stream.
type Sender struct {
	r io.Reader
	// next is the message read ahead, and at its media time.
	next   []byte
	at     time.Duration
	eof    bool
	pcr    int64 // the latest PCR, in 90 kHz units; -1 before the first
	first  int64
	msgs   int
	nbytes int
}

// NewSender reads the TS stream from r.
func NewSender(r io.Reader) *Sender { return &Sender{r: r, pcr: -1} }

// peek reads the next message ahead, if it hasn't; it returns io.EOF at the
// end of the stream.
func (s *Sender) peek() error {
	if s.next != nil {
		return nil
	}
	if s.eof {
		return io.EOF
	}
	msg := make([]byte, 0, MessageSize)
	p := make([]byte, ts.PacketSize)
	for len(msg) < MessageSize {
		if _, err := io.ReadFull(s.r, p); err == io.EOF {
			s.eof = true
			break
		} else if err == io.ErrUnexpectedEOF {
			return fmt.Errorf("TS packet %v: the stream ends in the middle of it", s.nbytes/ts.PacketSize+len(msg)/ts.PacketSize)
		} else if err != nil {
			return err
		}
		if p[0] != 0x47 {
			return fmt.Errorf("TS packet %v: lost sync, %#x instead of 0x47", s.nbytes/ts.PacketSize+len(msg)/ts.PacketSize, p[0])
		}
		if pcr, ok := pcrOf(p); ok {
			if s.pcr < 0 {
				s.first = pcr
			}
			s.pcr = pcr
		}
		msg = append(msg, p...)
	}
	if len(msg) == 0 {
		return io.EOF
	}
	s.next = msg
	s.at = 0
	if s.pcr >= 0 {
		s.at = time.Duration(s.pcr-s.first) * time.Second / 90000
	}
	return nil
}

// pcrOf returns the PCR base of a TS packet, in 90 kHz units, if it has one.
func pcrOf(p []byte) (int64, bool) {
	if p[3]&0x20 == 0 || p[4] < 7 || p[5]&0x10 == 0 {
		return 0, false
	}
	b := p[6:12]
	return int64(b[0])<<25 | int64(b[1])<<17 | int64(b[2])<<9 | int64(b[3])<<1 | int64(b[4]>>7), true
}

// send writes the message read ahead.
func (s *Sender) send(w io.Writer) error {
	if _, err := w.Write(s.next); err != nil {
		return err
	}
	s.msgs++
	s.nbytes += len(s.next)
	s.next = nil
	return nil
}

// SendThrough sends, at once, the messages before media time d. It returns
// nil when it reaches d or the end.
func (s *Sender) SendThrough(w io.Writer, d time.Duration) error {
	for {
		if err := s.peek(); err == io.EOF {
			return nil
		} else if err != nil {
			return err
		}
		if s.at >= d {
			return nil
		}
		if err := s.send(w); err != nil {
			return err
		}
	}
}

// Stream sends the rest of the stream in real time, each message when as much
// wall time has passed since Stream started as media time since the first
// message it sends, until the end or ctx. It returns nil at the end. Now and
// sleep are the clock; nil is the real one.
func (s *Sender) Stream(ctx context.Context, w io.Writer, now func() time.Time, sleep func(context.Context, time.Duration) error) error {
	if now == nil {
		now = time.Now
	}
	if sleep == nil {
		sleep = sleepCtx
	}
	start, base, started := now(), time.Duration(0), false
	for {
		if err := ctx.Err(); err != nil {
			return err
		}
		if err := s.peek(); err == io.EOF {
			return nil
		} else if err != nil {
			return err
		}
		if !started {
			started, base = true, s.at
		}
		if d := start.Add(s.at - base).Sub(now()); d > 0 {
			if err := sleep(ctx, d); err != nil {
				return err
			}
		}
		if err := s.send(w); err != nil {
			return err
		}
	}
}

func sleepCtx(ctx context.Context, d time.Duration) error {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-t.C:
		return nil
	}
}

// Sent is the number of messages and bytes sent so far.
func (s *Sender) Sent() (messages, bytes int) { return s.msgs, s.nbytes }
