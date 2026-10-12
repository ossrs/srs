// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/dash"
	"srs-integ/internal/media"
	"srs-integ/internal/server"
)

// TestDash_Common publishes over RTMP without real-time pacing, and plays the
// DASH of the Common server: its MPD, and the init and m4s segments of the
// video and audio representations.
func TestDash_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "live/" + streamName(c)
		publishAll(c, "rtmp://"+srs.Addr(server.RTMP)+"/"+stream, media.Path(c, media.H264HighAAC))

		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		r, err := dash.PlayHTTP(ctx, nil, "http://"+srs.Addr(server.HTTPServer)+"/"+stream+".mpd", segmentsUntil)
		c.Must(err, "play")
		checkPlayed(c, r, "h264", "aac")
		c.Check(r.Video.Keyframes >= 15, "%v keyframes, want a 1s GOP over 15s", r.Video.Keyframes)
	})
}
