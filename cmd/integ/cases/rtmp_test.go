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

// TestRtmp_Common publishes and plays RTMP on the Common server.
func TestRtmp_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		publishPlayRTMP(c, media.H264HighAAC, "h264", "aac")
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		publishPlayRTMP(c, media.HEVCAAC, "hevc", "aac")
	})
}

// publishPlayRTMP publishes a file in real time and plays it back over RTMP.
func publishPlayRTMP(c *check.C, file, vcodec, acodec string) {
	srs := server.Common(c)
	url := "rtmp://" + srs.Addr(server.RTMP) + "/live/" + streamName(c)

	pub := startPublisher(c, url, media.Path(c, file), 0)
	c.Check(pub.Handshake.Complex, "publisher handshake is simple, want complex")

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	r, err := flv.PlayRTMP(ctx, url, nil, playedUntil)
	c.Must(err, "play")
	checkPlayed(c, r, vcodec, acodec)
}
