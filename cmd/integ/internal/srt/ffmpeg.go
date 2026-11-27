// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"srs-integ/internal/media"
)

// FFmpeg is an ffmpeg built with libsrt, run as a process, as encoders and
// players run it. It moves media one way, by the stream ID: a Conn whose
// stream ID has m=publish publishes what it is written, and any other plays
// what the peer sends, as SRS's default mode does. So it is not in Clients,
// whose Conns both write and read, but ClientNamed finds it.
var FFmpeg Client = ffmpegClient{}

// ffmpegBuild is how to get an ffmpeg with libsrt.
const ffmpegBuild = "build one with: bash skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh, or set $FFMPEG"

// ffmpegSRTPath returns the path of the ffmpeg find gives, when it has
// libsrt; it fails, saying how to build one, otherwise.
func ffmpegSRTPath(find func() (*media.FFmpeg, error)) (string, error) {
	ff, err := find()
	if err != nil {
		return "", fmt.Errorf("%v; %v", err, ffmpegBuild)
	}
	if !ff.HasProtocol("srt") {
		return "", fmt.Errorf("ffmpeg %v lacks libsrt; %v", ff.Path, ffmpegBuild)
	}
	return ff.Path, nil
}

var ffmpegPathOnce = sync.OnceValues(func() (string, error) { return ffmpegSRTPath(media.FindFFmpeg) })

// FFmpegPath returns the path of the ffmpeg with libsrt the integ suite finds,
// as it finds ffmpeg for its other cases; it fails, saying how to build one,
// when there is none.
func FFmpegPath() (string, error) { return ffmpegPathOnce() }

type ffmpegClient struct{}

func (ffmpegClient) Name() string { return "ffmpeg" }

// ffmpegPublishRE is the m=publish of a stream ID, in any form SRS takes.
var ffmpegPublishRE = regexp.MustCompile(`(?i)(^|[,?&:])m=publish($|[,&])`)

// ffmpegPublishes tells whether a stream ID asks to publish.
func ffmpegPublishes(streamID string) bool { return ffmpegPublishRE.MatchString(streamID) }

// ffmpegURL is the srt:// URL of addr with opts as ffmpeg's options, its
// times in microseconds, except connect_timeout, in milliseconds, and the
// stream ID and passphrase as options of the command line, before the URL:
// ffmpeg 6 and later URL-decode a value of the URL, ffmpeg 5 does not, so a
// string can't be given there the same way to both.
func ffmpegURL(addr string, opts Options, timeout time.Duration) (u string, args []string) {
	us := func(d time.Duration) string { return strconv.FormatInt(d.Microseconds(), 10) }
	bit := func(b bool) string {
		if b {
			return "1"
		}
		return "0"
	}
	q := []string{"mode=caller", "transtype=live", "connect_timeout=" + strconv.FormatInt(timeout.Milliseconds(), 10)}
	add := func(name, v string) { q = append(q, name+"="+v) }
	if opts.StreamID != "" {
		args = append(args, "-srt_streamid", opts.StreamID)
	}
	if opts.Passphrase != "" {
		args = append(args, "-passphrase", opts.Passphrase)
		if opts.PBKeyLen != 0 {
			add("pbkeylen", strconv.Itoa(opts.PBKeyLen))
		}
	}
	if opts.Latency != 0 {
		add("latency", us(opts.Latency))
	}
	if recv, peer, ok := recvPeerLatency(opts); ok {
		add("rcvlatency", us(recv))
		add("peerlatency", us(peer))
	}
	if opts.MSS != 0 {
		add("mss", strconv.Itoa(opts.MSS))
	}
	if opts.PayloadSize != 0 {
		add("payload_size", strconv.Itoa(opts.PayloadSize))
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
		add("tsbpd", bit(*opts.TSBPDMode))
	}
	if opts.TLPktDrop != nil {
		add("tlpktdrop", bit(*opts.TLPktDrop))
	}
	return "srt://" + addr + "?" + strings.Join(q, "&"), args
}

// ffmpegRejectRE is libsrt's log of a refused handshake, which ffmpeg prints
// among its errors, such as "rejection HS response: ERROR:BADSECRET".
var ffmpegRejectRE = regexp.MustCompile(`(?i)reject.*\bERROR:([A-Z]+)(?::([0-9]+))?`)

// ffmpegRejectNames are libsrt's names of the reasons, from 1000 on.
var ffmpegRejectNames = []string{
	"UNKNOWN", "SYSTEM", "PEER", "RESOURCE", "ROGUE", "BACKLOG", "IPE", "CLOSE", "VERSION", "RDVCOOKIE",
	"BADSECRET", "UNSECURE", "MESSAGEAPI", "CONGESTION", "FILTER", "GROUP", "TIMEOUT", "CRYPTO",
}

