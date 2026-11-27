// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/exec"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// LiveTransmit is srt-live-transmit of libsrt v1.5.7, in the interop image
// of srt-interop-image.sh, run with Docker, as users relay a stream with it.
// It moves media one way, by the stream ID, as FFmpeg does: a Conn whose
// stream ID has m=publish publishes what it is written, and any other plays.
// So it is not in Clients, but ClientNamed finds it.
var LiveTransmit Client = liveTransmitClient{}

// An interopTool is one of libsrt's own tools in the interop image, which
// moves media from its first URI to its second.
type interopTool struct {
	name string
	args []string
	// callerArgs are added for a caller, which drops what it reads before it
	// connects, so it is written once its stderr has a line that ends with
	// connected.
	callerArgs []string
	// wholeReads is set for a tool that needs each read of its stdin to be a
	// whole message: srt-live-transmit 1.5.7 sends a whole chunk after a
	// short read, the rest of it stale bytes, and Docker passes stdin on in
	// pieces of its own. So dd gives it whole messages through a pipe, whose
	// writes up to 4096 bytes are atomic.
	wholeReads bool
}

var (
	// interopLiveTransmit is srt-live-transmit, with no reconnect and
	// messages of 7 TS packets; it says "SRT target connected", or "SRT
	// source connected" for a caller it reads from.
	interopLiveTransmit = interopTool{"srt-live-transmit", []string{"-a", "no", "-chunk", "1316"}, nil, true}
	// interopTestLive is srt-test-live, which doesn't wait 5s at its end; its
	// verbose log to stderr, -v:2, says " connected."
	interopTestLive = interopTool{"srt-test-live", []string{"-sf"}, []string{"-v:2"}, false}
)

// interopConnectedRE is a tool's log line of a caller that connected.
var interopConnectedRE = regexp.MustCompile(`(^|[^s])connected\.?$`)

// interopImage is the Docker image of srt-interop-image.sh.
func interopImage() string {
	if s := os.Getenv("SRT_INTEROP_IMAGE"); s != "" {
		return s
	}
	return "srs-srt-interop:1.5.7"
}

var interopOnce = sync.OnceValue(func() error {
	if _, err := exec.LookPath("docker"); err != nil {
		return err
	}
	if out, err := exec.Command("docker", "image", "inspect", "-f", "{{.Id}}", interopImage()).CombinedOutput(); err != nil {
		return fmt.Errorf("no image %v: %v: %s; build it with: bash skills/srs-develop/scripts/srt-interop-image.sh", interopImage(), err, bytes.TrimSpace(out))
	}
	return nil
})

// InteropReady tells why libsrt's tools in Docker can't run, or nil: they run
// only with $SRT_INTEROP on, as a container takes seconds, and need the
// interop image.
func InteropReady() error {
	if os.Getenv("SRT_INTEROP") != "on" {
		return errors.New("libsrt's tools in Docker run with SRT_INTEROP=on, after: bash skills/srs-develop/scripts/srt-interop-image.sh")
	}
	return interopOnce()
}

var interopSeq atomic.Int64

// interopRun is a docker run of a tool, with the network of the host on
// Linux; Docker Desktop has none, so publish lists its UDP ports.
func interopRun(tool interopTool, publish []int, args ...string) (cmd *exec.Cmd, name string, rm func()) {
	name = fmt.Sprintf("srs-srt-cross-%v-%v", os.Getpid(), interopSeq.Add(1))
	run := []string{"run", "--rm", "-i", "--name", name}
	if runtime.GOOS == "linux" {
		run = append(run, "--network", "host")
	} else {
		for _, p := range publish {
			run = append(run, "-p", fmt.Sprintf("127.0.0.1:%v:%v/udp", p, p))
		}
	}
	run = append(run, interopImage())
	if tool.wholeReads {
		run = append(run, "sh", "-c", fmt.Sprintf(`dd bs=%v iflag=fullblock status=none | exec "$0" "$@"`, MessageSize))
	}
	run = append(append(append(run, tool.name), tool.args...), args...)
	return exec.Command("docker", run...), name, func() { exec.Command("docker", "rm", "-f", name).Run() }
}

// interopHost is how a container calls the host's loopback address.
func interopHost(addr string) string {
	if runtime.GOOS == "linux" {
		return addr
	}
	return strings.Replace(addr, "127.0.0.1", "host.docker.internal", 1)
}

type liveTransmitClient struct{}

func (liveTransmitClient) Name() string { return "srtLiveTransmit" }

