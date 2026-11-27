// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"hash/fnv"
	"io"
	"net"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/ts"
)

// The cross-check sends the same TS messages from each library as a caller to
// each library as a listener, before any of them judges SRS: plain, encrypted
// at both key lengths, and through the relay at 5% loss. A library that
// passes the TS on as it is must deliver the bytes it was sent, once each and
// in order; ffmpeg remuxes, so a pair with it must deliver the same frames.

// crossCount is how many messages each pair sends.
const crossCount = 500

// crossPassphrase is the passphrase of the encrypted modes.
const crossPassphrase = "the-cross-check-phrase"

// A crossMode is how a pair connects.
type crossMode struct {
	name       string
	passphrase string
	keylen     int
	// loss is the percentage of packets the relay drops each way, or no
	// relay.
	loss float64
	// latency is the caller's, which the listener takes when it is higher,
	// or the library's default.
	latency time.Duration
}

// crossModes are the modes of each pair. At loss, a lost packet whose NAK or
// retransmission is lost too waits for the next NAK report, which comes
// after up to 300ms at the start, while the RTT is still the 100ms libsrt
// assumes; so the loss mode asks for 400ms, over the default 120ms.
var crossModes = []crossMode{
	{name: "Plain"},
	{name: "Aes16", passphrase: crossPassphrase, keylen: 16},
	{name: "Aes32", passphrase: crossPassphrase, keylen: 32},
	{name: "Loss5", loss: 5, latency: 400 * time.Millisecond},
}

// A crossSend is a caller's connection: Write sends one message, Close ends
// it.
type crossSend interface {
	Write(p []byte) (int, error)
	Close() error
}

// A crossCaller is one library as a caller.
type crossCaller struct {
	name string
	// remux is set for a library that sends its own remux of the TS, not the
	// messages it is given.
	remux bool
	// messages is set for a library that sends each message it is given as
	// one SRT message; a process whose stdin comes through Docker may split
	// them.
	messages bool
	need     func(t *testing.T)
	dial     func(t *testing.T, ctx context.Context, addr string, m crossMode) (crossSend, error)
}

// A crossResult is what a listener received: the bytes and the messages.
type crossResult struct {
	data []byte
	msgs int
	err  error
}

// A crossListener is one library as a listener. listen returns the address to
// call and the channel of what it receives from its first caller: up to want
// bytes, or until the caller closes, or ctx ends. libsrt is set for a listener
// that receives with libsrt, which at loss now and then drops a packet it
// should recover.
type crossListener struct {
	name     string
	remux    bool
	libsrt   bool
	messages bool
	need     func(t *testing.T)
	listen   func(t *testing.T, ctx context.Context, m crossMode, want int) (string, <-chan crossResult)
}

func noNeed(t *testing.T) {}

func needLibsrt(t *testing.T) { needClient(t, Libsrt) }

var crossCallers = []crossCaller{
	{name: "gosrt", messages: true, need: noNeed, dial: dialClient(GoSRT)},
	{name: "zsiec", messages: true, need: noNeed, dial: dialClient(Zsiec)},
	{name: "libsrt", messages: true, need: needLibsrt, dial: dialClient(Libsrt)},
	{name: "ffmpeg", remux: true, need: needFFmpeg, dial: dialClient(FFmpeg)},
	{name: "srtLiveTransmit", need: needInterop, dial: dialInterop(interopLiveTransmit)},
	{name: "srtTestLive", need: needInterop, dial: dialInterop(interopTestLive)},
}

var crossListeners = []crossListener{
	{name: "gosrt", messages: true, need: noNeed, listen: crossListenPeer(listenGosrt)},
	{name: "zsiec", messages: true, need: noNeed, listen: crossListenPeer(listenZsiec)},
	{name: "libsrt", libsrt: true, need: needLibsrt, listen: crossListenLibsrt},
	{name: "ffmpeg", remux: true, need: needFFmpeg, listen: crossListenFFmpeg},
	{name: "srtLiveTransmit", libsrt: true, need: needInterop, listen: listenInterop(interopLiveTransmit)},
	{name: "srtTestLive", libsrt: true, need: needInterop, listen: listenInterop(interopTestLive)},
}