// ffmpegRejectOf returns a *RejectError for the reason libsrt logged in
// ffmpeg's output, and err with the output otherwise. ffmpeg itself only says
// "Input/output error".
func ffmpegRejectOf(err error, logs string) error {
	for _, line := range strings.Split(logs, "\n") {
		m := ffmpegRejectRE.FindStringSubmatch(line)
		if m == nil {
			continue
		}
		n, _ := strconv.Atoi(m[2])
		switch m[1] {
		case "PREDEFINED":
			return &RejectError{Reason: 1000 + n}
		case "USERDEFINED":
			return &RejectError{Reason: 2000 + n}
		}
		for i, name := range ffmpegRejectNames {
			if name == m[1] {
				return &RejectError{Reason: 1000 + i}
			}
		}
	}
	return fmt.Errorf("ffmpeg: %v: %v", err, strings.TrimSpace(logs))
}

// ffmpegSettle is how long Dial of a player waits for ffmpeg's handshake to
// fail before it returns the Conn: ffmpeg says nothing when it connects, and
// a refusal comes back in one round trip. A handshake nobody answers fails
// later, at the first Read.
const ffmpegSettle = 300 * time.Millisecond

// ffmpegCloseTimeout bounds how long Close waits for ffmpeg to end.
const ffmpegCloseTimeout = 2 * time.Second

// Dial runs ffmpeg as a caller: a publisher, which remuxes what the Conn is
// written to addr, or a player, which remuxes what addr sends to the Conn's
// reads. A publisher connects once ffmpeg has read enough media to know its
// streams, so its Dial returns at once and a refusal comes at a Write; a
// player's Dial returns a refusal itself.
func (ffmpegClient) Dial(ctx context.Context, addr string, opts Options) (*Conn, error) {
	if err := checkOptions(opts); err != nil {
		return nil, err
	}
	if opts.PeerIdleTimeout != 0 {
		return nil, fmt.Errorf("ffmpeg has no peer_idle_timeout option")
	}
	path, err := FFmpegPath()
	if err != nil {
		return nil, err
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}

	u, srtArgs := ffmpegURL(addr, opts, handshakeTimeout(ctx, opts))
	l := &ffmpegLink{publish: ffmpegPublishes(opts.StreamID), failed: make(chan struct{}), exited: make(chan struct{})}
	// ffmpeg learns the streams from 1s of media, not the 3s it reads of a
	// live input by default: a TS muxer may hold audio up to 0.7s behind the
	// video. Its own TS muxer sends each packet at once (-muxdelay 0).
	args := []string{"-hide_banner", "-loglevel", "error", "-nostdin", "-analyzeduration", "1000000", "-f", "mpegts"}
	if l.publish {
		args = append(args, "-i", "pipe:0", "-map", "0", "-c", "copy", "-copyts", "-muxdelay", "0", "-f", "mpegts")
		args = append(append(args, srtArgs...), u)
	} else {
		args = append(append(args, srtArgs...), "-i", u)
		args = append(args, "-map", "0", "-c", "copy", "-copyts", "-muxdelay", "0", "-f", "mpegts", "-flush_packets", "1", "pipe:1")
	}
	l.cmd = exec.Command(path, args...)
	if l.publish {
		if l.stdin, err = l.cmd.StdinPipe(); err != nil {
			return nil, err
		}
	} else {
		// An os.Pipe of its own, which Wait leaves open, so the last of the
		// media can be read after ffmpeg exits.
		r, w, err := os.Pipe()
		if err != nil {
			return nil, err
		}
		defer w.Close()
		l.cmd.Stdout, l.out, l.stdout = w, r, bufio.NewReader(r)
	}
	stderr, err := l.cmd.StderrPipe()
	if err != nil {
		return nil, err
	}
	if err := l.cmd.Start(); err != nil {
		if l.out != nil {
			l.out.Close()
		}
		return nil, err
	}
	go l.watch(stderr)
	if l.publish {
		return &Conn{c: l, client: "ffmpeg"}, nil
	}

	// The first byte of media, a failure, or the settle time, whichever is
	// first.
	l.peeked = make(chan struct{})
	go func() {
		defer close(l.peeked)
		l.stdout.Peek(1)
	}()
	settle := time.NewTimer(ffmpegSettle)
	defer settle.Stop()
	select {
	case <-ctx.Done():
		l.kill()
		return nil, ctx.Err()
	case <-l.failed:
		l.kill()
		return nil, l.err()
	case <-l.peeked:
	case <-settle.C:
	}
	return &Conn{c: l, client: "ffmpeg"}, nil
}

