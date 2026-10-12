// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"crypto/tls"
	"fmt"
	"net"
	"slices"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// TestRtmps_Common publishes and plays RTMP over TLS on the Common server's
// RTMPS listener, which uses the suite's test certificate: the same protocol
// checklist as RTMP where TLS changes something, and RTMPS and RTMP clients
// on one stream.
func TestRtmps_Common(t *testing.T) {
	t.Parallel()

	// PublishPlay publishes and plays H.264/AAC over RTMPS, and checks the
	// player verified the test certificate.
	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpsURL(srs, "live/"+streamName(c))
		startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, tlsOpts(c), rtmp.FMLE)
		player, r := playRTMPClient(c, url, tlsOpts(c))
		checkPlayed(c, r, "h264", "aac")
		if st := player.TLS(); c.Check(st != nil, "the player has no TLS state") {
			c.Check(len(st.VerifiedChains) > 0, "the player didn't verify the server's certificate")
		}
	})

	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpsURL(srs, "live/"+streamName(c))
		startPublisherWith(c, url, media.Path(c, media.HEVCAAC), 0, tlsOpts(c), rtmp.FMLE)
		_, r := playRTMPClient(c, url, tlsOpts(c))
		checkPlayed(c, r, "hevc", "aac")
	})

	// ServesTestCertificate checks the listener presents the configured
	// certificate, not one of its own: the client trusts only the
	// self-signed test certificate, so a verified chain is that certificate.
	check.Case(t, "ServesTestCertificate", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		cl := dialRTMP(c, rtmpsURL(srs, "live/"+streamName(c)), tlsOpts(c))
		st := cl.TLS()
		if !c.Check(st != nil && len(st.VerifiedChains) == 1 && len(st.VerifiedChains[0]) == 1, "TLS state %+v, want one chain of the test certificate", st) {
			return
		}
		leaf := st.VerifiedChains[0][0]
		c.CheckEqual(leaf.Subject.CommonName, "localhost", "certificate subject")
		c.Check(st.Version >= tls.VersionTLS12, "TLS version %x, want 1.2 or later", st.Version)
		c.CheckEqual(cl.Connected.Code, "NetConnection.Connect.Success", "connect over TLS")
	})

	check.Case(t, "HandshakeSimple", check.Normal, func(c *check.C) {
		rtmpsHandshakeCase(c, rtmp.Simple)
	})

	check.Case(t, "HandshakeComplex", check.Normal, func(c *check.C) {
		rtmpsHandshakeCase(c, rtmp.Complex)
	})

	// ChunkSize128 sends and plays in 128-byte chunks, many small TLS
	// records.
	check.Case(t, "ChunkSize128", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpsURL(srs, "live/"+streamName(c))
		opts := tlsOpts(c)
		opts.ChunkSize = 128
		startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, opts, rtmp.FMLE)
		_, r := playRTMPClient(c, url, opts)
		checkPlayed(c, r, "h264", "aac")
	})

	// StreamNaming publishes to a multi-level app with a param: the hook
	// gets the rtmps:// tcUrl, the app, the stream and the param, and a
	// plain RTMP player finds the stream by the same path.
	check.Case(t, "StreamNaming", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		url := rtmpsURL(srs, "live/a/b/"+stream+"?token=abc")
		startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, tlsOpts(c), rtmp.FMLE)

		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("tcUrl"), "rtmps://"+srs.Addr(server.RTMPS)+"/live/a/b", "tcUrl")
		c.CheckEqual(h.Field("app"), "live/a/b", "app")
		c.CheckEqual(h.Field("stream"), stream, "stream")
		c.CheckEqual(h.Field("param"), "?token=abc", "param")
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		playChecked(c, rtmpURL(srs, "live/a/b/"+stream))
	})

	// PublishStatuses checks the FMLE publish and play status replies over
	// TLS.
	check.Case(t, "PublishStatuses", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpsURL(srs, "live/"+streamName(c))
		pub := dialRTMP(c, url, tlsOpts(c))
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish")
		c.CheckEqual(statusList(pub.Statuses()), []string{
			"onFCPublish NetStream.Publish.Start",
			"onStatus status NetStream.Publish.Start",
		}, "publish statuses")

		player := dialRTMP(c, url, tlsOpts(c))
		_, err = player.Play(ctx)
		c.Must(err, "play")
		c.CheckEqual(statusList(player.Statuses()), []string{
			"onStatus status NetStream.Play.Reset",
			"onStatus status NetStream.Play.Start",
		}, "play statuses")
	})

	// PublishRtmpsPlayRtmp and PublishRtmpPlayRtmps put both listeners on
	// one stream.
	check.Case(t, "PublishRtmpsPlayRtmp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		path := "live/" + streamName(c)
		startPublisherWith(c, rtmpsURL(srs, path), media.Path(c, media.H264HighAAC), 0, tlsOpts(c), rtmp.FMLE)
		_, r := playRTMPClient(c, rtmpURL(srs, path), nil)
		checkPlayed(c, r, "h264", "aac")
	})

	check.Case(t, "PublishRtmpPlayRtmps", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		path := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, path), media.Path(c, media.H264HighAAC), 0)
		_, r := playRTMPClient(c, rtmpsURL(srs, path), tlsOpts(c))
		checkPlayed(c, r, "h264", "aac")
	})

	// PlayersOfBoth plays one RTMPS stream with 3 RTMP and 3 RTMPS players at
	// once.
	check.Case(t, "PlayersOfBoth", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		path := "live/" + streamName(c)
		startPublisherWith(c, rtmpsURL(srs, path), media.Path(c, media.H264HighAAC), 0, tlsOpts(c), rtmp.FMLE)

		const n = 6
		reports, errs := make([]*report.Report, n), make([]error, n)
		var wg sync.WaitGroup
		for i := range n {
			url, opts := rtmpURL(srs, path), (*rtmp.Options)(nil)
			if i%2 == 1 {
				url, opts = rtmpsURL(srs, path), tlsOpts(c)
			}
			wg.Add(1)
			go func() {
				defer wg.Done()
				ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
				defer cancel()
				reports[i], errs[i] = flv.PlayRTMP(ctx, url, opts, playedUntil)
			}()
		}
		wg.Wait()
		for i := range n {
			if c.CheckNoError(errs[i], fmt.Sprintf("player %d", i)) {
				checkPlayed(c, reports[i], "h264", "aac")
			}
		}
	})

	// PlayBeforePublish plays over RTMPS before the stream is published,
	// then gets it from its start.
	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := rtmpsURL(srs, "live/"+streamName(c))
		player := dialRTMP(c, url, tlsOpts(c))
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Play(ctx)
		c.Must(err, "play")
		startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, tlsOpts(c), rtmp.FMLE)

		var a flv.Analyzer
		c.Must(readUntil(player, &a, playedUntil, 2*time.Second), "play")
		checkPlayed(c, &a.Report, "h264", "aac")
		c.CheckEqual(a.Report.Metadata, 1, "onMetaData messages")
	})

	// SecondPublisherRejected publishes over RTMPS a stream an RTMP client
	// already publishes, and the reverse: SRS refuses the second without
	// NetStream.Publish.Start, and the first still plays.
	check.Case(t, "SecondPublisherRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		path := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, path), media.Path(c, media.H264HighAAC), 0)
		checkSecondRejected(c, rtmpsURL(srs, path), tlsOpts(c))
		playChecked(c, rtmpURL(srs, path))
	})

	check.Case(t, "SecondPublisherRtmpRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		path := "live/" + streamName(c)
		startPublisherWith(c, rtmpsURL(srs, path), media.Path(c, media.H264HighAAC), 0, tlsOpts(c), rtmp.FMLE)
		checkSecondRejected(c, rtmpURL(srs, path), nil)
		_, r := playRTMPClient(c, rtmpsURL(srs, path), tlsOpts(c))
		checkPlayed(c, r, "h264", "aac")
	})

	// PublisherCloseCleanup closes an RTMPS publisher: its client and stream
	// are gone from the API.
	check.Case(t, "PublisherCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := dialRTMP(c, rtmpsURL(srs, "live/"+stream), tlsOpts(c))
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := pub.Publish(ctx, rtmp.FMLE)
		c.Must(err, "publish")
		h := waitHook(c, srs, "on_publish", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")
		c.CheckEqual(apiCode(c, srs, streamPath), 0, "the stream in the API")

		pub.Close()
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after it closed")
		c.CheckNoError(untilGone(c, srs, streamPath), "the stream after its publisher closed")
	})

	// PlainRtmpOnRtmpsPort sends an RTMP C0 and C1 to the RTMPS port: SRS
	// closes the connection, and still serves RTMPS.
	check.Case(t, "PlainRtmpOnRtmpsPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.RTMPS))
		_, err := conn.Write(c0c1(3))
		c.Must(err, "write C0 and C1")
		checkTLSRefused(c, conn, "after a plain C0 and C1")
		checkRTMPSServes(c, srs)
	})

	// RtmpsOnRtmpPort dials the plain RTMP port with TLS: the dial fails, and
	// SRS still serves RTMP. The ClientHello is shorter than the 1537 bytes
	// of C0 and C1 that SRS reads before it checks them, so SRS waits for
	// more rather than closing; the client gives up after 200ms.
	check.Case(t, "RtmpsOnRtmpPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		url := "rtmps://" + srs.Addr(server.RTMP) + "/live/" + streamName(c)
		opts := tlsOpts(c)
		opts.Timeout = 200 * time.Millisecond
		cl, err := rtmp.Dial(context.Background(), url, opts)
		if err == nil {
			cl.Close()
		}
		c.Check(err != nil, "a TLS dial to the RTMP port succeeded")
		checkRTMPServes(c, srs)
	})

	// CloseInTlsHandshake sends part of a TLS ClientHello and closes: SRS
	// still serves RTMPS.
	check.Case(t, "CloseInTlsHandshake", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.RTMPS))
		// A handshake record header that promises 512 bytes, and 5 of them.
		_, err := conn.Write([]byte{0x16, 0x03, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0xfc, 0x03})
		c.Must(err, "write part of a ClientHello")
		conn.Close()
		checkRTMPSServes(c, srs)
	})

	// BadTlsRecord sends a TLS record of an unknown type: SRS closes the
	// connection, and still serves RTMPS.
	check.Case(t, "BadTlsRecord", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.RTMPS))
		_, err := conn.Write(append([]byte{0x63, 0x03, 0x03, 0x00, 0x10}, make([]byte, 16)...))
		c.Must(err, "write a bad record")
		checkTLSRefused(c, conn, "after a record of type 0x63")
		checkRTMPSServes(c, srs)
	})
}

