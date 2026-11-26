// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"context"
	"fmt"
	"io"
	"regexp"
	"strconv"
	"strings"
	"time"

	gosrt "github.com/datarhei/gosrt"
	zsrt "github.com/zsiec/srtgo"
)

// A Client is an SRT library the suite calls a listener with. Its Conn
// publishes TS with Publish or a Sender, plays it with Analyze, and closes.
type Client interface {
	// Name is the library's short name, such as gosrt.
	Name() string
	// Dial connects to an SRT listener as a caller. A listener that refuses
	// the handshake gives a *RejectError; an option the library lacks, or a
	// value it can't take, gives an error naming the option.
	Dial(ctx context.Context, addr string, opts Options) (*Conn, error)
}

var (
	// GoSRT is datarhei/gosrt, pure Go: the suite's main client.
	GoSRT Client = gosrtClient{}
	// Zsiec is zsiec/srtgo, pure Go.
	Zsiec Client = zsiecClient{}
)

// Clients are the libraries a case can pick, for a matrix of cases.
func Clients() []Client { return []Client{GoSRT, Zsiec, Libsrt} }

// ClientNamed returns the client of a library by its name: one of Clients,
// FFmpeg or LiveTransmit.
func ClientNamed(name string) (Client, error) {
	var names []string
	for _, cl := range append(Clients(), FFmpeg, LiveTransmit) {
		if cl.Name() == name {
			return cl, nil
		}
		names = append(names, cl.Name())
	}
	return nil, fmt.Errorf("no SRT client %q, only %v", name, strings.Join(names, ", "))
}

// A link is one library's connection: Read and Write move one message, and
// Close ends a Read that waits.
type link interface {
	Read(p []byte) (int, error)
	Write(p []byte) (int, error)
	Close() error
	tsbpd() (recv, send time.Duration)
	negotiated() (mss, flowWindow int)
}

// Publish sends a TS stream from r: lead of it, by its PCR, at once, then the
// rest in real time, until the end, when it returns nil, or until ctx ends.
func (c *Conn) Publish(ctx context.Context, r io.Reader, lead time.Duration) error {
	s := NewSender(r)
	if err := s.SendThrough(c, lead); err != nil {
		return err
	}
	return s.Stream(ctx, c, nil, nil)
}

// checkOptions checks the options every library takes the same.
func checkOptions(opts Options) error {
	if opts.Passphrase != "" && opts.PBKeyLen != 0 && opts.PBKeyLen != 16 && opts.PBKeyLen != 24 && opts.PBKeyLen != 32 {
		return fmt.Errorf("pbkeylen %v, want 16, 24 or 32", opts.PBKeyLen)
	}
	return nil
}

// handshakeTimeout is the handshake's timeout of opts, at most what is left of
// ctx.
func handshakeTimeout(ctx context.Context, opts Options) time.Duration {
	d := opts.Timeout
	if d == 0 {
		d = 3 * time.Second
	}
	if dl, ok := ctx.Deadline(); ok && time.Until(dl) < d {
		d = max(time.Until(dl), time.Millisecond)
	}
	return d
}

// recvPeerLatency returns the receive and peer latencies of opts, when either
// is set.
func recvPeerLatency(opts Options) (recv, peer time.Duration, ok bool) {
	if opts.RecvLatency == 0 && opts.PeerLatency == 0 {
		return 0, 0, false
	}
	recv, peer = opts.RecvLatency, opts.PeerLatency
	if recv == 0 {
		recv = opts.Latency
	}
	if peer == 0 {
		peer = opts.Latency
	}
	return recv, peer, true
}

type gosrtClient struct{}

func (gosrtClient) Name() string { return "gosrt" }

func (gosrtClient) Dial(ctx context.Context, addr string, opts Options) (*Conn, error) {
	if err := checkOptions(opts); err != nil {
		return nil, err
	}
	cfg := gosrt.DefaultConfig()
	cfg.StreamId = opts.StreamID
	cfg.Passphrase = opts.Passphrase
	if opts.PBKeyLen != 0 {
		cfg.PBKeylen = opts.PBKeyLen
	}
	// gosrt's Latency, unless negative, sets both of the others.
	cfg.Latency = opts.Latency
	if recv, peer, ok := recvPeerLatency(opts); ok {
		cfg.Latency, cfg.ReceiverLatency, cfg.PeerLatency = -1, recv, peer
	}
	if opts.PeerIdleTimeout != 0 {
		cfg.PeerIdleTimeout = opts.PeerIdleTimeout
	}
	if opts.PayloadSize != 0 {
		cfg.PayloadSize = uint32(opts.PayloadSize)
	}
	if opts.MSS != 0 {
		cfg.MSS = uint32(opts.MSS)
	}
	if opts.MaxBW != 0 {
		cfg.MaxBW = opts.MaxBW
	}
	if opts.SendBuf != 0 {
		cfg.SendBufferSize = uint32(opts.SendBuf)
	}
	if opts.RecvBuf != 0 {
		cfg.ReceiverBufferSize = uint32(opts.RecvBuf)
	}
	if opts.TSBPDMode != nil {
		cfg.TSBPDMode = *opts.TSBPDMode
	}
	if opts.TLPktDrop != nil {
		cfg.TooLatePacketDrop = *opts.TLPktDrop
	}
	cfg.ConnectionTimeout = handshakeTimeout(ctx, opts)

	if err := ctx.Err(); err != nil {
		return nil, err
	}
	c, err := gosrt.Dial("srt", addr, cfg)
	if err != nil {
		return nil, gosrtRejectOf(err)
	}
	return &Conn{c: gosrtLink{c}, client: "gosrt"}, nil
}

