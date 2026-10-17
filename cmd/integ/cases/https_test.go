// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"context"
	"crypto/tls"
	"fmt"
	"net"
	"net/http"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/httpplay"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
)

// TestHttps_Common checks the Common server's HTTPS server, which uses the
// suite's test certificate, at the TLS level: the certificate it serves, and
// bad input on its port and TLS on the plain HTTP port. The streams it
// serves are cases of their remux: TestHttpFlv_Common and
// TestHttpTs_TsMount, Https*.
func TestHttps_Common(t *testing.T) {
	t.Parallel()

	// ServesTestCertificate checks the listener presents the configured
	// certificate: the client trusts only the self-signed test certificate,
	// so a verified chain is that certificate. A request over it gets an
	// HTTP answer.
	check.Case(t, "ServesTestCertificate", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := dialHTTPS(c, srs)
		st := conn.ConnectionState()
		if c.Check(len(st.VerifiedChains) == 1 && len(st.VerifiedChains[0]) == 1, "verified chains %v, want one chain of the test certificate", st.VerifiedChains) {
			c.CheckEqual(st.VerifiedChains[0][0].Subject.CommonName, "localhost", "certificate subject")
		}
		c.Check(st.Version >= tls.VersionTLS12, "TLS version %x, want 1.2 or later", st.Version)

		_, err := fmt.Fprintf(conn, "GET /live/%s.mp4 HTTP/1.1\r\nHost: localhost\r\n\r\n", streamName(c))
		c.Must(err, "write the request")
		conn.SetDeadline(time.Now().Add(time.Second))
		res, err := http.ReadResponse(bufio.NewReader(conn), nil)
		c.Must(err, "the answer over TLS")
		res.Body.Close()
		c.CheckEqual(res.StatusCode, 404, "status of a missing file")
	})

	// PlainHttpOnHttpsPort sends a plain HTTP request to the HTTPS port: SRS
	// closes the connection, perhaps after a TLS alert, and still serves
	// HTTPS.
	check.Case(t, "PlainHttpOnHttpsPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.HTTPSServer))
		_, err := fmt.Fprintf(conn, "GET /live/%s.flv HTTP/1.1\r\nHost: localhost\r\n\r\n", streamName(c))
		c.Must(err, "write a plain request")
		checkTLSRefused(c, conn, "after a plain HTTP request")
		checkHTTPSServes(c, srs)
	})

	// HttpsOnHttpPort dials the plain HTTP port with TLS: the handshake
	// fails, and SRS still serves HTTP.
	check.Case(t, "HttpsOnHttpPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		cfg := httpsOpts(c).TLS
		cfg.ServerName = "localhost"
		conn := rawConnTo(c, srs.Addr(server.HTTPServer))
		ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
		defer cancel()
		err := tls.Client(conn, cfg).HandshakeContext(ctx)
		c.Check(err != nil, "a TLS handshake with the HTTP port succeeded")
		c.Check(!isTimeout(err) && ctx.Err() == nil, "the HTTP port didn't end the TLS handshake within 300ms: %v", err)
		checkHTTPStatus(c, srs, "live/"+streamName(c)+".mp4", nil, 404)
	})

	// CloseInTlsHandshake sends part of a TLS ClientHello and closes: SRS
	// still serves HTTPS.
	check.Case(t, "CloseInTlsHandshake", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.HTTPSServer))
		// A handshake record header that promises 512 bytes, and 5 of them.
		_, err := conn.Write([]byte{0x16, 0x03, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0xfc, 0x03})
		c.Must(err, "write part of a ClientHello")
		conn.Close()
		checkHTTPSServes(c, srs)
	})

	// BadTlsRecord sends a TLS record of an unknown type: SRS closes the
	// connection, and still serves HTTPS.
	check.Case(t, "BadTlsRecord", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.HTTPSServer))
		_, err := conn.Write(append([]byte{0x63, 0x03, 0x03, 0x00, 0x10}, make([]byte, 16)...))
		c.Must(err, "write a bad record")
		checkTLSRefused(c, conn, "after a record of type 0x63")
		checkHTTPSServes(c, srs)
	})
}

// httpsFlvCases adds the HTTP-FLV cases over the HTTPS server to
// TestHttpFlv_Common.
func httpsFlvCases(t *testing.T) {
	httpsCases(t, server.Common, remuxMount{"flv", "video/x-flv", media.H264HighAAC, playedUntil,
		func(c *check.C, r *report.Report) { checkPlayed(c, r, "h264", "aac") }})
}

// httpsTsCases adds the HTTP-TS cases over the HTTPS server to
// TestHttpTs_TsMount, on its server srs, which has hooks of its own, so a
// case may unpublish while a player is on.
func httpsTsCases(t *testing.T, srs *server.Server) {
	httpsCases(t, func(testing.TB) *server.Server { return srs }, tsMount)

	// HttpsUnpublishEndsStream closes the publisher: the stream of an HTTPS
	// player ends, and on_stop comes.
	check.Case(t, "HttpsUnpublishEndsStream", check.Normal, func(c *check.C) {
		name := streamName(c)
		stream := "live/" + name
		pub := startPublisher(c, rtmpURL(srs, stream), media.Path(c, tsMount.file), 200*time.Millisecond)
		p := remuxPlay(c, srs, stream+".ts", httpsOpts(c), tsMount)
		pub.Close()
		c.CheckNoError(p.untilEnds(time.Second), "the player after the unpublish")
		waitHook(c, srs, "on_stop", name)
	})
}

