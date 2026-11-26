// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Command srt-libsrt is an SRT peer in live mode on its own libsrt, for the
// SRS tests: it publishes the TS of its stdin, plays to its stdout, or listens
// for one caller, and writes its events and statistics as JSON lines to
// stderr. See README.md.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"strings"
	"sync/atomic"
	"syscall"
	"time"
)

/*
#include <unistd.h>
#include <srt/srt.h>
*/
import "C"

const usage = `Usage: srt-libsrt <command> [options] <host:port>

Commands:
  publish   call host:port, send stdin to the peer and what the peer sends to
            stdout; end at the end of stdin, once the peer has it all
  play      call host:port and write what the peer sends to stdout
  listen    listen on host:port, port 0 for any, for one caller, then as publish

Each ends when the peer closes, or on SIGINT or SIGTERM, too. Events go to
stderr as JSON lines: listening, connected, then closed with the statistics,
or error. The exit code is 0 after closed, 1 after error, 2 for bad usage.

Options, as libsrt's SRTO_ names; times in milliseconds, sizes in bytes:
`

// An event is one JSON line on stderr.
type event struct {
	Event           string `json:"event"`
	Addr            string `json:"addr,omitempty"`
	Local           string `json:"local,omitempty"`
	Peer            string `json:"peer,omitempty"`
	StreamID        string `json:"streamid,omitempty"`
	MsRcvTsbPdDelay int    `json:"msRcvTsbPdDelay,omitempty"`
	MsSndTsbPdDelay int    `json:"msSndTsbPdDelay,omitempty"`
	MSS             int    `json:"mss,omitempty"`
	FlowWindow      int    `json:"flowWindow,omitempty"`
	Reason          string `json:"reason,omitempty"`
	MsgSent         int64  `json:"msgSent,omitempty"`
	MsgRecv         int64  `json:"msgRecv,omitempty"`
	Stats           *Stats `json:"stats,omitempty"`
	Error           string `json:"error,omitempty"`
	Reject          int    `json:"reject,omitempty"`
}

func main() {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	code := run(ctx, os.Args[1:], os.Stdin, os.Stdout, os.Stderr)
	stop()
	// _exit, not exit: libsrt's static destructor waits up to 1s for its
	// garbage collector thread, which every caller's close then pays.
	C._exit(C.int(code))
}

// The options of a socket, in the order they are set: MSS before the payload
// size it bounds, the latency before the two it sets.
var (
	optMSS           = option{"mss", C.SRTO_MSS}
	optPayloadSize   = option{"payloadsize", C.SRTO_PAYLOADSIZE}
	optSndBuf        = option{"sndbuf", C.SRTO_SNDBUF}
	optRcvBuf        = option{"rcvbuf", C.SRTO_RCVBUF}
	optMaxBW         = option{"maxbw", C.SRTO_MAXBW}
	optLatency       = option{"latency", C.SRTO_LATENCY}
	optRcvLatency    = option{"rcvlatency", C.SRTO_RCVLATENCY}
	optPeerLatency   = option{"peerlatency", C.SRTO_PEERLATENCY}
	optConnTimeo     = option{"conntimeo", C.SRTO_CONNTIMEO}
	optPeerIdleTimeo = option{"peeridletimeo", C.SRTO_PEERIDLETIMEO}
	optTSBPDMode     = option{"tsbpdmode", C.SRTO_TSBPDMODE}
	optTLPktDrop     = option{"tlpktdrop", C.SRTO_TLPKTDROP}
	optPassphrase    = option{"passphrase", C.SRTO_PASSPHRASE}
	optPBKeyLen      = option{"pbkeylen", C.SRTO_PBKEYLEN}
	optStreamID      = option{"streamid", C.SRTO_STREAMID}
)

var intOptions = []option{optMSS, optPayloadSize, optSndBuf, optRcvBuf}
var timeOptions = []option{optLatency, optRcvLatency, optPeerLatency, optConnTimeo, optPeerIdleTimeo}
var boolOptions = []option{optTSBPDMode, optTLPktDrop}

// onOff is a flag of on or off.
type onOff struct{ v bool }

func (o *onOff) String() string {
	if o.v {
		return "on"
	}
	return "off"
}

func (o *onOff) Set(s string) error {
	switch s {
	case "on":
		o.v = true
	case "off":
		o.v = false
	default:
		return errors.New("want on or off")
	}
	return nil
}

// config is the command line of one run.
type config struct {
	command string
	addr    *net.UDPAddr
	ints    map[string]*int
	maxbw   *int64
	bools   map[string]*onOff
	strs    map[string]*string
	set     map[string]bool
}