// knownCrossFailure tells whether a caller fails with a listener in a mode,
// and why. These are between the libraries, not SRS: each is logged in the
// task file with the reason.
func knownCrossFailure(caller, listener string, m crossMode) (string, bool) {
	if reason, ok := knownPairFailure(caller, listener, m.keylen); ok {
		return reason, true
	}
	return "", false
}

// TestCrossCheck runs each caller against each listener in each mode.
func TestCrossCheck(t *testing.T) {
	msgs := crossMessages(t)
	var all []byte
	for _, b := range msgs {
		all = append(all, b...)
	}
	want := analyzeTS(all)

	for _, cl := range crossCallers {
		for _, ln := range crossListeners {
			for _, m := range crossModes {
				t.Run(cl.name+"To"+strings.ToUpper(ln.name[:1])+ln.name[1:]+"/"+m.name, func(t *testing.T) {
					t.Parallel()
					cl.need(t)
					ln.need(t)
					reason, known := knownCrossFailure(cl.name, ln.name, m)
					timeout := 10 * time.Second
					if known {
						timeout = 2 * time.Second
					}
					ctx, cancel := context.WithTimeout(context.Background(), timeout)
					defer cancel()
					got, err := crossRun(t, ctx, cl, ln, m, msgs)
					if err == nil && !cl.remux && !ln.remux {
						err = checkExact(got, all, crossCount, cl.messages && ln.messages)
						if err != nil && m.loss != 0 && ln.libsrt {
							if n, ok := droppedMessages(got.data, all); ok && n <= crossDropLimit {
								t.Logf("libsrt dropped %v of %v messages at %v%% loss, the rest arrived once each and in order", n, crossCount, m.loss)
								err = nil
							}
						}
					} else if err == nil {
						err = checkRemux(analyzeTS(got.data), want)
					}
					if known {
						if err == nil {
							t.Errorf("the pair passes now; drop it from knownCrossFailure: %v", reason)
						} else {
							t.Logf("skipped, known: %v (%v)", reason, err)
						}
						return
					}
					if err != nil {
						t.Fatal(err)
					}
				})
			}
		}
	}
}

