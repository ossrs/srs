// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"fmt"
	"net"
	"testing"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// rtcOnHTTPPortConfig is a server whose WebRTC over TCP listens on the HTTP
// server's port, with HTTP-FLV: SRS peeks at each client's first bytes on that
// port to tell a WebRTC client from an HTTP one.
func rtcOnHTTPPortConfig() server.Config {
	cfg := server.WithRTC(server.RemuxConfig("flv"), server.RTC{})
	cfg.Listeners = append(cfg.Listeners, server.RTCUDP, server.RTCTCP)
	cfg.RTCOnHTTPPort = true
	return cfg
}

// TestRtc_TcpOnHttpPort checks HTTP and WebRTC over TCP on one port, while
// other clients of that port connect and send too few bytes to tell what they
// are, or none, as a browser's preconnect does: each client waits for its own
// bytes, so those idle clients hold no other client.
func TestRtc_TcpOnHttpPort(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, rtcOnHTTPPortConfig())

	// OnePort checks the answer's TCP candidate is the HTTP server's port.
	check.Case(t, "OnePort", check.Normal, func(c *check.C) {
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		c.CheckEqual(fmt.Sprint(candidateList(s.Answer)), fmt.Sprint([]string{
			"udp " + srs.Addr(server.RTCUDP), "tcp " + srs.Addr(server.HTTPServer) + " passive",
		}), "candidates")
	})

	// FlvWhileClientsIdle plays HTTP-FLV on the port while idle clients
	// hold connections to it.
	check.Case(t, "FlvWhileClientsIdle", check.Normal, func(c *check.C) {
		idleClients(c, srs)
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), publishLead)
		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkPlayed(c, readHTTPFlv(c, p, playedUntil), "h264", "aac")
	})

	// TcpPublishPlayWhileClientsIdle publishes and plays with ICE only over
	// TCP, on the port, while idle clients hold connections to it.
	check.Case(t, "TcpPublishPlayWhileClientsIdle", check.Normal, func(c *check.C) {
		idleClients(c, srs)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TCP: true})
		player := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{TCP: true})
		c.CheckEqual(pub.Transport(), "tcp", "publisher transport")
		c.CheckEqual(player.Transport(), "tcp", "player transport")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// ClientsLeave checks idle clients that leave before sending enough
	// don't stop the port: HTTP-FLV plays after they close.
	check.Case(t, "ClientsLeave", check.Normal, func(c *check.C) {
		for _, conn := range idleClients(c, srs) {
			conn.Close()
		}
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), publishLead)
		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkPlayed(c, readHTTPFlv(c, p, playedUntil), "h264", "aac")
	})
}

// idleClients connects to the HTTP server's port as clients that send fewer
// bytes than SRS peeks to tell WebRTC from HTTP, and then wait: one sends
// nothing, one the start of an HTTP request, and one the start of a STUN
// binding request framed for TCP. They are closed when the case ends.
func idleClients(c *check.C, srs *server.Server) []net.Conn {
	c.Helper()
	var conns []net.Conn
	for _, first := range [][]byte{nil, []byte("GET "), {0x00, 0x6c, 0x00, 0x01}} {
		conn, err := net.Dial("tcp", srs.Addr(server.HTTPServer))
		c.Must(err, "dial the HTTP server's port")
		c.Cleanup(func() { conn.Close() })
		if len(first) > 0 {
			_, err = conn.Write(first)
			c.Must(err, "write the first bytes")
		}
		conns = append(conns, conn)
	}
	return conns
}
