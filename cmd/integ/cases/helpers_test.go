// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"net/http"
	"os"
	"path"
	"regexp"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// httpClient is the client of every HTTP player and API call. It keeps an idle
// connection for each parallel case, where the default keeps 2 per host and
// closes the rest, which leaves a socket in TIME_WAIT on an ephemeral port for
// each request a busy run makes beyond them.
var httpClient = func() *http.Client {
	tr := http.DefaultTransport.(*http.Transport).Clone()
	tr.MaxIdleConns = 1024
	tr.MaxIdleConnsPerHost = 256
	return &http.Client{Transport: tr}
}()

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
	return startPublisherWith(c, url, file, lead, nil, rtmp.FMLE)
}

// startPublisherWith is startPublisher with the client's options and publish
// style.
func startPublisherWith(c *check.C, url, file string, lead time.Duration, opts *rtmp.Options, mode rtmp.PublishMode) *rtmp.Client {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	pub, err := rtmp.Dial(ctx, url, opts)
	c.Must(err, "dial the publisher")
	c.Cleanup(func() { pub.Close() })
	_, err = pub.Publish(ctx, mode)
	c.Must(err, "publish")
	streamFile(c, pub, file, lead)
	return pub
}

// streamFile publishes a file on a client that publishes, until the case
// ends, like startPublisher.
func streamFile(c *check.C, pub *rtmp.Client, file string, lead time.Duration) {
	c.Helper()
	f, err := os.Open(file)
	c.Must(err, "open the media")
	r, err := media.NewFLVReader(f)
	c.Must(err, "read the media")
	ctx, cancel := context.WithCancel(context.Background())

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
	checkVideo(c, r, vcodec)
	checkAudio(c, r, acodec, 1)
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	c.Check(r.AVSync <= 100*time.Millisecond, "A/V sync %v, want at most 100ms", r.AVSync)
}

// checkVideo checks the video of a test file: its codec, the source's 768x320
// and one sequence header.
func checkVideo(c *check.C, r *report.Report, codec string) {
	c.Helper()
	if !c.Check(r.Video != nil, "no video: %v", r) {
		return
	}
	v := r.Video
	c.CheckEqual(v.Codec, codec, "video codec")
	c.Check(v.Width == 768 && v.Height == 320, "size %vx%v, want 768x320", v.Width, v.Height)
	c.CheckEqual(v.SequenceHeaders, 1, "video sequence headers")
}

// checkAudio checks the audio of a test file: its codec, the source's 44.1 kHz
// stereo, and the number of sequence headers, 1 for AAC and 0 for MP3.
func checkAudio(c *check.C, r *report.Report, codec string, headers int) {
	c.Helper()
	if !c.Check(r.Audio != nil, "no audio: %v", r) {
		return
	}
	a := r.Audio
	c.CheckEqual(a.Codec, codec, "audio codec")
	c.Check(a.SampleRate == 44100 && a.Channels == 2, "audio %vHz %vch, want 44100Hz 2ch", a.SampleRate, a.Channels)
	c.CheckEqual(a.SequenceHeaders, headers, "audio sequence headers")
}

// publishAll publishes a whole file at once, without real-time pacing, and
// keeps the stream published until the case ends. Segment groups (HLS, DASH,
// DVR) use it, because segments follow the media timestamps, not wall time.
// It holds one of the server's burst slots while it publishes, so a run of
// many segment cases doesn't make every case on the server slow at once.
func publishAll(c *check.C, srs *server.Server, stream, file string) {
	c.Helper()
	url := "rtmp://" + srs.Addr(server.RTMP) + "/" + stream
	f, err := os.Open(file)
	c.Must(err, "open the media")
	defer f.Close()
	r, err := media.NewFLVReader(f)
	c.Must(err, "read the media")

	free := srs.Burst(c)
	defer free()
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