func parse(args []string, stderr io.Writer) (*config, error) {
	if len(args) == 0 {
		return nil, errors.New("no command")
	}
	c := &config{command: args[0], ints: map[string]*int{}, bools: map[string]*onOff{}, strs: map[string]*string{}, set: map[string]bool{}}
	switch c.command {
	case "publish", "play", "listen":
	default:
		return nil, fmt.Errorf("unknown command %q", c.command)
	}

	fs := flag.NewFlagSet(c.command, flag.ContinueOnError)
	fs.SetOutput(stderr)
	fs.Usage = func() {
		fmt.Fprint(stderr, usage)
		fs.PrintDefaults()
	}
	for _, o := range append(append([]option{}, intOptions...), timeOptions...) {
		c.ints[o.name] = fs.Int(o.name, 0, "SRTO_"+strings.ToUpper(o.name))
	}
	c.ints[optPBKeyLen.name] = fs.Int(optPBKeyLen.name, 0, "SRTO_PBKEYLEN: 16, 24 or 32")
	c.maxbw = fs.Int64(optMaxBW.name, 0, "SRTO_MAXBW, bytes per second")
	for _, o := range boolOptions {
		c.bools[o.name] = &onOff{}
		fs.Var(c.bools[o.name], o.name, "SRTO_"+strings.ToUpper(o.name)+": on or off")
	}
	c.strs[optPassphrase.name] = fs.String(optPassphrase.name, "", "SRTO_PASSPHRASE")
	c.strs[optStreamID.name] = fs.String(optStreamID.name, "", "SRTO_STREAMID")
	if err := fs.Parse(args[1:]); err != nil {
		return nil, err
	}
	fs.Visit(func(f *flag.Flag) { c.set[f.Name] = true })

	if fs.NArg() != 1 {
		fs.Usage()
		return nil, errors.New("want one host:port")
	}
	addr, err := net.ResolveUDPAddr("udp", fs.Arg(0))
	if err != nil {
		return nil, err
	}
	if addr.IP == nil {
		addr.IP = net.IPv4zero
	}
	c.addr = addr
	return c, nil
}

// apply sets the options of the command line on s.
func (c *config) apply(s *socket) error {
	for _, o := range intOptions {
		if c.set[o.name] {
			if err := s.setInt(o, *c.ints[o.name]); err != nil {
				return err
			}
		}
	}
	if c.set[optMaxBW.name] {
		if err := s.setInt64(optMaxBW, *c.maxbw); err != nil {
			return err
		}
	}
	for _, o := range timeOptions {
		if c.set[o.name] {
			if err := s.setInt(o, *c.ints[o.name]); err != nil {
				return err
			}
		}
	}
	for _, o := range boolOptions {
		if c.set[o.name] {
			v := 0
			if c.bools[o.name].v {
				v = 1
			}
			if err := s.setInt(o, v); err != nil {
				return err
			}
		}
	}
	for _, o := range []option{optPassphrase, optStreamID} {
		if c.set[o.name] {
			if err := s.setString(o, *c.strs[o.name]); err != nil {
				return err
			}
		}
	}
	if c.set[optPBKeyLen.name] {
		if err := s.setInt(optPBKeyLen, *c.ints[optPBKeyLen.name]); err != nil {
			return err
		}
	}
	return nil
}

// messageSize is the size of the messages sent: 7 TS packets, or as many as
// fit in the payload size.
func (c *config) messageSize() int {
	n := 7 * 188
	if c.set[optPayloadSize.name] {
		if p := *c.ints[optPayloadSize.name]; p < n {
			n = p
			if n >= 188 {
				n -= n % 188
			}
		}
	}
	return n
}

func run(ctx context.Context, args []string, stdin io.Reader, stdout, stderr io.Writer) int {
	c, err := parse(args, stderr)
	if err != nil {
		fmt.Fprintf(stderr, "srt-libsrt: %v\n", err)
		return 2
	}
	enc := json.NewEncoder(stderr)
	emit := func(e event) { enc.Encode(e) }

	s, err := c.open(ctx, emit)
	if err != nil {
		e := event{Event: "error", Error: err.Error()}
		var rej *rejectError
		if errors.As(err, &rej) {
			e.Reject = rej.reason
		}
		emit(e)
		return 1
	}

	e := event{Event: "connected"}
	if a, err := s.name(false); err == nil {
		e.Local = a.String()
	}
	if a, err := s.name(true); err == nil {
		e.Peer = a.String()
	}
	e.StreamID, _ = s.getString(optStreamID)
	if st, err := s.stats(); err == nil {
		e.MsRcvTsbPdDelay, e.MsSndTsbPdDelay = st.MsRcvTsbPdDelay, st.MsSndTsbPdDelay
		e.MSS, e.FlowWindow = st.ByteMSS, st.PktFlowWindow
	}
	emit(e)

	if c.command == "play" {
		stdin = nil
	}
	emit(c.serve(ctx, s, stdin, stdout))
	return 0
}

