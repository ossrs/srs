// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"context"
	"errors"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/ts"
)

// These tests check the client against libsrt: an ffmpeg with libsrt listens,
// and the client calls it.

// ffmpegSRT returns the path of an ffmpeg with libsrt, or skips the test.
func ffmpegSRT(t *testing.T) string {
	t.Helper()
	ff, err := media.FindFFmpeg()
	if err != nil {
		t.Skipf("no ffmpeg: %v", err)
	}
	if !ff.HasProtocol("srt") {
		t.Skipf("ffmpeg %v lacks libsrt", ff.Path)
	}
	return ff.Path
}

// freeUDPPort returns a UDP port on the loopback interface that was free a
// moment ago.
func freeUDPPort(t *testing.T) int {
	t.Helper()
	pc, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer pc.Close()
	return pc.LocalAddr().(*net.UDPAddr).Port
}

// startFFmpeg runs ffmpeg with args until the test ends, and returns a channel
// that gets its result and its output when it exits.
func startFFmpeg(t *testing.T, ctx context.Context, path string, args ...string) <-chan error {
	t.Helper()
	cmd := exec.CommandContext(ctx, path, append([]string{"-hide_banner", "-loglevel", "error", "-nostdin", "-y"}, args...)...)
	var out strings.Builder
	cmd.Stdout, cmd.Stderr = &out, &out
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	done, exited := make(chan error, 1), make(chan struct{})
	go func() {
		defer close(exited)
		err := cmd.Wait()
		if err != nil {
			err = fmt.Errorf("%w: %s", err, out.String())
		}
		done <- err
	}()
	t.Cleanup(func() {
		cmd.Process.Kill()
		<-exited
	})
	return done
}

// dialRetry calls addr until ffmpeg listens, for up to 3s; a rejected
// handshake is returned at once.
func dialRetry(t *testing.T, ctx context.Context, addr string, opts Options) (*Conn, error) {
	t.Helper()
	opts.Timeout = 100 * time.Millisecond
	deadline := time.Now().Add(3 * time.Second)
	for {
		c, err := Dial(ctx, addr, opts)
		var rej *RejectError
		if err == nil || errors.As(err, &rej) || time.Now().After(deadline) {
			return c, err
		}
	}
}

// TestFFmpegReceives publishes 2s of the TS test file to ffmpeg, which
// writes the first second it gets to a file.
func TestFFmpegReceives(t *testing.T) {
	ff := ffmpegSRT(t)
	file := media.Path(t, media.H264HighAACTS)
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	port := freeUDPPort(t)
	out := filepath.Join(t.TempDir(), "got.ts")
	done := startFFmpeg(t, ctx, ff, "-i", fmt.Sprintf("srt://127.0.0.1:%v?mode=listener", port), "-t", "1", "-c", "copy", "-f", "mpegts", out)

	c, err := dialRetry(t, ctx, fmt.Sprintf("127.0.0.1:%v", port), Options{StreamID: "#!::r=live/s,m=publish"})
	if err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(file)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if err := NewSender(f).SendThrough(c, 2*time.Second); err != nil {
		t.Fatal(err)
	}
	// Let the messages arrive before the shutdown.
	time.Sleep(300 * time.Millisecond)
	c.Close()
	if err := <-done; err != nil {
		t.Fatalf("ffmpeg: %v", err)
	}

	g, err := os.Open(out)
	if err != nil {
		t.Fatal(err)
	}
	defer g.Close()
	r, err := ts.Analyze(ts.NewDemuxer(g), report.Until{})
	if err != nil {
		t.Fatalf("%v: %v", err, r)
	}
	if v, a := r.Video, r.Audio; v == nil || a == nil || v.Codec != "h264" || a.Codec != "aac" || v.Keyframes < 2 || v.Width != 768 {
		t.Errorf("ffmpeg got %v, want h264 768x320 with 2 keyframes and aac", r)
	}
}

// TestFFmpegSends plays what ffmpeg sends in real time, and checks that it
// refuses a wrong passphrase.
func TestFFmpegSends(t *testing.T) {
	ff := ffmpegSRT(t)
	file := media.Path(t, media.H264HighAACTS)
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	const secret = "the-ffmpeg-secret"
	port := freeUDPPort(t)
	startFFmpeg(t, ctx, ff, "-re", "-i", file, "-t", "3", "-c", "copy", "-f", "mpegts",
		fmt.Sprintf("srt://127.0.0.1:%v?mode=listener&passphrase=%v&pbkeylen=16", port, secret))
	addr := fmt.Sprintf("127.0.0.1:%v", port)

	_, err := dialRetry(t, ctx, addr, Options{Passphrase: "a-wrong-secret"})
	var rej *RejectError
	if !errors.As(err, &rej) || rej.Reason != RejectBadSecret {
		t.Errorf("a wrong passphrase: %v, want rejected with %v", err, RejectBadSecret)
	}

	c, err := dialRetry(t, ctx, addr, Options{Passphrase: secret, StreamID: "#!::r=live/s,m=request"})
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	r, err := c.Analyze(ctx, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 10})
	if err != nil {
		t.Fatalf("%v: %v", err, r)
	}
	if v, a := r.Video, r.Audio; v.Codec != "h264" || a.Codec != "aac" || v.Width != 768 || a.SampleRate != 44100 {
		t.Errorf("got %v, want h264 768x320 and aac 44100Hz", r)
	}
}
