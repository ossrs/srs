// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"fmt"
	"net"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// rtmpSignaling adds the RTMP signaling cases to TestRtmp_Common: the
// handshakes, chunk sizes, connect, how SRS names a stream from the tcUrl and
// the stream name, the publish styles, and the status replies. SRS starts
// with env only, so it has one vhost, the default one; a vhost a client names
// is resolved to it, which is what the vhost cases check.
func rtmpSignaling(t *testing.T) {
	check.Case(t, "HandshakeSimple", check.Normal, func(c *check.C) {
		handshakeCase(c, rtmp.Simple)
	})

	check.Case(t, "HandshakeComplex", check.Normal, func(c *check.C) {
		handshakeCase(c, rtmp.Complex)
	})

	for _, size := range []int{128, 4096, 60000} {
		check.Case(t, fmt.Sprintf("ChunkSize%d", size), check.Normal, func(c *check.C) {
			srs := server.Common(c)
			url := rtmpURL(srs, "live/"+streamName(c))
			opts := &rtmp.Options{ChunkSize: size}
			startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, opts, rtmp.FMLE)
			_, r := playRTMPClient(c, url, opts)
			checkPlayed(c, r, "h264", "aac")
		})
	}

	// ConnectResult checks the connect reply: a status of NetConnection.Connect.Success
	// in the _result, after SRS set its chunk size, 60000 by default, so
	// replies larger than 128 bytes reach clients like OBS.
	check.Case(t, "ConnectResult", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		cl := dialRTMP(c, rtmpURL(srs, "live/"+streamName(c)), nil)
		want := rtmp.Status{Command: "_result", Level: "status", Code: "NetConnection.Connect.Success", Description: "Connection succeeded"}
		c.CheckEqual(cl.Connected, want, "connect result")
		c.CheckEqual(cl.ServerChunkSize(), 60000, "server chunk size")
	})

	// VhostFromTcUrl publishes with a tcUrl whose host names a vhost SRS
	// doesn't have; SRS puts the stream on the default vhost, where a player
	// that names no vhost finds it.
	check.Case(t, "VhostFromTcUrl", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		tcURL := "rtmp://example.com:" + port(srs) + "/live"
		opts := &rtmp.Options{TcURL: tcURL}
		startPublisherWith(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 0, opts, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		c.CheckEqual(h.Field("tcUrl"), tcURL, "tcUrl")
		c.CheckEqual(h.Field("app"), "live", "app")
		c.CheckEqual(h.Field("param"), "", "param")
		playChecked(c, rtmpURL(srs, "live/"+stream))
	})

	// VhostFromQuery names the vhost with ?vhost= on the stream name. The
	// vhost isn't configured, so the stream is on the default vhost, and the
	// query stays in the param.
	check.Case(t, "VhostFromQuery", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startPublisherWith(c, rtmpURL(srs, "live/"+stream+"?vhost=example.com"), media.Path(c, media.H264HighAAC), 0, nil, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		c.CheckEqual(h.Field("stream"), stream, "stream")
		c.CheckEqual(h.Field("param"), "?vhost=example.com", "param")
		playChecked(c, rtmpURL(srs, "live/"+stream))
	})

	// VhostDefaultQueryDropped names the default vhost with ?vhost=; when it
	// is the only parameter, SRS clears the param.
	check.Case(t, "VhostDefaultQueryDropped", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startPublisherWith(c, rtmpURL(srs, "live/"+stream+"?vhost=__defaultVhost__"), media.Path(c, media.H264HighAAC), 0, nil, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		c.CheckEqual(h.Field("param"), "", "param")
	})

	// ParamInStream puts params on the stream name: they are the param, not
	// part of the stream, so a player with other params plays the stream.
	check.Case(t, "ParamInStream", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startPublisherWith(c, rtmpURL(srs, "live/"+stream+"?token=abc&k=v"), media.Path(c, media.H264HighAAC), 0, nil, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("stream"), stream, "stream")
		c.CheckEqual(h.Field("param"), "?token=abc&k=v", "param")
		playChecked(c, rtmpURL(srs, "live/"+stream+"?token=other"))
	})

	// ParamInTcUrl puts the params on the tcUrl, after the app, as some
	// encoders do; SRS moves them after the stream name.
	check.Case(t, "ParamInTcUrl", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		tcURL := "rtmp://" + srs.Addr(server.RTMP) + "/live?token=abc"
		startPublisherWith(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 0, &rtmp.Options{TcURL: tcURL}, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		c.CheckEqual(h.Field("stream"), stream, "stream")
		c.CheckEqual(h.Field("param"), "?token=abc", "param")
		playChecked(c, rtmpURL(srs, "live/"+stream))
	})

	// AppFromTcUrl sends a connect whose app property differs from the
	// tcUrl's path: SRS takes the app from the tcUrl.
	check.Case(t, "AppFromTcUrl", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		startPublisherWith(c, rtmpURL(srs, "live/"+stream), media.Path(c, media.H264HighAAC), 0, &rtmp.Options{App: "other"}, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live", "app")
		playChecked(c, rtmpURL(srs, "live/"+stream))
	})

	// MultiLevelApp publishes to live/a/b/<stream>: every segment but the
	// last is the app.
	check.Case(t, "MultiLevelApp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		url := rtmpURL(srs, "live/a/b/"+stream)
		startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, nil, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live/a/b", "app")
		c.CheckEqual(h.Field("stream"), stream, "stream")
		c.CheckEqual(h.Field("stream_url"), "/live/a/b/"+stream, "stream_url")
		playChecked(c, url)
	})

	// Ipv6 publishes and plays over ::1. A host without IPv6, such as a
	// container started without it, can't run it.
	check.Case(t, "Ipv6", check.Normal, func(c *check.C) {
		if !server.HasIPv6() {
			c.Skip("the host has no IPv6 loopback")
		}
		srs := server.Common(c)
		stream := streamName(c)
		url := "rtmp://[::1]:" + port(srs) + "/live/" + stream
		startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, nil, rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("ip"), "::1", "client ip")
		c.CheckEqual(h.Field("tcUrl"), "rtmp://[::1]:"+port(srs)+"/live", "tcUrl")
		playChecked(c, url)
	})

	// PublishFmle publishes like OBS and ffmpeg: releaseStream, FCPublish,
	// createStream and publish. SRS answers publish with onFCPublish, then
	// onStatus, both NetStream.Publish.Start.
	check.Case(t, "PublishFmle", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		pub := startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, nil, rtmp.FMLE)
		c.CheckEqual(statusList(pub.Statuses()), []string{
			"onFCPublish NetStream.Publish.Start",
			"onStatus status NetStream.Publish.Start",
		}, "publish statuses")
		playChecked(c, url)
	})

	// PublishFlash publishes like a Flash NetStream: createStream and publish.
	// SRS answers with onStatus NetStream.Publish.Start only.
	check.Case(t, "PublishFlash", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		pub := startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, nil, rtmp.Flash)
		c.CheckEqual(statusList(pub.Statuses()), []string{"onStatus status NetStream.Publish.Start"}, "publish statuses")
		playChecked(c, url)
	})

	// PlayStatuses checks the replies to play: NetStream.Play.Reset, then
	// NetStream.Play.Start.
	check.Case(t, "PlayStatuses", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)
		player, _ := playRTMPClient(c, url, nil)
		c.CheckEqual(span(statusList(player.Statuses()), 0, 2), []string{
			"onStatus status NetStream.Play.Reset",
			"onStatus status NetStream.Play.Start",
		}, "play statuses")
	})

	// UnpublishFmle unpublishes with FCUnpublish: SRS answers onFCUnpublish
	// and onStatus NetStream.Unpublish.Success, and calls on_unpublish.
	check.Case(t, "UnpublishFmle", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := publishOnly(c, rtmpURL(srs, "live/"+stream), rtmp.FMLE)

		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Unpublish(ctx)
		c.Must(err, "unpublish")
		c.CheckEqual(span(statusList(pub.Statuses()), 2, -1), []string{
			"onFCUnpublish NetStream.Unpublish.Success",
			"onStatus status NetStream.Unpublish.Success",
		}, "unpublish statuses")
		waitHook(c, srs, "on_unpublish", stream)
	})

	// UnpublishFlash unpublishes with closeStream: SRS sends no status, and
	// calls on_unpublish.
	check.Case(t, "UnpublishFlash", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := publishOnly(c, rtmpURL(srs, "live/"+stream), rtmp.Flash)

		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		st, err := pub.Unpublish(ctx)
		c.Must(err, "unpublish")
		c.Check(st == nil, "unpublish status %+v, want none", st)
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckEqual(len(pub.Statuses()), 1, "statuses, only the publish start")
	})

	// ConnectWithoutTcUrl sends a connect whose tcUrl is null: SRS closes the
	// connection without a reply, and still serves the next client.
	check.Case(t, "ConnectWithoutTcUrl", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		cl, err := rtmp.Dial(ctx, rtmpURL(srs, "live/"+streamName(c)), &rtmp.Options{Connect: map[string]any{"tcUrl": nil}})
		if err == nil {
			cl.Close()
		}
		c.Check(err != nil, "connect without a tcUrl succeeded, want the connection closed")
		checkRTMPServes(c, srs)
	})

	// BadTcUrl sends a tcUrl that isn't a URL: SRS can't name the stream, so
	// it closes the connection at publish, without NetStream.Publish.Start,
	// and still serves the next client.
	check.Case(t, "BadTcUrl", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		cl := dialRTMP(c, rtmpURL(srs, "live/"+stream), &rtmp.Options{TcURL: "not a url"})
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := cl.Publish(ctx, rtmp.FMLE)
		c.Check(err != nil, "publish with a bad tcUrl succeeded, want the connection closed")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 0, "on_publish requests")
		checkRTMPServes(c, srs)
	})
}