// liveTransmitURI is the caller's srt:// URI of addr with opts; the tool
// URL-decodes each value, so a stream ID keeps its # and commas.
func liveTransmitURI(addr string, opts Options, timeout time.Duration) string {
	ms := func(d time.Duration) string { return strconv.FormatInt(d.Milliseconds(), 10) }
	q := []string{"mode=caller", "transtype=live", "conntimeo=" + ms(timeout)}
	if opts.StreamID != "" {
		q = append(q, "streamid="+url.QueryEscape(opts.StreamID))
	}
	if opts.Passphrase != "" {
		q = append(q, "passphrase="+url.QueryEscape(opts.Passphrase))
		if opts.PBKeyLen != 0 {
			q = append(q, "pbkeylen="+strconv.Itoa(opts.PBKeyLen))
		}
	}
	if opts.Latency != 0 {
		q = append(q, "latency="+ms(opts.Latency))
	}
	return "srt://" + addr + "?" + strings.Join(q, "&")
}

// liveTransmitOptions refuses the options the client doesn't pass to the
// tool, by name.
func liveTransmitOptions(opts Options) error {
	var names []string
	add := func(set bool, name string) {
		if set {
			names = append(names, name)
		}
	}
	add(opts.RecvLatency != 0 || opts.PeerLatency != 0, "rcvlatency/peerlatency")
	add(opts.PeerIdleTimeout != 0, "peer_idle_timeout")
	add(opts.PayloadSize != 0, "payloadsize")
	add(opts.MSS != 0, "mss")
	add(opts.MaxBW != 0, "maxbw")
	add(opts.SendBuf != 0 || opts.RecvBuf != 0, "sendbuf/recvbuf")
	add(opts.TSBPDMode != nil, "tsbpdmode")
	add(opts.TLPktDrop != nil, "tlpktdrop")
	if names != nil {
		return fmt.Errorf("srtLiveTransmit takes no %v", strings.Join(names, ", "))
	}
	return nil
}

// liveTransmitCloseTimeout bounds how long Close waits for the tool to end.
const liveTransmitCloseTimeout = 2 * time.Second

// liveTransmitSettle is how long Dial of a player waits for the handshake to
// fail, after the tool starts, before it returns the Conn: the tool says a
// source connected only when its first media arrives, and a refusal comes
// back in one round trip.
const liveTransmitSettle = 300 * time.Millisecond

// liveTransmitStartedRE is the tool's first line, before it connects.
var liveTransmitStartedRE = regexp.MustCompile(`^Media path: `)

// Dial runs srt-live-transmit as a caller: a publisher sends what the Conn is
// written, a player writes what addr sends to the Conn's reads. A publisher's
// Dial returns when the tool says it connected, and a player's after
// liveTransmitSettle; a refusal is a *RejectError, from libsrt's log. A
// container starts in about a second, so ctx needs more than that.
func (liveTransmitClient) Dial(ctx context.Context, addr string, opts Options) (*Conn, error) {
	if err := checkOptions(opts); err != nil {
		return nil, err
	}
	if err := liveTransmitOptions(opts); err != nil {
		return nil, err
	}
	if err := interopOnce(); err != nil {
		return nil, err
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}

	l := newInteropLink("srtLiveTransmit", ffmpegPublishes(opts.StreamID), interopConnectedRE)
	uri := liveTransmitURI(interopHost(addr), opts, handshakeTimeout(ctx, opts))
	tool := interopLiveTransmit
	var args []string
	if l.publish {
		args = []string{"file://con", uri}
	} else {
		tool.wholeReads = false
		args = []string{uri, "file://con"}
	}
	if err := l.start(tool, args...); err != nil {
		return nil, err
	}

	ready := l.connected
	if !l.publish {
		select {
		case <-l.started:
		case <-l.rejected:
		case <-l.exited:
		case <-ctx.Done():
		}
		ready = make(chan struct{})
		settle := time.AfterFunc(liveTransmitSettle, func() { close(ready) })
		defer settle.Stop()
	}
	return l.wait(ctx, ready)
}

// TestLive is srt-test-live of libsrt v1.5.7, in the interop image, run with
// Docker, as a publisher that calls as a socket group: libsrt's own bonded
// caller. The image patches it to send over a group, which 1.5.7 refuses as
// it is. It calls only as a group, so it is not in GroupClients, but tests
// that have the image name it.
var TestLive GroupClient = testLiveClient{}

type testLiveClient struct{}

func (testLiveClient) Name() string { return "srtTestLive" }

// Dial is refused: srt-live-transmit is the plain libsrt tool, LiveTransmit.
func (testLiveClient) Dial(ctx context.Context, addr string, opts Options) (*Conn, error) {
	return nil, errors.New("srtTestLive calls only as a socket group here; srtLiveTransmit is libsrt's plain caller")
}

