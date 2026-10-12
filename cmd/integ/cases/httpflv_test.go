// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/server"
)

// TestHttpFlv_Common publishes over RTMP and plays HTTP-FLV on the Common
// server.
func TestHttpFlv_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlayHTTPFLV(c, media.H264HighAAC, "h264", "aac")
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlayHTTPFLV(c, media.HEVCAAC, "hevc", "aac")
	})
}

// publishPlayHTTPFLV publishes a file over RTMP and plays it back over
// HTTP-FLV. The player joins a stream that has already sent 200ms, so its
// first frames come from the GOP cache with audio and video: SRS guesses the
// FLV header from them, and a player that joins before the first audio frame
// gets a video-only stream, which the HTTP-FLV options cases cover.
func publishPlayHTTPFLV(c *check.C, file, vcodec, acodec string) {
	srs := server.Common(c)
	stream := "live/" + streamName(c)
	startPublisher(c, "rtmp://"+srs.Addr(server.RTMP)+"/"+stream, media.Path(c, file), 200*time.Millisecond)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	r, err := flv.PlayHTTP(ctx, httpClient, "http://"+srs.Addr(server.HTTPServer)+"/"+stream+".flv", playedUntil)
	c.Must(err, "play")
	checkPlayed(c, r, vcodec, acodec)
}
