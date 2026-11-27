// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Libsrt is Haivision's libsrt, a newer release than SRS bundles, through the
// tools/srt-libsrt process, which LibsrtPath finds.
var Libsrt Client = libsrtClient{}

// libsrtBuild is how to build the tool.
const libsrtBuild = "build it with: bash skills/srs-develop/scripts/srt-libsrt-build.sh, or set $SRT_LIBSRT"

// LibsrtPath returns the path of the srt-libsrt tool: $SRT_LIBSRT, then
// tools/srt-libsrt/objs/srt-libsrt of the repository. It fails, saying how
// to build it, when there is none.
func LibsrtPath() (string, error) {
	p := os.Getenv("SRT_LIBSRT")
	if p == "" {
		_, file, _, _ := runtime.Caller(0)
		p = filepath.Join(filepath.Dir(file), "..", "..", "..", "..", "tools", "srt-libsrt", "objs", "srt-libsrt")
	}
	if st, err := os.Stat(p); err != nil || st.IsDir() {
		return "", fmt.Errorf("no srt-libsrt at %v; %v", filepath.Clean(p), libsrtBuild)
	}
	return p, nil
}

type libsrtClient struct{}

func (libsrtClient) Name() string { return "libsrt" }

// libsrtEvent is a JSON line the tool writes to stderr.
type libsrtEvent struct {
	Event           string `json:"event"`
	Error           string `json:"error"`
	Reject          int    `json:"reject"`
	MsRcvTsbPdDelay int    `json:"msRcvTsbPdDelay"`
	MsSndTsbPdDelay int    `json:"msSndTsbPdDelay"`
	MSS             int    `json:"mss"`
	FlowWindow      int    `json:"flowWindow"`
	Links           int    `json:"links"`
}

// libsrtArgs are the tool's options for opts, with timeout for the handshake.
func libsrtArgs(opts Options, timeout time.Duration) []string {
	ms := func(d time.Duration) string { return strconv.FormatInt(d.Milliseconds(), 10) }
	onOff := func(b bool) string {
		if b {
			return "on"
		}
		return "off"
	}
	args := []string{"-conntimeo", ms(timeout)}
	add := func(name, v string) { args = append(args, "-"+name, v) }
	if opts.StreamID != "" {
		add("streamid", opts.StreamID)
	}
	if opts.Passphrase != "" {
		add("passphrase", opts.Passphrase)
		if opts.PBKeyLen != 0 {
			add("pbkeylen", strconv.Itoa(opts.PBKeyLen))
		}
	}
	if opts.Latency != 0 {
		add("latency", ms(opts.Latency))
	}
	if recv, peer, ok := recvPeerLatency(opts); ok {
		add("rcvlatency", ms(recv))
		add("peerlatency", ms(peer))
	}
	if opts.PeerIdleTimeout != 0 {
		add("peeridletimeo", ms(opts.PeerIdleTimeout))
	}
	if opts.MSS != 0 {
		add("mss", strconv.Itoa(opts.MSS))
	}
	if opts.PayloadSize != 0 {
		add("payloadsize", strconv.Itoa(opts.PayloadSize))
	}
	if opts.MaxBW != 0 {
		add("maxbw", strconv.FormatInt(opts.MaxBW, 10))
	}
	if opts.SendBuf != 0 {
		add("sndbuf", strconv.Itoa(opts.SendBuf))
	}
	if opts.RecvBuf != 0 {
		add("rcvbuf", strconv.Itoa(opts.RecvBuf))
	}
	if opts.TSBPDMode != nil {
		add("tsbpdmode", onOff(*opts.TSBPDMode))
	}
	if opts.TLPktDrop != nil {
		add("tlpktdrop", onOff(*opts.TLPktDrop))
	}
	return args
}

// Dial runs the tool's publish, which also writes what the peer sends to its
// stdout, so the Conn both publishes and plays, as the stream ID asks.
func (libsrtClient) Dial(ctx context.Context, addr string, opts Options) (*Conn, error) {
	return libsrtStart(ctx, opts, addr)
}

// DialGroup runs the tool's publish as a group over links.
func (libsrtClient) DialGroup(ctx context.Context, mode GroupMode, links []string, opts Options) (*Conn, error) {
	return libsrtStart(ctx, opts, libsrtGroupArgs(mode, links)...)
}

