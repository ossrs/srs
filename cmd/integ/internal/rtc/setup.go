// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"time"
)

// SetupTimes is how long a raw session took to reach each step of its
// setup, from when it posted its offer: the answer, and the end of its DTLS
// handshake, 0 without DTLS.
type SetupTimes struct {
	Answer, DTLS time.Duration
}

// Setup is how long the session took to set up.
func (s *RawSession) Setup() SetupTimes {
	return s.setup
}

// FirstPacket waits for the first RTP packet of the answer's codecs, and
// returns when it came, from when the session posted its offer.
func (p *RawPlayer) FirstPacket(ctx context.Context) (time.Duration, error) {
	select {
	case <-p.first:
		return p.firstAt, nil
	case <-ctx.Done():
		return 0, ctx.Err()
	}
}