// crossRun connects the caller to the listener, through a relay in a mode
// with loss, sends msgs, and returns what the listener received. The caller
// stays connected until the listener has it all, or for crossSettle and the
// latency after the last message, since some libraries drop what is not
// acknowledged when they close.
func crossRun(t *testing.T, ctx context.Context, cl crossCaller, ln crossListener, m crossMode, msgs [][]byte) (crossResult, error) {
	want := 0
	if !cl.remux {
		want = len(msgs) * len(msgs[0])
	}
	lctx, stopListener := context.WithCancel(ctx)
	defer stopListener()
	addr, got := ln.listen(t, lctx, m, want)
	if m.loss != 0 {
		h := fnv.New64a()
		h.Write([]byte(t.Name()))
		seed := int64(h.Sum64() >> 1)
		relay, err := NewImpair(addr, seed)
		if err != nil {
			t.Fatal(err)
		}
		defer relay.Close()
		relay.Set(Up, Shape{Loss: m.loss})
		relay.Set(Down, Shape{Loss: m.loss})
		addr = relay.Addr().String()
		defer func() {
			up, down := relay.Counters(Up), relay.Counters(Down)
			t.Logf("relay seed %v: up %+v, down %+v", seed, up, down)
		}()
	}

	c, err := cl.dial(t, ctx, addr, m)
	if err != nil {
		return crossResult{}, fmt.Errorf("dial: %w", err)
	}
	closed := false
	defer func() {
		if !closed {
			c.Close()
		}
	}()
	// 10 messages every 5ms: 500 in a quarter of a second.
	tick := time.NewTicker(5 * time.Millisecond)
	defer tick.Stop()
	for i, b := range msgs {
		if _, err := c.Write(b); err != nil {
			return crossResult{}, fmt.Errorf("write of message %v: %w", i, err)
		}
		if i%10 == 9 {
			select {
			case <-ctx.Done():
				return crossResult{}, ctx.Err()
			case <-tick.C:
			}
		}
	}

	// A live sender recovers a lost packet only when the receiver sees a later
	// one, as in a stream that goes on, so null packets follow the messages
	// until the listener has them all, or for the latency and crossSettle,
	// for a listener that can't tell, as with ffmpeg's remux.
	latency := m.latency
	if latency == 0 {
		latency = 120 * time.Millisecond
	}
	settle := time.NewTimer(latency + crossSettle)
	defer settle.Stop()
	pad := time.NewTicker(10 * time.Millisecond)
	defer pad.Stop()
	null := nullMessage()
padding:
	for {
		select {
		case r := <-got:
			return r, r.err
		case <-settle.C:
			break padding
		case <-ctx.Done():
			break padding
		case <-pad.C:
			if _, err := c.Write(null); err != nil {
				break padding
			}
		}
	}
	c.Close()
	closed = true
	// A listener that waits for the caller to close, as with ffmpeg's remux,
	// misses a shutdown the relay drops until its idle timeout, so it is
	// stopped a while after the close and gives what it has.
	stop := time.NewTimer(crossListenerGrace)
	defer stop.Stop()
	select {
	case r := <-got:
		return r, r.err
	case <-stop.C:
		stopListener()
		r := <-got
		return r, r.err
	case <-ctx.Done():
		return crossResult{}, fmt.Errorf("the listener got nothing to the end: %w", ctx.Err())
	}
}

// crossSettle is how long, over the latency, a caller sends null packets
// after the messages, for the last of them to be retransmitted and
// delivered.
const crossSettle = 400 * time.Millisecond

// crossListenerGrace is how long a listener has to end after its caller
// closed: ffmpeg's listener takes about a second to write what it holds and
// exit.
const crossListenerGrace = 2 * time.Second

// nullMessage is a message of null TS packets, which a demuxer drops.
func nullMessage() []byte {
	b := make([]byte, MessageSize)
	for i := 0; i < len(b); i += ts.PacketSize {
		b[i], b[i+1], b[i+2], b[i+3] = 0x47, 0x1f, 0xff, 0x10
	}
	return b
}

// crossMessages are the first crossCount messages of the TS test file.
func crossMessages(t *testing.T) [][]byte {
	b, err := os.ReadFile(media.Path(t, media.H264HighAACTS))
	if err != nil {
		t.Fatal(err)
	}
	if len(b) < crossCount*MessageSize {
		t.Fatalf("the TS file has %v bytes, want at least %v", len(b), crossCount*MessageSize)
	}
	var msgs [][]byte
	for i := range crossCount {
		msgs = append(msgs, b[i*MessageSize:(i+1)*MessageSize])
	}
	return msgs
}

// analyzeTS is the report of a whole TS stream, up to its end. Its error is
// dropped: the messages end in the middle of a PES, so the analyzer always
// stops at a cut one, and the report holds the frames before it.
func analyzeTS(b []byte) *report.Report {
	r, _ := ts.Analyze(ts.NewDemuxer(bytes.NewReader(b)), report.Until{})
	return r
}