// An ffmpegLink is a running ffmpeg: a publisher reads its stdin, and a
// player writes its stdout.
type ffmpegLink struct {
	cmd     *exec.Cmd
	publish bool
	stdin   io.WriteCloser
	out     *os.File
	stdout  *bufio.Reader
	// peeked is closed when the player's first byte, or its end, is there.
	peeked chan struct{}
	// failed is closed when ffmpeg logs that it could not connect, and
	// exited when it has exited, with waitErr.
	failed, exited chan struct{}
	waitErr        error
	failOnce       sync.Once
	closeOnce      sync.Once
	mu             sync.Mutex
	logs           strings.Builder
}

// ffmpegConnectFailedRE is ffmpeg's log of a handshake that failed, such as
// "[srt @ 0x..] Connection to srt://... failed: Input/output error".
var ffmpegConnectFailedRE = regexp.MustCompile(`\] Connection to srt://.* failed`)

// watch keeps ffmpeg's output for its errors and notes a failed handshake,
// until ffmpeg exits.
func (l *ffmpegLink) watch(stderr io.Reader) {
	s := bufio.NewScanner(stderr)
	for s.Scan() {
		line := s.Text()
		l.mu.Lock()
		fmt.Fprintf(&l.logs, "%v\n", line)
		l.mu.Unlock()
		if ffmpegConnectFailedRE.MatchString(line) {
			l.failOnce.Do(func() { close(l.failed) })
			// ffmpeg would only exit, after up to 1s in libsrt's cleanup, and
			// a publisher's writes wait for it until then.
			l.cmd.Process.Kill()
		}
	}
	l.waitErr = l.cmd.Wait()
	close(l.exited)
}

// err is why ffmpeg failed, with libsrt's reason when it logged one.
func (l *ffmpegLink) err() error {
	l.mu.Lock()
	defer l.mu.Unlock()
	return ffmpegRejectOf(errors.New("connect failed"), l.logs.String())
}

// failure waits up to d for ffmpeg to log a failed handshake or to exit, and
// returns the handshake's failure, or nil. A player whose peer closes before
// it sent media also ends ffmpeg with an error, which is no failure of the
// connection.
func (l *ffmpegLink) failure(d time.Duration) error {
	select {
	case <-l.failed:
		return l.err()
	case <-l.exited:
		select {
		case <-l.failed:
			return l.err()
		default:
			return nil
		}
	case <-time.After(d):
		return nil
	}
}

func (l *ffmpegLink) Read(p []byte) (int, error) {
	if l.publish {
		return 0, errors.New("the ffmpeg connection publishes; give a stream ID without m=publish to play")
	}
	<-l.peeked
	n, err := l.stdout.Read(p)
	if err != nil {
		// A refusal or a timeout ends ffmpeg with its output unopened.
		if ferr := l.failure(100 * time.Millisecond); ferr != nil {
			return n, ferr
		}
		select {
		case <-l.exited:
			if l.waitErr != nil {
				l.mu.Lock()
				defer l.mu.Unlock()
				return n, fmt.Errorf("ffmpeg: %w: %v", l.waitErr, strings.TrimSpace(l.logs.String()))
			}
		default:
		}
	}
	return n, err
}

func (l *ffmpegLink) Write(p []byte) (int, error) {
	if !l.publish {
		return 0, errors.New("the ffmpeg connection plays; give a stream ID with m=publish to publish")
	}
	select {
	case <-l.failed:
		l.kill()
		return 0, l.err()
	default:
	}
	n, err := l.stdin.Write(p)
	if err != nil {
		ferr := l.failure(ffmpegCloseTimeout)
		l.kill()
		if ferr != nil {
			return n, ferr
		}
		l.mu.Lock()
		defer l.mu.Unlock()
		return n, fmt.Errorf("ffmpeg: %w: %v", err, strings.TrimSpace(l.logs.String()))
	}
	return n, err
}

// Close ends a publisher's stdin, so ffmpeg sends what it has and closes the
// connection, and stops a player with SIGTERM, so ffmpeg tells the peer; then
// it waits for ffmpeg to exit, and kills it after ffmpegCloseTimeout.
func (l *ffmpegLink) Close() error {
	l.closeOnce.Do(func() {
		if l.publish {
			l.stdin.Close()
		} else {
			l.cmd.Process.Signal(syscall.SIGTERM)
		}
		select {
		case <-l.exited:
		case <-time.After(ffmpegCloseTimeout):
			l.cmd.Process.Kill()
			<-l.exited
		}
		if l.out != nil {
			l.out.Close()
		}
	})
	return nil
}

// kill ends ffmpeg at once.
func (l *ffmpegLink) kill() {
	l.closeOnce.Do(func() {
		l.cmd.Process.Kill()
		<-l.exited
		if l.out != nil {
			l.out.Close()
		}
	})
}

// tsbpd is zero: ffmpeg does not tell the delays it agreed.
func (l *ffmpegLink) tsbpd() (recv, send time.Duration) { return 0, 0 }

func (l *ffmpegLink) negotiated() (mss, flowWindow int) { return 0, 0 }
