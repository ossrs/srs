// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/server"
)

// TestHls_Common publishes over RTMP without real-time pacing, and plays the
// HLS of the Common server, with its TS segments.
func TestHls_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlayHLS(c, media.H264HighAAC, "h264", "aac")
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlayHLS(c, media.HEVCAAC, "hevc", "aac")
	})
}

// publishPlayHLS publishes a whole file at once and plays 15s of it from the
// HLS of the Common server.
func publishPlayHLS(c *check.C, file, vcodec, acodec string) {
	srs := server.Common(c)
	stream := "live/" + streamName(c)
	publishAll(c, srs, stream, media.Path(c, file))

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	r, err := hls.PlayHTTP(ctx, httpClient, "http://"+srs.Addr(server.HTTPServer)+"/"+stream+".m3u8", segmentsUntil)
	c.Must(err, "play")
	checkPlayed(c, r, vcodec, acodec)
	c.Check(r.Video.Keyframes >= 15, "%v keyframes, want a 1s GOP over 15s", r.Video.Keyframes)
}
