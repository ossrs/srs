// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/signal"
	"syscall"
	"time"

	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
)

// publish sends an FLV file to an RTMP URL in real time, until the file ends
// or the process is interrupted.
func publish(file, url string, stdout io.Writer) error {
	if _, err := rtmp.ParseURL(url); err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	f, err := os.Open(file)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	defer f.Close()
	r, err := media.NewFLVReader(f)
	if err != nil {
		return fmt.Errorf("publish: %v: %w", file, err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	c, err := rtmp.Dial(ctx, url, nil)
	if err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	defer c.Close()
	if _, err := c.Publish(ctx, rtmp.FMLE); err != nil {
		return fmt.Errorf("publish: %w", err)
	}
	fmt.Fprintf(stdout, "publishing %v to %v, %v handshake\n", file, url, c.Handshake.Kind())

	start := time.Now()
	stats, err := rtmp.PublishFLV(ctx, c, r, &rtmp.Pacer{})
	fmt.Fprintf(stdout, "sent %v tags (%v video, %v audio), %v of media in %v\n",
		stats.Tags, stats.Video, stats.Audio, stats.Duration, time.Since(start).Round(time.Millisecond))
	if err != nil && ctx.Err() == nil {
		return fmt.Errorf("publish: %w", err)
	}
	return nil
}