// TestRtmp_ChunkSize4096 sets the RTMP chunk size to 4096 on each side: on
// SRS, a variant whose vhost sets chunk_size 4096, not the default 60000, and
// on the client, which tells SRS its size with Set Chunk Size, not the RTMP
// default 128. The test file's keyframes are larger than 4096 bytes, so each
// side splits them into chunks the other side joins.
func TestRtmp_ChunkSize4096(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.PlayConfig(server.Play{ChunkSize: 4096}))

	check.Case(t, "ConnectResult", check.Normal, func(c *check.C) {
		cl := dialRTMP(c, rtmpURL(srs, "live/"+streamName(c)), nil)
		c.CheckEqual(cl.ServerChunkSize(), 4096, "server chunk size")
	})

	// ServerSends: SRS sends in 4096-byte chunks, and the clients send in
	// 128-byte ones.
	check.Case(t, "ServerSends", check.Normal, func(c *check.C) {
		chunkSizeCase(c, srs, 128, 4096)
	})

	// ClientSends: the clients send in 4096-byte chunks to the Common
	// server, which sends in its default 60000-byte ones.
	check.Case(t, "ClientSends", check.Normal, func(c *check.C) {
		chunkSizeCase(c, server.Common(c), 4096, 60000)
	})

	// BothSend: SRS and the clients all send in 4096-byte chunks.
	check.Case(t, "BothSend", check.Normal, func(c *check.C) {
		chunkSizeCase(c, srs, 4096, 4096)
	})
}

