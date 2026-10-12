// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"syscall"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/server"
)

// TestTimer_Signal_Smoke sends signals to a server of its own and checks how it
// exits. SRS quits on its 1s main-cycle tick, so these are timer cases.
func TestTimer_Signal_Smoke(t *testing.T) {
	t.Parallel()

	check.Case(t, "TermExitsZero", check.Timer, func(c *check.C) {
		srs := server.Group(c, server.Config{Listeners: []server.Listener{server.HTTPAPI}})
		c.Must(srs.Signal(syscall.SIGTERM), "SIGTERM")
		code, err := srs.Wait(3 * time.Second)
		c.Must(err, "wait")
		c.CheckEqual(code, 0, "exit code after SIGTERM")
	})

	check.Case(t, "TermWithPublisherExitsZero", check.Timer, func(c *check.C) {
		srs := server.Group(c, server.Config{Listeners: []server.Listener{server.RTMP, server.HTTPAPI}})
		startPublisher(c, "rtmp://"+srs.Addr(server.RTMP)+"/live/"+streamName(c), media.Path(c, media.H264HighAAC), 200*time.Millisecond)
		c.Must(srs.Signal(syscall.SIGTERM), "SIGTERM")
		code, err := srs.Wait(3 * time.Second)
		c.Must(err, "wait")
		c.CheckEqual(code, 0, "exit code after SIGTERM with a publisher")
	})
}
