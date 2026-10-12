// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// TestHooks_Common checks the hooks of the Common server, which all call one
// mock that accepts by default; a case scripts a reply only for its own stream.
// Each case is a whole RTMP publish through the shared server, so it is Normal:
// alone it takes a few ms, but next to the segment groups, which publish whole
// files at once, Common's one core makes it take up to about 130ms.
func TestHooks_Common(t *testing.T) {
	t.Parallel()
	hooksRtmp(t)
	hooksHTTP(t)
	hooksProtocols(t)
	hooksReject(t)

	check.Case(t, "OnPublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		c.Must(checkHooks(srs), "hooks")
		stream := streamName(c)

		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		pub, err := rtmp.Dial(ctx, "rtmp://"+srs.Addr(server.RTMP)+"/live/"+stream+"?token=abc", nil)
		c.Must(err, "dial")
		_, err = pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish")

		r, err := srs.Hooks.Wait(ctx, mock.Match{Action: "on_publish", Stream: stream})
		c.Must(err, "on_publish")
		c.CheckEqual(r.Method, "POST", "method")
		c.CheckEqual(r.Path, "/hooks/on_publish", "path")
		c.CheckEqual(r.Field("app"), "live", "app")
		c.CheckEqual(r.Field("vhost"), "__defaultVhost__", "vhost")
		c.CheckEqual(r.Field("param"), "?token=abc", "param")
		c.CheckEqual(r.Field("ip"), "127.0.0.1", "ip")
		c.Check(r.Field("client_id") != "", "no client_id in %s", r.Body)

		pub.Close()
		_, err = srs.Hooks.Wait(ctx, mock.Match{Action: "on_unpublish", Stream: stream})
		c.CheckNoError(err, "on_unpublish after the publisher closes")
	})

	check.Case(t, "OnPublishRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		c.Must(checkHooks(srs), "hooks")
		stream := streamName(c)
		srs.Hooks.Script(mock.Match{Action: "on_publish", Stream: stream}, mock.Reject(403))

		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		pub, err := rtmp.Dial(ctx, "rtmp://"+srs.Addr(server.RTMP)+"/live/"+stream, nil)
		c.Must(err, "dial")
		defer pub.Close()
		_, err = pub.Publish(ctx, rtmp.FMLE)
		c.Check(err != nil, "publish succeeded, want it rejected by on_publish")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 1, "on_publish requests")
	})
}

// checkHooks fails when a server has no hook mock.
func checkHooks(srs *server.Server) error {
	if srs.Hooks == nil {
		return errNoHooks
	}
	return nil
}

var errNoHooks = errors.New("the server has no hook mock")
