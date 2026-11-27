// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"context"
	"encoding/json"
	"fmt"
	"hash/fnv"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

// The group cross-check sends the TS messages of the cross-check from each
// socket group caller over two links, each through a relay of its own, to each
// library that accepts socket groups as a listener, before SRS takes groups:
// clean, and with the link that carries the media blackholed halfway, when
// every message must still arrive once and in order over the other link.

// A groupMode is how a group pair runs.
type groupMode struct {
	name     string
	mode     GroupMode
	linkDown bool
}

var groupModes = []groupMode{
	{name: "Broadcast", mode: GroupBroadcast},
	{name: "BroadcastLinkDown", mode: GroupBroadcast, linkDown: true},
	{name: "Backup", mode: GroupBackup},
	{name: "BackupLinkDown", mode: GroupBackup, linkDown: true},
}

// groupLatency is the latency of each link: a backup group moves to its other
// link only after the active one has been silent for a while, then sends
// again what was not acknowledged, which must still be in time.
const groupLatency = 300 * time.Millisecond

// groupLinks is how many links each group has.
const groupLinks = 2

// A groupCaller is one library as a group caller.
type groupCaller struct {
	name string
	need func(t *testing.T)
	dial func(t *testing.T, ctx context.Context, mode GroupMode, links []string) (crossSend, error)
}

// A groupInfo is what a group listener tells of what it accepted: whether it
// was a group, and the most links it had. tells is false for a listener that
// says neither.
type groupInfo struct {
	tells bool
	group bool
	links int
}

// A groupListener is one library as a listener that accepts groups. listen
// returns the address to call, the channel of what it receives, as a
// crossListener's, and info, which says what it accepted, once it knows or the
// listener ended, waiting at most a timeout.
type groupListener struct {
	name   string
	need   func(t *testing.T)
	listen func(t *testing.T, ctx context.Context, want int) (string, <-chan crossResult, func(time.Duration) groupInfo)
}

var groupCallers = []groupCaller{
	{name: "zsiec", need: noNeed, dial: dialGroupClient(zsiecClient{})},
	{name: "libsrt", need: needLibsrt, dial: dialGroupClient(libsrtClient{})},
	{name: "srtTestLive", need: needInterop, dial: dialGroupClient(TestLive)},
}

var groupListeners = []groupListener{
	{name: "libsrt", need: needLibsrt, listen: groupListenLibsrt},
	{name: "srtTestLive", need: needInterop, listen: groupListenTestLive},
}

// knownGroupFailure tells whether a group caller fails with a listener, and
// why, or whether the pair can't be checked; each is logged in the task file.
func knownGroupFailure(caller string, ln groupListener) (reason string, known, skip bool) {
	if caller != "zsiec" {
		return "", false, false
	}
	reason = "zsiec v0.4.0's Group leaves the group out of the handshake, so libsrt accepts each link as a connection of its own: the tool's listener takes the first link as a plain caller, and the second link finds no listener"
	if ln.name == "srtTestLive" {
		return reason + "; srt-test-live does not say what it accepted", false, true
	}
	return reason, true, false
}