// rtmpsURL is the rtmps:// URL of a path on a server's RTMPS listener.
func rtmpsURL(srs *server.Server, path string) string {
	return "rtmps://" + srs.Addr(server.RTMPS) + "/" + path
}

// tlsOpts is a new client options that trusts the test certificate.
func tlsOpts(c *check.C) *rtmp.Options {
	c.Helper()
	cfg, err := server.ClientTLS()
	c.Must(err, "the client TLS config")
	return &rtmp.Options{TLS: cfg}
}

func rtmpsHandshakeCase(c *check.C, kind rtmp.Handshake) {
	srs := server.Common(c)
	url := rtmpsURL(srs, "live/"+streamName(c))
	opts := tlsOpts(c)
	opts.Handshake = kind
	pub := startPublisherWith(c, url, media.Path(c, media.H264HighAAC), 0, opts, rtmp.FMLE)
	c.CheckEqual(pub.Handshake.Kind(), kind, "publisher handshake")
	player, r := playRTMPClient(c, url, opts)
	c.CheckEqual(player.Handshake.Kind(), kind, "player handshake")
	checkPlayed(c, r, "h264", "aac")
}

// checkSecondRejected publishes a stream that is already published, and
// checks SRS refuses it without NetStream.Publish.Start.
func checkSecondRejected(c *check.C, url string, opts *rtmp.Options) {
	c.Helper()
	second := dialRTMP(c, url, opts)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := second.Publish(ctx, rtmp.FMLE)
	c.Check(err != nil, "the second publisher started, want it refused")
	sts := statusList(second.Statuses())
	c.Check(!slices.Contains(sts, "onStatus status NetStream.Publish.Start"), "second publisher statuses %v, want no NetStream.Publish.Start", sts)
}

