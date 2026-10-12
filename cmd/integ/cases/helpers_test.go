// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"os"
	"path"
	"regexp"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
)

var unsafeStreamChars = regexp.MustCompile(`[^A-Za-z0-9_-]+`)

// streamName is a stream name of its own for a case: the case name and a
// random suffix, such as PublishPlay-3fa2c1.
func streamName(c *check.C) string {
	b := make([]byte, 3)
	rand.Read(b)
	return unsafeStreamChars.ReplaceAllString(path.Base(c.Name()), "_") + "-" + hex.EncodeToString(b)
}

// startPublisher publishes a file until the case ends: the media before lead
// at once, before it returns, like a stream that has run for that long, then
// the rest in real time.
func startPublisher(c *check.C, url, file string, lead time.Duration) *rtmp.Client {
	c.Helper()
	f, err := os.Open(file)
	c.Must(err, "open the media")
	r, err := media.NewFLVReader(f)
	c.Must(err, "read the media")

	ctx, cancel := context.WithCancel(context.Background())
	dctx, dcancel := context.WithTimeout(ctx, 2*time.Second)
	defer dcancel()
	pub, err := rtmp.Dial(dctx, url, nil)
	c.Must(err, "dial the publisher")
	_, err = pub.Publish(dctx, rtmp.FMLE)
	c.Must(err, "publish")

	// The first tag at or after lead starts the real-time part.
	var next *media.FLVTag
	for lead > 0 {
		tag, err := r.ReadTag()
		c.Must(err, "read the media")
		if time.Duration(tag.Timestamp)*time.Millisecond >= lead {
			next = tag
			break
		}
		c.Must(pub.WriteTag(tag), "publish the lead")
	}

	done := make(chan error, 1)
	go func() {
		p := &rtmp.Pacer{}
		if next != nil {
			if err := p.Wait(ctx, next.Timestamp); err != nil {
				done <- err
				return
			}
			if err := pub.WriteTag(next); err != nil {
				done <- err
				return
			}
		}
		_, err := rtmp.PublishFLV(ctx, pub, r, p)
		done <- err
	}()
	c.Cleanup(func() {
		cancel()
		pub.Close()
		<-done
		f.Close()
	})
	return pub
}

// playedUntil is when a player of a stream published in real time has seen
// it play: the metadata, both sequence headers, a keyframe and 5 frames.
var playedUntil = report.Until{Metadata: true, VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}

// checkPlayed checks the report of a player that got a test file: its codecs,
// the source's 768x320 and 44.1 kHz stereo, one sequence header each, and
// timestamps that only go forward, without gaps, in sync.
func checkPlayed(c *check.C, r *report.Report, vcodec, acodec string) {
	c.Helper()
	if !c.Check(r.Video != nil && r.Audio != nil, "no video or audio: %v", r) {
		return
	}
	v, a := r.Video, r.Audio
	c.CheckEqual(v.Codec, vcodec, "video codec")
	c.Check(v.Width == 768 && v.Height == 320, "size %vx%v, want 768x320", v.Width, v.Height)
	c.CheckEqual(v.SequenceHeaders, 1, "video sequence headers")
	c.CheckEqual(a.Codec, acodec, "audio codec")
	c.Check(a.SampleRate == 44100 && a.Channels == 2, "audio %vHz %vch, want 44100Hz 2ch", a.SampleRate, a.Channels)
	c.CheckEqual(a.SequenceHeaders, 1, "audio sequence headers")
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	c.Check(r.AVSync <= 100*time.Millisecond, "A/V sync %v, want at most 100ms", r.AVSync)
}

// publishAll publishes a whole file at once, without real-time pacing, and
// keeps the stream published until the case ends. Segment groups (HLS, DASH,
// DVR) use it, because segments follow the media timestamps, not wall time.
func publishAll(c *check.C, url, file string) {
	c.Helper()
	f, err := os.Open(file)
	c.Must(err, "open the media")
	defer f.Close()
	r, err := media.NewFLVReader(f)
	c.Must(err, "read the media")

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	pub, err := rtmp.Dial(ctx, url, nil)
	c.Must(err, "dial the publisher")
	c.Cleanup(func() { pub.Close() })
	_, err = pub.Publish(ctx, rtmp.FMLE)
	c.Must(err, "publish")
	_, err = rtmp.PublishFLV(ctx, pub, r, nil)
	c.Must(err, "publish the file")
}

// segmentsUntil is when a player of a segment group has seen it play: both
// sequence headers and 15s of media, which takes two 10s segments.
var segmentsUntil = report.Until{VideoHeader: true, AudioHeader: true, Duration: 15 * time.Second}