// libsrtGroupArgs are the tool's options of a group of mode over links.
func libsrtGroupArgs(mode GroupMode, links []string) []string {
	args := []string{"-group", string(mode)}
	for _, l := range links {
		args = append(args, "-link", l)
	}
	return args
}

// libsrtStart runs the tool's publish with opts, then the rest of its
// arguments, and returns the Conn once it connected.
func libsrtStart(ctx context.Context, opts Options, rest ...string) (*Conn, error) {
	if err := checkOptions(opts); err != nil {
		return nil, err
	}
	path, err := LibsrtPath()
	if err != nil {
		return nil, err
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}

	cmd := exec.Command(path, append(append([]string{"publish"}, libsrtArgs(opts, handshakeTimeout(ctx, opts))...), rest...)...)
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	// An os.Pipe of its own, which Wait leaves open, so the last of the media
	// can be read after the tool exits.
	stdout, w, err := os.Pipe()
	if err != nil {
		return nil, err
	}
	defer w.Close()
	cmd.Stdout = w
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		stdout.Close()
		return nil, err
	}
	l := &libsrtLink{cmd: cmd, stdin: stdin, stdout: stdout, exited: make(chan struct{})}

	// The first event is connected or error; the tool's other lines, such as
	// libsrt's logs, tell why it ended without one.
	first := make(chan libsrtEvent, 1)
	var logs strings.Builder
	var logsMu sync.Mutex
	go func() {
		s := bufio.NewScanner(stderr)
		sent := false
		for s.Scan() {
			var e libsrtEvent
			if json.Unmarshal(s.Bytes(), &e) != nil || e.Event == "" {
				logsMu.Lock()
				fmt.Fprintf(&logs, "%s\n", s.Bytes())
				logsMu.Unlock()
				continue
			}
			if !sent && (e.Event == "connected" || e.Event == "error") {
				sent = true
				first <- e
			}
		}
		close(first)
		l.waitErr = cmd.Wait()
		close(l.exited)
	}()

	select {
	case <-ctx.Done():
		l.kill()
		return nil, ctx.Err()
	case e, ok := <-first:
		if ok && e.Event == "connected" {
			l.recv = time.Duration(e.MsRcvTsbPdDelay) * time.Millisecond
			l.send = time.Duration(e.MsSndTsbPdDelay) * time.Millisecond
			l.mss, l.flowWindow, l.nlinks = e.MSS, e.FlowWindow, e.Links
			return &Conn{c: l, client: "libsrt"}, nil
		}
		l.kill()
		if ok && e.Reject != 0 {
			return nil, &RejectError{Reason: e.Reject}
		}
		logsMu.Lock()
		defer logsMu.Unlock()
		if ok {
			return nil, fmt.Errorf("libsrt: %v", e.Error)
		}
		return nil, fmt.Errorf("libsrt: %v: %v", l.waitErr, strings.TrimSpace(logs.String()))
	}
}

// A libsrtLink is a running tool: its stdin goes to the peer, and what the
// peer sends comes on its stdout.
type libsrtLink struct {
	cmd             *exec.Cmd
	stdin           io.WriteCloser
	stdout          *os.File
	recv, send      time.Duration
	mss, flowWindow int
	nlinks          int
	exited          chan struct{}
	waitErr         error
	closeOnce       sync.Once
}

func (l *libsrtLink) Read(p []byte) (int, error) { return l.stdout.Read(p) }

func (l *libsrtLink) Write(p []byte) (int, error) { return l.stdin.Write(p) }

// libsrtCloseTimeout bounds how long Close waits for the tool to send what it
// has and end, before it kills it.
const libsrtCloseTimeout = 2 * time.Second

// Close ends the tool's stdin, so it sends what it has, closes the
// connection and exits.
func (l *libsrtLink) Close() error {
	l.closeOnce.Do(func() {
		l.stdin.Close()
		select {
		case <-l.exited:
		case <-time.After(libsrtCloseTimeout):
			l.kill()
		}
		l.stdout.Close()
	})
	return nil
}

func (l *libsrtLink) kill() {
	l.cmd.Process.Kill()
	<-l.exited
	l.stdout.Close()
}

func (l *libsrtLink) tsbpd() (recv, send time.Duration) { return l.recv, l.send }

func (l *libsrtLink) negotiated() (mss, flowWindow int) { return l.mss, l.flowWindow }

func (l *libsrtLink) links() int { return l.nlinks }
