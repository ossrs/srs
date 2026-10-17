// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"context"
	"io"
	"time"

	"srs-integ/internal/media"
)

// A Pacer paces tags in real time by their timestamps, like ffmpeg -re: the
// first timestamp is sent at once, and each later one when as much wall time
// has passed as media time. A tag that is late, or whose timestamp goes back,
// is sent at once. The zero value uses the real clock.
type Pacer struct {
	Now   func() time.Time
	Sleep func(ctx context.Context, d time.Duration) error

	started bool
	start   time.Time
	base    uint32
}

// Wait waits until the tag with timestamp ts is due.
func (p *Pacer) Wait(ctx context.Context, ts uint32) error {
	if err := ctx.Err(); err != nil {
		return err
	}
	now := p.Now
	if now == nil {
		now = time.Now
	}
	if !p.started {
		p.started, p.start, p.base = true, now(), ts
		return nil
	}
	if ts < p.base {
		return nil
	}
	d := p.start.Add(time.Duration(ts-p.base) * time.Millisecond).Sub(now())
	if d <= 0 {
		return nil
	}
	if p.Sleep != nil {
		return p.Sleep(ctx, d)
	}
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-t.C:
		return nil
	}
}

// PublishStats is what PublishFLV sent.
type PublishStats struct {
	Tags, Video, Audio int
	// Duration is from the first timestamp to the last.
	Duration time.Duration
}

// PublishFLV sends every tag of an FLV file on a publishing client, paced by
// p, or as fast as it can when p is nil, as segment groups do. It stops at the
// end of the file, or when ctx is done.
func PublishFLV(ctx context.Context, c *Client, r *media.FLVReader, p *Pacer) (PublishStats, error) {
	var stats PublishStats
	var first uint32
	for {
		tag, err := r.ReadTag()
		if err == io.EOF {
			return stats, nil
		}
		if err != nil {
			return stats, err
		}
		if p != nil {
			err = p.Wait(ctx, tag.Timestamp)
		} else {
			err = ctx.Err()
		}
		if err != nil {
			return stats, err
		}
		if err := c.WriteTag(tag); err != nil {
			return stats, err
		}

		if stats.Tags == 0 {
			first = tag.Timestamp
		}
		stats.Tags++
		switch tag.Type {
		case media.TagVideo:
			stats.Video++
		case media.TagAudio:
			stats.Audio++
		}
		if tag.Timestamp >= first {
			stats.Duration = time.Duration(tag.Timestamp-first) * time.Millisecond
		}
	}
}
