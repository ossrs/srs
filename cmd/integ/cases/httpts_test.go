// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/ts"
)

// TestHttpTs_TsMount publishes over RTMP and plays HTTP-TS, on a variant that
// mounts every stream as .ts: a vhost has one HTTP remux mount, and Common's
// is FLV.
func TestHttpTs_TsMount(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.RemuxConfig("ts"))

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlayHTTPTS(c, srs, media.H264HighAAC, "h264", "aac")
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlayHTTPTS(c, srs, media.HEVCAAC, "hevc", "aac")
	})
}

// publishPlayHTTPTS publishes a file over RTMP and plays it back over
// HTTP-TS, joining a stream that has already sent 200ms, like the HTTP-FLV
// cases. TS has no metadata, and carries the sequence headers in band, which
// the report counts once while they don't change.
func publishPlayHTTPTS(c *check.C, srs *server.Server, file, vcodec, acodec string) {
	stream := "live/" + streamName(c)
	startPublisher(c, "rtmp://"+srs.Addr(server.RTMP)+"/"+stream, media.Path(c, file), 200*time.Millisecond)

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	u := report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}
	r, err := ts.PlayHTTP(ctx, nil, "http://"+srs.Addr(server.HTTPServer)+"/"+stream+".ts", u)
	c.Must(err, "play")
	checkPlayed(c, r, vcodec, acodec)
}
