// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"context"
	"strings"
	"testing"
	"time"
)

// TestLibsrtArgs checks that every option becomes the tool's flag of the
// same libsrt name, and an unset one none.
func TestLibsrtArgs(t *testing.T) {
	on, off := true, false
	got := strings.Join(libsrtArgs(Options{
		StreamID: "#!::r=live/s,m=publish", Passphrase: "a-long-secret", PBKeyLen: 32,
		Latency: 200 * time.Millisecond, RecvLatency: 300 * time.Millisecond,
		PeerIdleTimeout: 2 * time.Second, PayloadSize: 1316, MSS: 1400, MaxBW: 1000000,
		SendBuf: 2000000, RecvBuf: 3000000, TSBPDMode: &on, TLPktDrop: &off,
	}, 1500*time.Millisecond), " ")
	want := "-conntimeo 1500 -streamid #!::r=live/s,m=publish -passphrase a-long-secret -pbkeylen 32 " +
		"-latency 200 -rcvlatency 300 -peerlatency 200 -peeridletimeo 2000 -mss 1400 -payloadsize 1316 " +
		"-maxbw 1000000 -sndbuf 2000000 -rcvbuf 3000000 -tsbpdmode on -tlpktdrop off"
	if got != want {
		t.Errorf("args\n%v\nwant\n%v", got, want)
	}
	if got := strings.Join(libsrtArgs(Options{}, time.Second), " "); got != "-conntimeo 1000" {
		t.Errorf("args of no options %q, want only the handshake timeout", got)
	}
}

// TestLibsrtMissing checks that a missing tool fails the dial with how to
// build it.
func TestLibsrtMissing(t *testing.T) {
	t.Setenv("SRT_LIBSRT", t.TempDir()+"/none")
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := Libsrt.Dial(ctx, "127.0.0.1:1", Options{})
	if err == nil || !strings.Contains(err.Error(), "srt-libsrt-build.sh") {
		t.Errorf("dial with no tool: %v, want how to build it", err)
	}
}

// TestLibsrtNoListener checks that a call nobody answers fails after the
// handshake timeout, with no reject reason.
func TestLibsrtNoListener(t *testing.T) {
	t.Parallel()
	needClient(t, Libsrt)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	t0 := time.Now()
	_, err := Libsrt.Dial(ctx, "127.0.0.1:9", Options{Timeout: 300 * time.Millisecond})
	if err == nil || strings.Contains(err.Error(), "rejected") {
		t.Errorf("dial with no listener: %v, want a timeout", err)
	}
	if d := time.Since(t0); d > 2*time.Second {
		t.Errorf("gave up after %v, want about 300ms", d)
	}
}

// TestLibsrtGroupArgs checks the tool's options of a group: its mode, then
// each link.
func TestLibsrtGroupArgs(t *testing.T) {
	got := strings.Join(libsrtGroupArgs(GroupBackup, []string{"127.0.0.1:1", "127.0.0.1:2"}), " ")
	if want := "-group backup -link 127.0.0.1:1 -link 127.0.0.1:2"; got != want {
		t.Errorf("args %q, want %q", got, want)
	}
}