// checkExact checks that the listener got the bytes sent, once each and in
// order, then only null packets, the last maybe cut by the read of a pipe,
// and, when both libraries keep messages, as many messages.
func checkExact(got crossResult, want []byte, count int, messages bool) error {
	if len(got.data) > len(want) && bytes.Equal(got.data[:len(want)], want) {
		null := nullMessage()[:ts.PacketSize]
		for at := len(want); at < len(got.data); at += ts.PacketSize {
			rest := got.data[at:min(at+ts.PacketSize, len(got.data))]
			if !bytes.Equal(rest, null[:len(rest)]) {
				return fmt.Errorf("got %v bytes, the %v sent, then something else than null packets at byte %v", len(got.data), len(want), at)
			}
		}
		got.data = got.data[:len(want)]
	}
	if !bytes.Equal(got.data, want) {
		at := 0
		for at < len(got.data) && at < len(want) && got.data[at] == want[at] {
			at++
		}
		return fmt.Errorf("got %v bytes in %v messages, want %v bytes; they differ from byte %v, message %v: %v", len(got.data), got.msgs, len(want), at, at/MessageSize, packetsFrom(got.data, want, at))
	}
	if messages && got.msgs != count {
		return fmt.Errorf("got the bytes in %v messages, want %v", got.msgs, count)
	}
	return nil
}

// crossDropLimit is how many whole messages a libsrt listener may drop at
// loss and still pass: now and then a lost packet is not sent again within
// the latency, and the receiver drops it; more is something else.
const crossDropLimit = 3

// droppedMessages tells how many whole messages of want are missing from got,
// when got is the rest of want once each and in order, the last one there,
// then only null packets, the last maybe cut by the read of a pipe. It is
// false when got differs from want in any other way.
func droppedMessages(got, want []byte) (int, bool) {
	dropped, at, lastThere := 0, 0, false
	for i := 0; i+MessageSize <= len(want); i += MessageSize {
		lastThere = at+MessageSize <= len(got) && bytes.Equal(got[at:at+MessageSize], want[i:i+MessageSize])
		if lastThere {
			at += MessageSize
		} else {
			dropped++
		}
	}
	if !lastThere {
		return 0, false
	}
	null := nullMessage()[:ts.PacketSize]
	for ; at < len(got); at += ts.PacketSize {
		rest := got[at:min(at+ts.PacketSize, len(got))]
		if !bytes.Equal(rest, null[:len(rest)]) {
			return 0, false
		}
	}
	return dropped, true
}

// packetsFrom tells which sent TS packets the next few received ones are,
// from the packet of byte at, to show a loss, a duplicate or a reorder.
func packetsFrom(got, want []byte, at int) string {
	const p = ts.PacketSize
	index := map[string]int{}
	for i := len(want)/p - 1; i >= 0; i-- {
		index[string(want[i*p:(i+1)*p])] = i
	}
	var s []string
	for i := at / p; i < at/p+8 && (i+1)*p <= len(got); i++ {
		if j, ok := index[string(got[i*p:(i+1)*p])]; ok {
			s = append(s, fmt.Sprintf("%v is %v", i, j))
		} else {
			s = append(s, fmt.Sprintf("%v is none", i))
		}
	}
	return "received TS packet " + strings.Join(s, ", ")
}

// crossRemuxSlack is how far the end of each track of a pair with ffmpeg may
// be from the messages': the frame the last message cuts short, which ffmpeg
// passes on and the analyzer drops, the last PES of audio, which holds
// several frames, and what ffmpeg holds or libsrt drops when the caller
// closes.
const crossRemuxSlack = 200 * time.Millisecond

// near tells whether a is within d of b.
func near(a, b, d time.Duration) bool { return a >= b-d && a <= b+d }

// checkRemux checks that a remuxed stream has the codecs and the frames of
// the messages, in order, from the first to near the last, with no gap.
func checkRemux(got, want *report.Report) error {
	if got == nil || got.Video == nil || got.Audio == nil {
		return fmt.Errorf("got %v, want %v", got, want)
	}
	v, a := got.Video, got.Audio
	if v.Codec != want.Video.Codec || a.Codec != want.Audio.Codec || v.Width != want.Video.Width || v.LeadingFrames != 0 {
		return fmt.Errorf("got %v, want %v", got, want)
	}
	if v.FirstDTS != want.Video.FirstDTS || a.FirstDTS != want.Audio.FirstDTS ||
		!near(v.LastDTS, want.Video.LastDTS, crossRemuxSlack) || !near(a.LastDTS, want.Audio.LastDTS, crossRemuxSlack) {
		return fmt.Errorf("got %v, want %v, up to %v off at the end", got, want, crossRemuxSlack)
	}
	// ffmpeg rounds the audio times to its 90kHz clock, which moves a gap by
	// less than a millisecond.
	if v.Backwards != 0 || a.Backwards != 0 || v.MaxGap > want.Video.MaxGap+time.Millisecond || a.MaxGap > want.Audio.MaxGap+time.Millisecond {
		return fmt.Errorf("got %v with gaps of %v and %v, want the frames of %v in order with gaps of %v and %v", got, v.MaxGap, a.MaxGap, want, want.Video.MaxGap, want.Audio.MaxGap)
	}
	return nil
}