// rawConnTo is a TCP connection to an address, closed when the case ends.
func rawConnTo(c *check.C, addr string) net.Conn {
	c.Helper()
	conn, err := net.DialTimeout("tcp", addr, time.Second)
	c.Must(err, "dial")
	c.Cleanup(func() { conn.Close() })
	return conn
}

// checkTLSRefused checks the server ends a connection within 1s: it closes
// it, perhaps after a TLS alert record, and sends nothing else.
func checkTLSRefused(c *check.C, conn net.Conn, what string) {
	c.Helper()
	conn.SetDeadline(time.Now().Add(time.Second))
	var got []byte
	buf := make([]byte, 256)
	for {
		n, err := conn.Read(buf)
		got = append(got, buf[:n]...)
		if err != nil {
			c.Check(!isTimeout(err), "%s: %v after %d bytes, want the connection closed", what, err, len(got))
			break
		}
	}
	// Anything sent must be TLS alert records (type 21).
	for b := got; len(b) > 0; {
		if len(b) < 5 || b[0] != 21 {
			c.Errorf("%s: the server sent % x, want nothing or a TLS alert", what, got)
			return
		}
		b = b[min(len(b), 5+int(b[3])<<8|int(b[4])):]
	}
}

// checkRTMPSServes checks the server still answers a new RTMPS connect.
func checkRTMPSServes(c *check.C, srs *server.Server) {
	c.Helper()
	cl := dialRTMP(c, rtmpsURL(srs, "live/"+streamName(c)), tlsOpts(c))
	c.CheckEqual(cl.Connected.Code, "NetConnection.Connect.Success", "a new RTMPS connect after the bad one")
}