// gosrtRejectedRE is how gosrt reports a refused handshake: the handshake
// type, in base 32, is the reason.
var gosrtRejectedRE = regexp.MustCompile(`connection rejected: REJECT \(([0-9a-v]+)\)`)

// gosrtRejectOf returns a *RejectError for gosrt's error of a refused
// handshake, and err otherwise.
func gosrtRejectOf(err error) error {
	m := gosrtRejectedRE.FindStringSubmatch(err.Error())
	if m == nil {
		return err
	}
	reason, perr := strconv.ParseUint(m[1], 32, 32)
	if perr != nil {
		return err
	}
	return &RejectError{Reason: int(reason)}
}

type gosrtLink struct{ gosrt.Conn }

// gosrt keeps no flow window of the peer: its PktFlowWindow is its own.
func (l gosrtLink) negotiated() (mss, flowWindow int) {
	var s gosrt.Statistics
	l.Stats(&s)
	return int(s.Instantaneous.ByteMSS), 0
}

func (l gosrtLink) tsbpd() (recv, send time.Duration) {
	var s gosrt.Statistics
	l.Stats(&s)
	return time.Duration(s.Instantaneous.MsRecvTsbPdDelay) * time.Millisecond,
		time.Duration(s.Instantaneous.MsSendTsbPdDelay) * time.Millisecond
}

type zsiecClient struct{}

func (zsiecClient) Name() string { return "zsiec" }

// zsiecPacket is the payload of a buffer's packet, as libsrt counts it, MSS
// less the IP and UDP headers: zsiec sizes its buffers in packets.
func zsiecPacket(mss int) int {
	if mss == 0 {
		mss = zsrt.DefaultMSS
	}
	return mss - 28
}

func (zsiecClient) Dial(ctx context.Context, addr string, opts Options) (*Conn, error) {
	if err := checkOptions(opts); err != nil {
		return nil, err
	}
	if opts.TSBPDMode != nil {
		return nil, fmt.Errorf("zsiec has no tsbpdmode option: always on in live mode")
	}
	if opts.TLPktDrop != nil {
		return nil, fmt.Errorf("zsiec has no tlpktdrop option: always on in live mode")
	}
	cfg := zsrt.DefaultConfig()
	cfg.StreamID = opts.StreamID
	cfg.Passphrase = opts.Passphrase
	cfg.KeyLength = opts.PBKeyLen
	if opts.Latency != 0 {
		cfg.Latency = opts.Latency
	}
	if recv, peer, ok := recvPeerLatency(opts); ok {
		cfg.RecvLatency, cfg.PeerLatency = recv, peer
	}
	if opts.PeerIdleTimeout != 0 {
		cfg.PeerIdleTimeout = opts.PeerIdleTimeout
	}
	cfg.PayloadSize = opts.PayloadSize
	if opts.MSS != 0 {
		cfg.MSS = opts.MSS
	}
	if opts.MaxBW != 0 {
		cfg.MaxBW = opts.MaxBW
	}
	if opts.SendBuf != 0 {
		cfg.SendBufSize = opts.SendBuf / zsiecPacket(opts.MSS)
	}
	if opts.RecvBuf != 0 {
		cfg.RecvBufSize = opts.RecvBuf / zsiecPacket(opts.MSS)
	}
	// Close at once, as gosrt does, not after draining for up to 1s.
	cfg.Linger = -1
	cfg.ConnTimeout = handshakeTimeout(ctx, opts)

	if err := ctx.Err(); err != nil {
		return nil, err
	}
	c, err := zsrt.Dial(addr, cfg)
	if err != nil {
		return nil, zsiecRejectOf(err)
	}
	return &Conn{c: zsiecLink{c}, client: "zsiec"}, nil
}

// zsiecRejectedRE is how zsiec reports a refused handshake, with the reason
// in decimal.
var zsiecRejectedRE = regexp.MustCompile(`connection rejected \(code ([0-9]+)\)`)

// zsiecRejectOf returns a *RejectError for zsiec's error of a refused
// handshake, and err otherwise.
func zsiecRejectOf(err error) error {
	m := zsiecRejectedRE.FindStringSubmatch(err.Error())
	if m == nil {
		return err
	}
	reason, perr := strconv.Atoi(m[1])
	if perr != nil {
		return err
	}
	return &RejectError{Reason: reason}
}

type zsiecLink struct{ *zsrt.Conn }

func (l zsiecLink) negotiated() (mss, flowWindow int) {
	s := l.Stats(false)
	return s.NegotiatedMSS, s.FlowWindow
}

func (l zsiecLink) tsbpd() (recv, send time.Duration) {
	// zsiec leaves MsRcvTsbPdDelay and MsSndTsbPdDelay at zero.
	s := l.Stats(false)
	return s.NegotiatedLatency, s.PeerLatency
}