// crossOptions are the options of a client in a mode.
func crossOptions(m crossMode) Options {
	return Options{StreamID: "#!::r=live/cross,m=publish", Passphrase: m.passphrase, PBKeyLen: m.keylen, Latency: m.latency}
}

// dialClient dials with a client, again while nothing answers, as a listener
// in a process may not listen yet; a refusal is returned at once.
func dialClient(cl Client) func(t *testing.T, ctx context.Context, addr string, m crossMode) (crossSend, error) {
	return func(t *testing.T, ctx context.Context, addr string, m crossMode) (crossSend, error) {
		opts := crossOptions(m)
		opts.Timeout = 500 * time.Millisecond
		deadline := time.Now().Add(3 * time.Second)
		for {
			c, err := cl.Dial(ctx, addr, opts)
			var rej *RejectError
			if err == nil {
				return c, nil
			}
			if errors.As(err, &rej) || ctx.Err() != nil || time.Now().After(deadline) {
				return nil, err
			}
		}
	}
}

// crossListenPeer is a listener of a pure-Go library, which reads messages.
func crossListenPeer(listen func(t *testing.T, passphrase string, keylen int, serve func(c peer, streamID string)) string) func(t *testing.T, ctx context.Context, m crossMode, want int) (string, <-chan crossResult) {
	return func(t *testing.T, ctx context.Context, m crossMode, want int) (string, <-chan crossResult) {
		got := make(chan crossResult, 1)
		var once atomic.Bool
		addr := listen(t, m.passphrase, m.keylen, func(c peer, streamID string) {
			defer c.Close()
			if !once.CompareAndSwap(false, true) {
				return
			}
			stop := context.AfterFunc(ctx, func() { c.Close() })
			defer stop()
			var r crossResult
			b := make([]byte, 2048)
			for want == 0 || len(r.data) < want {
				n, err := c.Read(b)
				if err != nil {
					break
				}
				r.data = append(r.data, b[:n]...)
				r.msgs++
			}
			got <- r
		})
		return addr, got
	}
}

// readAll reads r until want bytes, its end or ctx, and sends the result.
func readAll(ctx context.Context, r *os.File, want int, got chan<- crossResult) {
	stop := context.AfterFunc(ctx, func() { r.SetReadDeadline(time.Now()) })
	defer stop()
	var res crossResult
	b := make([]byte, 64*1024)
	for want == 0 || len(res.data) < want {
		n, err := r.Read(b)
		res.data = append(res.data, b[:n]...)
		if err != nil || ctx.Err() != nil {
			break
		}
	}
	got <- res
}

// startProcess runs cmd until the test ends, then runs cleanup and kills it.
// It returns its stdin, which stays open until then unless closed, its stdout,
// and the lines of its stderr, which a caller must drain, as drainLines does.
func startProcess(t *testing.T, cmd *exec.Cmd, cleanup func()) (stdin io.WriteCloser, stdout *os.File, lines <-chan string) {
	t.Helper()
	stdin, err := cmd.StdinPipe()
	if err != nil {
		t.Fatal(err)
	}
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	cmd.Stdout = w
	errR, err := cmd.StderrPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	w.Close()
	exited := make(chan struct{})
	var logs strings.Builder
	var mu sync.Mutex
	all := make(chan string, 64)
	go func() {
		s := bufio.NewScanner(errR)
		for s.Scan() {
			mu.Lock()
			fmt.Fprintf(&logs, "%v\n", s.Text())
			mu.Unlock()
			all <- s.Text()
		}
		close(all)
		cmd.Wait()
		close(exited)
	}()
	t.Cleanup(func() {
		stdin.Close()
		if cleanup != nil {
			cleanup()
		}
		cmd.Process.Kill()
		<-exited
		r.Close()
		if t.Failed() {
			mu.Lock()
			t.Logf("%v: %v", cmd.Args[0], strings.TrimSpace(logs.String()))
			mu.Unlock()
		}
	})
	return stdin, r, all
}