// testLiveLinkRE is libsrt's note, in srt-test-live's log at -ll:note, of a
// link that connected, and testLiveReadyRE the tool's line once it reads
// stdin.
var (
	testLiveLinkRE  = regexp.MustCompile(`: Connection established from .* to peer @`)
	testLiveReadyRE = regexp.MustCompile(`STARTING TRANSMISSION: `)
)

// DialGroup runs srt-test-live to publish what the Conn is written over a
// group of mode, one link to each address of links, with the group's options
// in its -g URI. The tool reads its stdin once its first link connects, as
// libsrt's srt_connect_group returns then, and tells nothing of the later
// links, so DialGroup counts libsrt's note of each link that connected, and
// returns once every link did, or the handshake timeout after the first; a
// refusal is a *RejectError, from libsrt's log. A container starts in about
// a second, so ctx needs more than that.
func (testLiveClient) DialGroup(ctx context.Context, mode GroupMode, links []string, opts Options) (*Conn, error) {
	if err := checkOptions(opts); err != nil {
		return nil, err
	}
	if err := liveTransmitOptions(opts); err != nil {
		return nil, fmt.Errorf("srtTestLive: %w", err)
	}
	if !ffmpegPublishes(opts.StreamID) {
		return nil, errors.New("srtTestLive only publishes; give a stream ID with m=publish")
	}
	if err := interopOnce(); err != nil {
		return nil, err
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}

	l := newInteropLink("srtTestLive", true, testLiveReadyRE)
	l.linkRE = testLiveLinkRE
	timeout := handshakeTimeout(ctx, opts)
	args := append([]string{}, interopTestLive.callerArgs...)
	args = append(args, "-ll:note", "file://con", "-g", testLiveGroupURI(mode, opts, timeout))
	for _, link := range links {
		args = append(args, interopHost(link))
	}
	if err := l.start(interopTestLive, args...); err != nil {
		return nil, err
	}
	c, err := l.wait(ctx, l.connected)
	if err != nil {
		return nil, err
	}
	deadline := time.Now().Add(timeout)
	for l.links() < len(links) && time.Now().Before(deadline) && ctx.Err() == nil {
		time.Sleep(5 * time.Millisecond)
	}
	return c, nil
}

// testLiveGroupURI is srt-test-live's URI of a group of mode with opts, whose
// values it URL-decodes, as srt-live-transmit does.
func testLiveGroupURI(mode GroupMode, opts Options, timeout time.Duration) string {
	q := []string{"type=" + string(mode), "conntimeo=" + strconv.FormatInt(timeout.Milliseconds(), 10)}
	if opts.StreamID != "" {
		q = append(q, "streamid="+url.QueryEscape(opts.StreamID))
	}
	if opts.Passphrase != "" {
		q = append(q, "passphrase="+url.QueryEscape(opts.Passphrase))
		if opts.PBKeyLen != 0 {
			q = append(q, "pbkeylen="+strconv.Itoa(opts.PBKeyLen))
		}
	}
	if opts.Latency != 0 {
		q = append(q, "latency="+strconv.FormatInt(opts.Latency.Milliseconds(), 10))
	}
	return "srt://*?" + strings.Join(q, "&")
}

// newInteropLink is a link of the client of a tool that publishes or plays,
// connected at its line readyRE.
func newInteropLink(client string, publish bool, readyRE *regexp.Regexp) *liveTransmitLink {
	return &liveTransmitLink{
		client: client, publish: publish, readyRE: readyRE,
		started: make(chan struct{}), connected: make(chan struct{}), rejected: make(chan struct{}), exited: make(chan struct{}),
	}
}

// start runs tool with args in its container, and watches its log.
func (l *liveTransmitLink) start(tool interopTool, args ...string) error {
	l.cmd, l.name, l.rm = interopRun(tool, nil, args...)
	var err error
	if l.stdin, err = l.cmd.StdinPipe(); err != nil {
		return err
	}
	// An os.Pipe of its own, which Wait leaves open, so the last of the media
	// can be read after the tool exits.
	r, w, err := os.Pipe()
	if err != nil {
		return err
	}
	defer w.Close()
	l.cmd.Stdout, l.stdout = w, r
	stderr, err := l.cmd.StderrPipe()
	if err != nil {
		r.Close()
		return err
	}
	if err := l.cmd.Start(); err != nil {
		r.Close()
		return err
	}
	go l.watch(stderr)
	return nil
}

// wait returns the Conn once ready is closed, or the tool's error when it is
// refused or ends first, or ctx's.
func (l *liveTransmitLink) wait(ctx context.Context, ready <-chan struct{}) (*Conn, error) {
	select {
	case <-ready:
		return &Conn{c: l, client: l.client}, nil
	case <-l.rejected:
		l.kill()
		return nil, l.err()
	case <-l.exited:
		l.kill()
		return nil, l.err()
	case <-ctx.Done():
		l.kill()
		return nil, fmt.Errorf("%v did not connect: %w: %v", l.client, ctx.Err(), l.output())
	}
}