// open returns the connected socket: called, or the first caller accepted.
func (c *config) open(ctx context.Context, emit func(event)) (*socket, error) {
	s, err := newSocket()
	if err != nil {
		return nil, err
	}
	if err := c.apply(s); err != nil {
		s.close()
		return nil, err
	}
	// Closing the socket ends a connect or an accept that waits.
	stop := context.AfterFunc(ctx, s.close)
	defer stop()

	if c.command != "listen" {
		if err := s.connect(c.addr); err != nil {
			s.close()
			return nil, err
		}
		return s, nil
	}

	defer s.close()
	if err := s.bind(c.addr); err != nil {
		return nil, err
	}
	if err := s.listen(1); err != nil {
		return nil, err
	}
	a, err := s.name(false)
	if err != nil {
		return nil, err
	}
	emit(event{Event: "listening", Addr: a.String()})
	conn, _, err := s.accept()
	if err != nil {
		return nil, err
	}
	return conn, nil
}

// serve moves stdin, unless nil, to the peer and what the peer sends to
// stdout, until the end of stdin, the peer closes, or ctx ends, then closes s
// and returns the closed event.
func (c *config) serve(ctx context.Context, s *socket, stdin io.Reader, stdout io.Writer) event {
	done := make(chan string, 2)
	var sent, recv atomic.Int64
	// libsrt frees a socket the peer closed at once, so the statistics are
	// taken after each message, for the end.
	var last atomic.Pointer[Stats]
	snap := func() {
		if st, err := s.stats(); err == nil {
			last.Store(&st)
		}
	}

	go func() {
		b := make([]byte, 1500)
		for {
			n, err := s.recv(b)
			if err != nil {
				done <- "peer"
				return
			}
			if _, err := stdout.Write(b[:n]); err != nil {
				done <- "stdout"
				return
			}
			recv.Add(1)
			snap()
		}
	}()
	if stdin != nil {
		go func() {
			if err := sendAll(s, stdin, c.messageSize(), func() { sent.Add(1); snap() }); err != nil {
				done <- "peer"
				return
			}
			drain(ctx, s)
			done <- "eof"
		}()
	}

	e := event{Event: "closed"}
	select {
	case e.Reason = <-done:
	case <-ctx.Done():
		e.Reason = "signal"
	}
	if st, err := s.stats(); err == nil {
		e.Stats = &st
	} else {
		e.Stats = last.Load()
	}
	s.close()
	e.MsgSent, e.MsgRecv = sent.Load(), recv.Load()
	return e
}

// sendAll sends what each read of r gives as a message, at most size bytes,
// until the end of r: a writer that writes whole messages keeps them.
func sendAll(s *socket, r io.Reader, size int, onSent func()) error {
	b := make([]byte, size)
	for {
		n, err := r.Read(b)
		if n > 0 {
			if serr := s.send(b[:n]); serr != nil {
				return serr
			}
			onSent()
		}
		if err != nil {
			return nil
		}
	}
}

// drainTimeout bounds how long the end of stdin waits for the peer to
// acknowledge what was sent.
const drainTimeout = time.Second

// drainMargin is added to the peer's TSBPD delay and the RTT, for a peer
// whose reader is slow to take what is due.
const drainMargin = 50 * time.Millisecond

// drain waits until the peer has acknowledged every byte sent, then for the
// peer's TSBPD delay, unless the connection breaks or ctx ends: closing a
// live socket drops what the peer hasn't acknowledged, and the peer drops
// what it still holds for its TSBPD delay when the connection ends.
func drain(ctx context.Context, s *socket) {
	deadline := time.Now().Add(drainTimeout)
	for time.Now().Before(deadline) && ctx.Err() == nil && s.connected() {
		if n, err := s.unacked(); err != nil || n == 0 {
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	st, err := s.stats()
	if err != nil || st.PktSent == 0 {
		return
	}
	hold := time.Duration(st.MsSndTsbPdDelay)*time.Millisecond + time.Duration(st.MsRTT*float64(time.Millisecond)) + drainMargin
	t := time.NewTimer(hold)
	defer t.Stop()
	select {
	case <-ctx.Done():
	case <-t.C:
	}
}