// drainLines drops the rest of the lines of a process's stderr.
// It returns a channel closed at the end of stderr, when the process ends.
func drainLines(lines <-chan string) <-chan struct{} {
	ended := make(chan struct{})
	go func() {
		defer close(ended)
		for range lines {
		}
	}()
	return ended
}

// crossListenLibsrt is the srt-libsrt tool's listener.
func crossListenLibsrt(t *testing.T, ctx context.Context, m crossMode, want int) (string, <-chan crossResult) {
	path, err := LibsrtPath()
	if err != nil {
		t.Fatal(err)
	}
	args := []string{"listen"}
	if m.passphrase != "" {
		args = append(args, "-passphrase", m.passphrase, "-pbkeylen", strconv.Itoa(m.keylen))
	}
	cmd := exec.Command(path, append(args, "127.0.0.1:0")...)
	_, stdout, lines := startProcess(t, cmd, nil)
	var addr string
	for line := range lines {
		var e struct{ Event, Addr, Error string }
		if json.Unmarshal([]byte(line), &e) == nil && e.Event == "listening" {
			addr = e.Addr
			break
		}
		if e.Event == "error" {
			t.Fatalf("srt-libsrt listen: %v", e.Error)
		}
	}
	if addr == "" {
		t.Fatal("srt-libsrt listen ended before it listened")
	}
	drainLines(lines)
	got := make(chan crossResult, 1)
	go readAll(ctx, stdout, want, got)
	return addr, got
}

// crossListenFFmpeg is ffmpeg's listener, which remuxes what it receives to
// its stdout.
func crossListenFFmpeg(t *testing.T, ctx context.Context, m crossMode, want int) (string, <-chan crossResult) {
	path, err := FFmpegPath()
	if err != nil {
		t.Fatal(err)
	}
	port := freeUDPPort(t)
	addr := "127.0.0.1:" + strconv.Itoa(port)
	args := []string{"-hide_banner", "-loglevel", "error", "-nostdin", "-analyzeduration", "1000000", "-f", "mpegts"}
	if m.passphrase != "" {
		args = append(args, "-passphrase", m.passphrase, "-pbkeylen", strconv.Itoa(m.keylen))
	}
	args = append(args, "-i", "srt://"+addr+"?mode=listener&transtype=live",
		"-map", "0", "-c", "copy", "-copyts", "-muxdelay", "0", "-f", "mpegts", "-flush_packets", "1", "pipe:1")
	_, stdout, lines := startProcess(t, exec.Command(path, args...), nil)
	drainLines(lines)
	waitUDPBound(t, port)
	got := make(chan crossResult, 1)
	go readAll(ctx, stdout, 0, got)
	return addr, got
}

// waitUDPBound waits up to 3s until a process binds the loopback UDP port.
func waitUDPBound(t *testing.T, port int) {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		pc, err := net.ListenPacket("udp", "127.0.0.1:"+strconv.Itoa(port))
		if err != nil {
			return
		}
		pc.Close()
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatalf("nothing listens on UDP port %v", port)
}

// needInterop skips a test of libsrt's tools when InteropReady says why.
func needInterop(t *testing.T) {
	t.Helper()
	if err := InteropReady(); err != nil {
		t.Skip(err)
	}
}

