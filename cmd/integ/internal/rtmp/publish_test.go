// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"context"
	"errors"
	"reflect"
	"testing"
	"time"

	"srs-integ/internal/media"
)

// fakeClock is a clock whose sleep moves its time.
type fakeClock struct {
	now    time.Time
	sleeps []time.Duration
}

func (f *fakeClock) Now() time.Time { return f.now }

func (f *fakeClock) Sleep(ctx context.Context, d time.Duration) error {
	if err := ctx.Err(); err != nil {
		return err
	}
	f.sleeps = append(f.sleeps, d)
	f.now = f.now.Add(d)
	return nil
}

func TestPacerRealTime(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	p := &Pacer{Now: clock.Now, Sleep: clock.Sleep}
	ctx := context.Background()

	// The first timestamp is the base, so it doesn't wait, whatever it is.
	steps := []struct {
		ts      uint32
		elapsed time.Duration // How much time passed before this tag.
		want    []time.Duration
	}{
		{5000, 0, nil},
		{5040, 0, []time.Duration{40 * time.Millisecond}},
		{5040, 0, nil},
		{5100, 10 * time.Millisecond, []time.Duration{50 * time.Millisecond}},
		// Behind schedule: no wait.
		{5120, 100 * time.Millisecond, nil},
		// A timestamp that goes back doesn't wait either.
		{5000, 0, nil},
	}
	for i, s := range steps {
		clock.now = clock.now.Add(s.elapsed)
		clock.sleeps = nil
		if err := p.Wait(ctx, s.ts); err != nil {
			t.Fatalf("step %v: %v", i, err)
		}
		if !reflect.DeepEqual(clock.sleeps, s.want) {
			t.Errorf("step %v ts %v: slept %v, want %v", i, s.ts, clock.sleeps, s.want)
		}
	}
}

func TestPacerCancelled(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	p := &Pacer{Now: clock.Now, Sleep: clock.Sleep}
	ctx, cancel := context.WithCancel(context.Background())
	p.Wait(ctx, 0)
	cancel()
	if err := p.Wait(ctx, 1000); !errors.Is(err, context.Canceled) {
		t.Errorf("got %v, want context.Canceled", err)
	}
}

// flvFile makes an FLV file of tags.
func flvFile(tags []*media.FLVTag) []byte {
	b := []byte{'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0}
	for _, tag := range tags {
		n := len(tag.Data)
		ts := tag.Timestamp
		b = append(b, tag.Type, byte(n>>16), byte(n>>8), byte(n), byte(ts>>16), byte(ts>>8), byte(ts), byte(ts>>24), 0, 0, 0)
		b = append(b, tag.Data...)
		b = append(b, be32(uint32(11+n))...)
	}
	return b
}

func TestPublishFLV(t *testing.T) {
	meta, _ := EncodeAMF0("onMetaData", ECMAArray{"duration": float64(1)})
	tags := []*media.FLVTag{
		{Type: media.TagScript, Timestamp: 0, Data: meta},
		{Type: media.TagVideo, Timestamp: 0, Data: []byte{0x17, 0, 0, 0, 0, 1}},
		{Type: media.TagAudio, Timestamp: 0, Data: []byte{0xaf, 0, 0x12, 0x10}},
		{Type: media.TagVideo, Timestamp: 40, Data: []byte{0x17, 1, 0, 0, 0, 2}},
		{Type: media.TagAudio, Timestamp: 46, Data: []byte{0xaf, 1, 3}},
		{Type: media.TagVideo, Timestamp: 80, Data: []byte{0x27, 1, 0, 0, 0, 3}},
	}
	r, err := media.NewFLVReader(bytes.NewReader(flvFile(tags)))
	if err != nil {
		t.Fatal(err)
	}

	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Publish(context.Background(), FMLE); err != nil {
		t.Fatalf("publish: %v", err)
	}

	clock := &fakeClock{now: time.Unix(1000, 0)}
	stats, err := PublishFLV(context.Background(), c, r, &Pacer{Now: clock.Now, Sleep: clock.Sleep})
	if err != nil {
		t.Fatalf("publish FLV: %v", err)
	}
	want := PublishStats{Tags: 6, Video: 3, Audio: 2, Duration: 80 * time.Millisecond}
	if stats != want {
		t.Errorf("stats %+v, want %+v", stats, want)
	}
	var slept time.Duration
	for _, d := range clock.sleeps {
		slept += d
	}
	if slept != 80*time.Millisecond {
		t.Errorf("slept %v, want 80ms, the media duration", slept)
	}
	s.waitFor("every tag", func() bool { return len(s.media) == 6 })
}

// TestPublishFLVNoPacing publishes as fast as it can, for segment groups.
func TestPublishFLVNoPacing(t *testing.T) {
	tags := []*media.FLVTag{
		{Type: media.TagVideo, Timestamp: 0, Data: []byte{0x17, 1, 0, 0, 0, 1}},
		{Type: media.TagVideo, Timestamp: 60000, Data: []byte{0x17, 1, 0, 0, 0, 2}},
	}
	r, _ := media.NewFLVReader(bytes.NewReader(flvFile(tags)))
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Publish(context.Background(), FMLE); err != nil {
		t.Fatalf("publish: %v", err)
	}
	start := time.Now()
	stats, err := PublishFLV(context.Background(), c, r, nil)
	if err != nil {
		t.Fatalf("publish FLV: %v", err)
	}
	if d := time.Since(start); d > time.Second {
		t.Errorf("took %v for 60s of media, want no pacing", d)
	}
	if stats.Tags != 2 || stats.Duration != time.Minute {
		t.Errorf("stats %+v, want 2 tags and 1m", stats)
	}
}

func TestPublishFLVCancelled(t *testing.T) {
	tags := []*media.FLVTag{
		{Type: media.TagVideo, Timestamp: 0, Data: []byte{0x17, 1, 0, 0, 0, 1}},
		{Type: media.TagVideo, Timestamp: 60000, Data: []byte{0x17, 1, 0, 0, 0, 2}},
	}
	r, _ := media.NewFLVReader(bytes.NewReader(flvFile(tags)))
	s := newFakeServer(t)
	s.start()
	c := dial(t, s.url("a"), nil)
	if _, err := c.Publish(context.Background(), FMLE); err != nil {
		t.Fatalf("publish: %v", err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	stats, err := PublishFLV(ctx, c, r, &Pacer{})
	if !errors.Is(err, context.DeadlineExceeded) {
		t.Errorf("got %v, want context.DeadlineExceeded", err)
	}
	if stats.Tags != 1 {
		t.Errorf("stats %+v, want 1 tag sent before the cancel", stats)
	}
}