// chunkSizeCase publishes and plays the H.264/AAC test file with clients
// that send in chunks of clientSize bytes, on a server that sends in chunks
// of serverSize bytes, and checks the played stream.
func chunkSizeCase(c *check.C, srs *server.Server, clientSize, serverSize int) {
	c.Helper()
	largest := 0
	for _, tag := range readTags(c, media.H264HighAAC, 0, time.Second) {
		largest = max(largest, len(tag.Data))
	}
	c.Check(largest > 4096, "largest message %d bytes, want more than 4096", largest)

	url := rtmpURL(srs, "live/"+streamName(c))
	opts := &rtmp.Options{ChunkSize: clientSize}
	startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, opts, rtmp.FMLE)
	player, r := playRTMPClient(c, url, opts)
	c.CheckEqual(player.ServerChunkSize(), serverSize, "server chunk size")
	checkPlayed(c, r, "h264", "aac")
}

// handshakeCase publishes and plays with one kind of handshake, which SRS
// answers in the same kind.
func handshakeCase(c *check.C, kind rtmp.Handshake) {
	srs := server.Common(c)
	url := rtmpURL(srs, "live/"+streamName(c))
	opts := &rtmp.Options{Handshake: kind}
	pub := startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, opts, rtmp.FMLE)
	c.CheckEqual(pub.Handshake.Kind(), kind, "publisher handshake")
	player, r := playRTMPClient(c, url, opts)
	c.CheckEqual(player.Handshake.Kind(), kind, "player handshake")
	checkPlayed(c, r, "h264", "aac")
}