// interopURI is the srt URI of a tool, with a passphrase in a mode.
func interopURI(hostPort string, mode string, m crossMode) string {
	q := []string{"mode=" + mode, "transtype=live"}
	if m.passphrase != "" {
		q = append(q, "passphrase="+m.passphrase, "pbkeylen="+strconv.Itoa(m.keylen))
	}
	if m.latency != 0 && mode == "caller" {
		q = append(q, "latency="+strconv.FormatInt(m.latency.Milliseconds(), 10))
	}
	return "srt://" + hostPort + "?" + strings.Join(q, "&")
}

// dialInterop runs a tool that sends its stdin to addr.
func dialInterop(tool interopTool) func(t *testing.T, ctx context.Context, addr string, m crossMode) (crossSend, error) {
	return func(t *testing.T, ctx context.Context, addr string, m crossMode) (crossSend, error) {
		args := append(append([]string{}, tool.callerArgs...), "file://con", interopURI(interopHost(addr), "caller", m))
		cmd, _, rm := interopRun(tool, nil, args...)
		stdin, _, lines := startProcess(t, cmd, rm)
		var logs []string
		for {
			select {
			case <-ctx.Done():
				return nil, fmt.Errorf("%v did not connect: %w: %v", tool.name, ctx.Err(), strings.Join(logs, "; "))
			case line, ok := <-lines:
				if !ok {
					return nil, fmt.Errorf("%v ended: %v", tool.name, strings.Join(logs, "; "))
				}
				if !interopConnectedRE.MatchString(line) {
					if len(logs) < 20 {
						logs = append(logs, line)
					}
					continue
				}
				drainLines(lines)
				return &interopSend{stdin: stdin, rm: rm}, nil
			}
		}
	}
}

// An interopSend is a tool's stdin; Close removes its container.
type interopSend struct {
	stdin io.WriteCloser
	rm    func()
}

func (s *interopSend) Write(p []byte) (int, error) { return s.stdin.Write(p) }

func (s *interopSend) Close() error {
	s.stdin.Close()
	s.rm()
	return nil
}

// listenInterop runs a tool that listens and writes what it receives to its
// stdout.
func listenInterop(tool interopTool) func(t *testing.T, ctx context.Context, m crossMode, want int) (string, <-chan crossResult) {
	return func(t *testing.T, ctx context.Context, m crossMode, want int) (string, <-chan crossResult) {
		// The port was free a moment ago; another test may take it before
		// Docker binds it, and docker run then ends, so it is tried again.
		for try := 1; ; try++ {
			port := freeUDPPort(t)
			cmd, name, rm := interopRun(tool, []int{port}, interopURI(":"+strconv.Itoa(port), "listener", m), "file://con")
			_, stdout, lines := startProcess(t, cmd, rm)
			ended := drainLines(lines)
			if waitInteropBound(t, ctx, name, port, ended) {
				got := make(chan crossResult, 1)
				go readAll(ctx, stdout, want, got)
				return "127.0.0.1:" + strconv.Itoa(port), got
			}
			if try == 3 {
				t.Fatalf("%v ended %v times before it listened", tool.name, try)
			}
			t.Logf("%v on port %v ended before it listened, try %v", tool.name, port, try)
		}
	}
}

// waitInteropBound waits until the tool in a container binds its UDP port, as
// the container's /proc/net/udp shows: Docker Desktop binds the host's port
// at the start, and srt-test-live says nothing when it listens. It is false
// when the process ends first.
func waitInteropBound(t *testing.T, ctx context.Context, name string, port int, ended <-chan struct{}) bool {
	t.Helper()
	hex := fmt.Sprintf(":%04X ", port)
	for ctx.Err() == nil {
		out, err := exec.CommandContext(ctx, "docker", "exec", name, "cat", "/proc/net/udp", "/proc/net/udp6").Output()
		if err == nil && strings.Contains(string(out), hex) {
			return true
		}
		select {
		case <-ended:
			return false
		case <-time.After(20 * time.Millisecond):
		}
	}
	t.Fatalf("the tool in %v did not bind UDP port %v: %v", name, port, ctx.Err())
	return false
}
