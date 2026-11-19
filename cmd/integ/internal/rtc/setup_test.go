// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"errors"
	"fmt"
	"testing"
	"time"
)

// TestRawSetupTimes times the setup of sessions with a fake server over a
// path with a one-way delay. Without SPED the client checks the pair, then
// does the two round trips of DTLS 1.2: three round trips. With SPED the
// ClientHello rides in the first check, which saves one.
func TestRawSetupTimes(t *testing.T) {
	const delay = 30 * time.Millisecond
	rtt := 2 * delay
	times := map[bool]SetupTimes{}
	for _, sped := range []bool{false, true} {
		t.Run(fmt.Sprintf("SPED=%v", sped), func(t *testing.T) {
			srv := newFakeRaw(t, true)
			if sped {
				srv = newFakeSped(t, false, false)
			}
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{SPED: sped, Faults: &Faults{Delay: delay}}, true)
			if err != nil {
				t.Fatal(err)
			}
			defer p.Close()
			st := p.Setup()
			trips := map[bool]int{false: 3, true: 2}[sped]
			if st.Answer <= 0 || st.DTLS < st.Answer+time.Duration(trips)*rtt {
				t.Errorf("setup %+v, want the answer, then DTLS done at least %v round trips of %v later", st, trips, rtt)
			}
			if limit := st.Answer + time.Duration(trips+1)*rtt + 50*time.Millisecond; st.DTLS > limit {
				t.Errorf("setup %+v, want DTLS done by %v, within %v round trips of %v after the answer", st, limit, trips+1, rtt)
			}
			times[sped] = st
		})
	}
	if raw, sped := times[false], times[true]; sped.DTLS-sped.Answer >= raw.DTLS-raw.Answer {
		t.Errorf("SPED %+v, raw %+v, want SPED done sooner after the answer", sped, raw)
	}
}

// TestRawSetupTimesWithoutDTLS has no DTLS time for a session without DTLS.
func TestRawSetupTimesWithoutDTLS(t *testing.T) {
	srv := newFakeRaw(t, false)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", nil, false)
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	if st := p.Setup(); st.Answer <= 0 || st.DTLS != 0 {
		t.Errorf("setup %+v, want an answer time and no DTLS time", st)
	}
}

// TestRawFirstPacket times the first RTP packet a player gets, which comes
// after the handshake and once the server sends, and gives up when ctx
// ends before any comes.
func TestRawFirstPacket(t *testing.T) {
	srv := newFakeRaw(t, true)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", nil, true)
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()

	short, cancelShort := context.WithTimeout(ctx, 100*time.Millisecond)
	defer cancelShort()
	if d, err := p.FirstPacket(short); !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("first packet %v, %v before the server sent, want the deadline", d, err)
	}

	const wait = 100 * time.Millisecond
	time.Sleep(wait)
	srv.sendSamples(t, readSamples(t, 200*time.Millisecond))
	d, err := p.FirstPacket(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if st := p.Setup(); d < st.DTLS+2*wait {
		t.Errorf("first packet at %v, setup %+v, want after the handshake and the %v waits", d, st, wait)
	}
	if again, err := p.FirstPacket(ctx); err != nil || again != d {
		t.Errorf("first packet again %v, %v, want %v", again, err, d)
	}
}