// rtmpURL is the rtmp:// URL of a path on a server's RTMP listener.
func rtmpURL(srs *server.Server, path string) string {
	return "rtmp://" + srs.Addr(server.RTMP) + "/" + path
}

// port is a server's RTMP port.
func port(srs *server.Server) string {
	_, p, _ := net.SplitHostPort(srs.Addr(server.RTMP))
	return p
}

// dialRTMP connects a client, closed when the case ends.
func dialRTMP(c *check.C, url string, opts *rtmp.Options) *rtmp.Client {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	cl, err := rtmp.Dial(ctx, url, opts)
	c.Must(err, "dial")
	c.Cleanup(func() { cl.Close() })
	return cl
}

// publishOnly publishes a stream without media, until the case ends.
func publishOnly(c *check.C, url string, mode rtmp.PublishMode) *rtmp.Client {
	c.Helper()
	pub := dialRTMP(c, url, nil)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := pub.Publish(ctx, mode)
	c.Must(err, "publish")
	return pub
}

// playRTMPClient plays a stream until playedUntil, and returns the player,
// closed when the case ends, and its report.
func playRTMPClient(c *check.C, url string, opts *rtmp.Options) (*rtmp.Client, *report.Report) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	player, err := rtmp.Dial(ctx, url, opts)
	c.Must(err, "dial the player")
	c.Cleanup(func() { player.Close() })
	_, err = player.Play(ctx)
	c.Must(err, "play")
	player.SetDeadline(time.Now().Add(2 * time.Second))
	r, err := flv.Analyze(player, playedUntil)
	c.Must(err, "play the stream")
	return player, r
}

// playChecked plays a stream of the H.264/AAC test file and checks it.
func playChecked(c *check.C, url string) {
	c.Helper()
	_, r := playRTMPClient(c, url, nil)
	checkPlayed(c, r, "h264", "aac")
}

// waitHook waits for a hook of a stream on the server's mock, a second
// scaled by the limit scale.
func waitHook(c *check.C, srs *server.Server, action, stream string) mock.Request {
	c.Helper()
	c.Must(checkHooks(srs), "hooks")
	ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(time.Second))
	defer cancel()
	h, err := srs.Hooks.Wait(ctx, mock.Match{Action: action, Stream: stream})
	c.Must(err, action)
	return h
}

// statusList is each status as "<command> [<level>] <code>".
func statusList(sts []rtmp.Status) []string {
	var list []string
	for _, st := range sts {
		s := st.Command
		if st.Level != "" {
			s += " " + st.Level
		}
		list = append(list, s+" "+st.Code)
	}
	return list
}

// checkRTMPServes checks the server still answers a new RTMP connect.
func checkRTMPServes(c *check.C, srs *server.Server) {
	c.Helper()
	cl := dialRTMP(c, rtmpURL(srs, "live/"+streamName(c)), nil)
	c.CheckEqual(cl.Connected.Code, "NetConnection.Connect.Success", "a new connect after the bad one")
}

// span is l[i:j] clamped to l, with j -1 for the end, so a short list fails a
// check rather than panicking.
func span(l []string, i, j int) []string {
	if j < 0 || j > len(l) {
		j = len(l)
	}
	if i > j {
		i = j
	}
	return l[i:j]
}