// httpsCases adds the cases of a remux mount over the HTTPS server, with
// players that trust the test certificate, on the server srsOf gives: the
// media, the request and the mount, and the lifecycle of the protocol
// checklist where TLS changes the transport. Each publishes over RTMP in
// real time, and the player joins 200ms in.
func httpsCases(t *testing.T, srsOf func(testing.TB) *server.Server, m remuxMount) {
	ext := "." + m.ext
	publish := func(c *check.C, srs *server.Server, app, file string) (name, stream string) {
		name = streamName(c)
		stream = app + "/" + name
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, file), 200*time.Millisecond)
		return name, stream
	}

	// HttpsPublishPlay plays H.264/AAC over HTTPS, and checks the player
	// verified the test certificate.
	check.Case(t, "HttpsPublishPlay", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		_, stream := publish(c, srs, "live", m.file)
		p := remuxPlay(c, srs, stream+ext, httpsOpts(c), m)
		m.check(c, p.Report)
		if st := p.tls(); c.Check(st != nil, "the player has no TLS state") {
			c.Check(len(st.VerifiedChains) > 0, "the player didn't verify the server's certificate")
		}
	})

	check.Case(t, "HttpsPublishPlayHevc", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		_, stream := publish(c, srs, "live", media.HEVCAAC)
		hevc := m
		hevc.file = media.HEVCAAC
		checkPlayed(c, remuxPlay(c, srs, stream+ext, httpsOpts(c), hevc).Report, "hevc", "aac")
	})

	// HttpsResponseHeaders checks the answer over HTTPS: 200, the mount's
	// content type, and, for a page of another site, the CORS header.
	check.Case(t, "HttpsResponseHeaders", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		_, stream := publish(c, srs, "live", m.file)
		opts := httpsOpts(c)
		opts.Header = originHeader
		res := httpResponse(c, remuxPlay(c, srs, stream+ext, opts, m))
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), m.contentType, "content type")
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "*", "Access-Control-Allow-Origin")
	})

	// HttpsStreamNaming plays a multi-level app with a param over HTTPS:
	// on_play has the app, the stream, the param and the vhost, as over HTTP.
	check.Case(t, "HttpsStreamNaming", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		name, stream := publish(c, srs, "live/a/b", m.file)
		m.check(c, remuxPlay(c, srs, stream+ext+"?token=abc", httpsOpts(c), m).Report)
		h := waitHook(c, srs, "on_play", name)
		c.CheckEqual(h.Field("app"), "live/a/b", "on_play app")
		c.CheckEqual(h.Field("stream"), name, "on_play stream")
		c.CheckEqual(h.Field("param"), "?token=abc", "on_play param")
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "on_play vhost")
	})

	// HttpsAndHttpPlayers plays one stream with 2 HTTPS and 2 HTTP players at
	// once.
	check.Case(t, "HttpsAndHttpPlayers", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		_, stream := publish(c, srs, "live", m.file)
		players := []*remuxPlayer{
			remuxPlayerOf(c, srs, stream+ext, httpsOpts(c)),
			remuxPlayerOf(c, srs, stream+ext, nil),
			remuxPlayerOf(c, srs, stream+ext, httpsOpts(c)),
			remuxPlayerOf(c, srs, stream+ext, nil),
		}
		errs := make([]error, len(players))
		var wg sync.WaitGroup
		for i, p := range players {
			wg.Add(1)
			go func() {
				defer wg.Done()
				errs[i] = p.readUntil(m.until, time.Second)
			}()
		}
		wg.Wait()
		for i, p := range players {
			if c.CheckNoError(errs[i], fmt.Sprintf("player %d", i)) {
				m.check(c, p.Report)
			}
			c.CheckEqual(p.tls() != nil, i%2 == 0, fmt.Sprintf("player %d over TLS", i))
		}
	})

	// HttpsPlayerCloseCleanup closes an HTTPS player: its client is gone
	// from the API, and the stream counts only the publisher again.
	check.Case(t, "HttpsPlayerCloseCleanup", check.Normal, func(c *check.C) {
		srs := srsOf(c)
		name, stream := publish(c, srs, "live", m.file)
		p := remuxPlay(c, srs, stream+ext, httpsOpts(c), m)
		h := waitHook(c, srs, "on_play", name)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the player in the API")
		c.CheckEqual(streamClients(c, srs, streamPath), 2, "stream clients with a player")

		p.close()
		waitHook(c, srs, "on_stop", name)
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")
		c.CheckEqual(streamClients(c, srs, streamPath), 1, "stream clients after the player closed")
	})
}

// httpsOpts is a new player options that trusts the test certificate, so the
// player asks the HTTPS server.
func httpsOpts(c *check.C) *httpplay.Options {
	c.Helper()
	cfg, err := server.ClientTLS()
	c.Must(err, "the client TLS config")
	return &httpplay.Options{TLS: cfg}
}

// dialHTTPS is a TLS connection to the server's HTTPS server, which the
// client verified with the test certificate, closed when the case ends.
func dialHTTPS(c *check.C, srs *server.Server) *tls.Conn {
	c.Helper()
	cfg := httpsOpts(c).TLS
	cfg.ServerName = "localhost"
	d := tls.Dialer{NetDialer: &net.Dialer{Timeout: time.Second}, Config: cfg}
	conn, err := d.DialContext(context.Background(), "tcp", srs.Addr(server.HTTPSServer))
	c.Must(err, "dial the HTTPS server")
	c.Cleanup(func() { conn.Close() })
	return conn.(*tls.Conn)
}

// checkHTTPSServes checks the server still answers a request over HTTPS.
func checkHTTPSServes(c *check.C, srs *server.Server) {
	c.Helper()
	checkHTTPStatus(c, srs, "live/"+streamName(c)+".mp4", httpsOpts(c), 404)
}
