// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"os"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// TestRtmp_Common publishes and plays RTMP on the Common server.
func TestRtmp_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlay(c, media.H264HighAAC, "h264", "aac")
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlay(c, media.HEVCAAC, "hevc", "aac")
	})
}

// publishPlay publishes a file in real time and plays it back: the player
// gets the metadata, both sequence headers, a keyframe and frames, with the
// codecs of the file.
func publishPlay(c *check.C, file, vcodec, acodec string) {
	srs := server.Common(c)
	url := "rtmp://" + srs.Addr(server.RTMP) + "/live/" + streamName(c)

	pub := startPublisher(c, url, media.Path(c, file))
	c.Check(pub.Handshake.Complex, "publisher handshake is simple, want complex")

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	player, err := rtmp.Dial(ctx, url, nil)
	c.Must(err, "dial the player")
	defer player.Close()
	_, err = player.Play(ctx)
	c.Must(err, "play")
	player.SetDeadline(time.Now().Add(2 * time.Second))

	var meta, vsh, ash, key bool
	var frames int
	for !(meta && vsh && ash && key && frames >= 5) {
		tag, err := player.ReadTag()
		c.Must(err, "read")
		switch tag.Type {
		case media.TagScript:
			meta = true
		case media.TagVideo:
			v, err := media.ParseVideo(tag.Data)
			c.Must(err, "video tag")
			c.CheckEqual(v.Codec, vcodec, "video codec")
			switch {
			case v.SequenceHeader:
				vsh = true
			case v.EndOfSequence || v.Metadata:
			default:
				c.Check(vsh, "a video frame before the sequence header")
				key = key || v.Key
				frames++
			}
		case media.TagAudio:
			a, err := media.ParseAudio(tag.Data)
			c.Must(err, "audio tag")
			c.CheckEqual(a.Codec, acodec, "audio codec")
			ash = ash || a.SequenceHeader
		}
	}
	c.Check(player.Handshake.Complex, "player handshake is simple, want complex")
}

// startPublisher publishes a file in real time until the case ends.
func startPublisher(c *check.C, url, file string) *rtmp.Client {
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

	done := make(chan error, 1)
	go func() {
		_, err := rtmp.PublishFLV(ctx, pub, r, &rtmp.Pacer{})
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