// links is how many links of a group the tool said connected, 0 for a plain
// connection.
func (l *liveTransmitLink) links() int {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.nlinks
}

// A liveTransmitLink is a running srt-live-transmit, or srt-test-live, in its
// container.
type liveTransmitLink struct {
	// client is the name of the client, for its errors.
	client  string
	cmd     *exec.Cmd
	name    string
	rm      func()
	publish bool
	stdin   io.WriteCloser
	stdout  *os.File
	// readyRE is the tool's line that says it connected, and linkRE, when
	// set, a line before it of each link of a group that connected, which
	// nlinks counts.
	readyRE, linkRE *regexp.Regexp
	nlinks          int
	// started, connected and rejected are closed at the tool's lines that
	// say so, and exited when the tool has exited.
	started, connected, rejected, exited chan struct{}
	closeOnce                            sync.Once
	mu                                   sync.Mutex
	logs                                 strings.Builder
}

// watch keeps the tool's log and notes its start, its connection and a
// refusal, until it exits. libsrt keeps a refused caller's socket, so the
// tool would wait on: it is removed then.
func (l *liveTransmitLink) watch(stderr io.Reader) {
	s := bufio.NewScanner(stderr)
	var started, connected, rejected sync.Once
	for s.Scan() {
		line := s.Text()
		l.mu.Lock()
		fmt.Fprintf(&l.logs, "%v\n", line)
		// libsrt's log and the tool's own lines share stderr, so a note
		// of a link may come in the middle of another line.
		if l.linkRE != nil && l.linkRE.MatchString(line) {
			l.nlinks++
		}
		l.mu.Unlock()
		switch {
		case liveTransmitStartedRE.MatchString(line):
			started.Do(func() { close(l.started) })
		case l.readyRE.MatchString(line):
			connected.Do(func() { close(l.connected) })
		case ffmpegRejectRE.MatchString(line):
			rejected.Do(func() {
				close(l.rejected)
				go l.rm()
			})
		}
	}
	l.cmd.Wait()
	close(l.exited)
}

// output is the tool's log so far.
func (l *liveTransmitLink) output() string {
	l.mu.Lock()
	defer l.mu.Unlock()
	return strings.TrimSpace(l.logs.String())
}

// err is why the tool failed: libsrt's reason of a refusal, when it logged
// one, or the log.
func (l *liveTransmitLink) err() error {
	logs := l.output()
	if err := ffmpegRejectOf(errors.New("ended"), logs); errors.As(err, new(*RejectError)) {
		return err
	}
	return fmt.Errorf("%v ended before it connected: %v", l.client, logs)
}

func (l *liveTransmitLink) Read(p []byte) (int, error) {
	if l.publish {
		return 0, fmt.Errorf("the %v connection publishes; give a stream ID without m=publish to play", l.client)
	}
	n, err := l.stdout.Read(p)
	if err != nil {
		select {
		case <-l.rejected:
			return n, l.err()
		default:
		}
	}
	return n, err
}

func (l *liveTransmitLink) Write(p []byte) (int, error) {
	if !l.publish {
		return 0, fmt.Errorf("the %v connection plays; give a stream ID with m=publish to publish", l.client)
	}
	n, err := l.stdin.Write(p)
	if err != nil {
		return n, fmt.Errorf("%v: %w: %v", l.client, err, l.output())
	}
	return n, nil
}

// Close ends a publisher's stdin, so the tool sends what it has and closes the
// connection, and stops a player with SIGTERM, on which the tool closes its
// connection; then it waits for the tool to end, and removes the container
// after liveTransmitCloseTimeout.
func (l *liveTransmitLink) Close() error {
	l.closeOnce.Do(func() {
		l.stdin.Close()
		if !l.publish {
			exec.Command("docker", "kill", "-s", "TERM", l.name).Run()
		}
		select {
		case <-l.exited:
		case <-time.After(liveTransmitCloseTimeout):
			l.rm()
			<-l.exited
		}
		l.stdout.Close()
	})
	return nil
}

// kill removes the container at once.
func (l *liveTransmitLink) kill() {
	l.closeOnce.Do(func() {
		l.stdin.Close()
		l.rm()
		<-l.exited
		l.stdout.Close()
	})
}

// tsbpd is zero: the tool does not tell the delays it agreed.
func (l *liveTransmitLink) tsbpd() (recv, send time.Duration) { return 0, 0 }

func (l *liveTransmitLink) negotiated() (mss, flowWindow int) { return 0, 0 }