// TestGroupCrossCheck runs each group caller against each group listener in
// each mode.
func TestGroupCrossCheck(t *testing.T) {
	msgs := crossMessages(t)
	var all []byte
	for _, b := range msgs {
		all = append(all, b...)
	}

	for _, cl := range groupCallers {
		for _, ln := range groupListeners {
			for _, m := range groupModes {
				t.Run(cl.name+"To"+strings.ToUpper(ln.name[:1])+ln.name[1:]+"/"+m.name, func(t *testing.T) {
					t.Parallel()
					cl.need(t)
					ln.need(t)
					reason, known, skip := knownGroupFailure(cl.name, ln)
					if skip {
						t.Skip(reason)
					}
					ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
					defer cancel()
					got, info, err := groupRun(t, ctx, cl, ln, m, msgs)
					if err == nil {
						err = checkExact(got, all, crossCount, false)
					}
					if err == nil && info.tells && (!info.group || info.links != groupLinks) {
						err = fmt.Errorf("the listener accepted %+v, want a group of %v links", info, groupLinks)
					}
					if known {
						if err == nil {
							t.Errorf("the pair passes now; drop it from knownGroupFailure: %v", reason)
						} else {
							t.Logf("known: %v (%v)", reason, err)
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

// groupRun connects the caller to the listener over groupLinks relays, sends
// msgs, with the carrying link blackholed halfway in a link-down mode, and
// returns what the listener received and what it accepted.
func groupRun(t *testing.T, ctx context.Context, cl groupCaller, ln groupListener, m groupMode, msgs [][]byte) (crossResult, groupInfo, error) {
	lctx, stopListener := context.WithCancel(ctx)
	defer stopListener()
	addr, got, info := ln.listen(t, lctx, len(msgs)*len(msgs[0]))

	h := fnv.New64a()
	h.Write([]byte(t.Name()))
	seed := int64(h.Sum64() >> 1)
	var relays []*Impair
	var links []string
	for i := range groupLinks {
		relay, err := NewImpair(addr, seed+int64(i))
		if err != nil {
			t.Fatal(err)
		}
		defer relay.Close()
		relays = append(relays, relay)
		links = append(links, relay.Addr().String())
	}
	defer func() {
		for i, relay := range relays {
			t.Logf("link %v relay seed %v: up %+v, down %+v", i, relay.Seed(), relay.Counters(Up), relay.Counters(Down))
		}
	}()

	c, err := cl.dial(t, ctx, m.mode, links)
	if err != nil {
		return crossResult{}, groupInfo{}, fmt.Errorf("dial: %w", err)
	}
	closed := false
	defer func() {
		if !closed {
			c.Close()
		}
	}()
	// A caller may say it connected when its first link did, so each link's
	// handshake answers must have passed its relay.
	if err := waitGroupLinks(ctx, relays); err != nil {
		return crossResult{}, groupInfo{}, err
	}

	tick := time.NewTicker(5 * time.Millisecond)
	defer tick.Stop()
	for i, b := range msgs {
		if m.linkDown && i == len(msgs)/2 {
			down := groupCarrier(relays)
			t.Logf("link %v blackholed after %v messages", down, i)
			relays[down].Blackhole(true)
		}
		if _, err := c.Write(b); err != nil {
			return crossResult{}, groupInfo{}, fmt.Errorf("write of message %v: %w", i, err)
		}
		if i%10 == 9 {
			select {
			case <-ctx.Done():
				return crossResult{}, groupInfo{}, ctx.Err()
			case <-tick.C:
			}
		}
	}

	// As in crossRun, null messages follow until the listener has it all.
	settle := time.NewTimer(groupLatency + crossSettle)
	defer settle.Stop()
	pad := time.NewTicker(10 * time.Millisecond)
	defer pad.Stop()
	null := nullMessage()
	var r crossResult
	received, done := false, false
	for !done {
		select {
		case r = <-got:
			received, done = true, true
		case <-settle.C:
			done = true
		case <-ctx.Done():
			return crossResult{}, groupInfo{}, ctx.Err()
		case <-pad.C:
			if _, err := c.Write(null); err != nil {
				done = true
			}
		}
	}
	c.Close()
	closed = true
	if !received {
		stop := time.NewTimer(crossListenerGrace)
		defer stop.Stop()
		select {
		case r = <-got:
		case <-stop.C:
			stopListener()
			r = <-got
		}
	}
	return r, info(crossListenerGrace), r.err
}

// waitGroupLinks waits up to 2s until each relay has passed the listener's
// induction and conclusion answers to the caller.
func waitGroupLinks(ctx context.Context, relays []*Impair) error {
	deadline := time.Now().Add(2 * time.Second)
	for {
		var waiting []int
		for i, relay := range relays {
			if relay.Counters(Down).Sent < 2 {
				waiting = append(waiting, i)
			}
		}
		if len(waiting) == 0 {
			return nil
		}
		if time.Now().After(deadline) || ctx.Err() != nil {
			return fmt.Errorf("links %v did not connect", waiting)
		}
		time.Sleep(5 * time.Millisecond)
	}
}

// groupCarrier is the link that carried the most data packets up: every link
// of a broadcast group, the first one, and the active one of a backup group.
func groupCarrier(relays []*Impair) int {
	best := 0
	for i, relay := range relays {
		if relay.DataCounters(Up).Received > relays[best].DataCounters(Up).Received {
			best = i
		}
	}
	return best
}

// groupOptions are the options of each link.
func groupOptions() Options {
	return Options{StreamID: "#!::r=live/cross,m=publish", Latency: groupLatency, Timeout: time.Second}
}

// dialGroupClient dials a group with a client.
func dialGroupClient(cl GroupClient) func(t *testing.T, ctx context.Context, mode GroupMode, links []string) (crossSend, error) {
	return func(t *testing.T, ctx context.Context, mode GroupMode, links []string) (crossSend, error) {
		c, err := cl.DialGroup(ctx, mode, links, groupOptions())
		if err != nil {
			return nil, err
		}
		if c.Links() != len(links) {
			c.Close()
			return nil, fmt.Errorf("%v links of %v connected", c.Links(), len(links))
		}
		return c, nil
	}
}

// groupListenLibsrt is the srt-libsrt tool's listener with groupconnect on,
// whose events tell whether it accepted a group, and its links.
func groupListenLibsrt(t *testing.T, ctx context.Context, want int) (string, <-chan crossResult, func(time.Duration) groupInfo) {
	path, err := LibsrtPath()
	if err != nil {
		t.Fatal(err)
	}
	args := []string{"listen", "-groupconnect", "on", "-latency", strconv.FormatInt(groupLatency.Milliseconds(), 10), "127.0.0.1:0"}
	_, stdout, lines := startProcess(t, exec.Command(path, args...), nil)
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

	// A link blackholed keeps the group open until libsrt breaks it, after
	// about 5s, so the most links come from the events as they go.
	var mu sync.Mutex
	info := groupInfo{tells: true}
	ended := make(chan struct{})
	go func() {
		defer close(ended)
		for line := range lines {
			var e struct {
				Event string
				Group bool
				Links int
			}
			if json.Unmarshal([]byte(line), &e) != nil {
				continue
			}
			mu.Lock()
			if e.Event == "connected" {
				info.group = e.Group
			}
			info.links = max(info.links, e.Links)
			mu.Unlock()
		}
	}()
	got := make(chan crossResult, 1)
	go readAll(ctx, stdout, want, got)
	return addr, got, func(timeout time.Duration) groupInfo {
		deadline := time.Now().Add(timeout)
		for {
			mu.Lock()
			r := info
			mu.Unlock()
			select {
			case <-ended:
				return r
			default:
			}
			if (r.group && r.links >= groupLinks) || time.Now().After(deadline) {
				return r
			}
			time.Sleep(10 * time.Millisecond)
		}
	}
}

// groupListenTestLive is srt-test-live listening with groupconnect, which
// says nothing of what it accepted.
func groupListenTestLive(t *testing.T, ctx context.Context, want int) (string, <-chan crossResult, func(time.Duration) groupInfo) {
	addr, got := startInteropListener(t, ctx, interopTestLive, want, func(port int) string {
		return "srt://:" + strconv.Itoa(port) + "?mode=listener&transtype=live&groupconnect=1&latency=" + strconv.FormatInt(groupLatency.Milliseconds(), 10)
	})
	return addr, got, func(time.Duration) groupInfo { return groupInfo{} }
}
