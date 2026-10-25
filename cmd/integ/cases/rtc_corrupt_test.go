// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// rtcCorrupt adds to TestRtc_Common the cases of a peer that sends corrupt
// packets on an established session: media that fails SRTP, media that
// passes SRTP but decodes to nothing valid, malformed RTCP, and packets of
// no known class.
//
// SRS protects a session with SRTP, so a packet a third party forges, or one
// that arrives damaged, fails the integrity check and is dropped. A packet
// the authenticated peer itself malforms passes SRTP, so SRS parses its
// contents, and must do so without crashing: the servers run under the
// sanitizer, which turns a bad read into a crash that fails the run.
//
// Over UDP SRS drops the bad packet and the session goes on. Over TCP, where
// one framed packet it can't handle leaves the byte stream out of sync, SRS
// ends the session, unpublishes the stream, and it can be published again.
func rtcCorrupt(t *testing.T) {
	// CorruptMediaDroppedUdp sends, next to each video packet, a copy whose
	// last byte, of the SRTP authentication tag, is flipped: SRS drops what
	// fails the tag, and plays the stream from the good packets.
	check.Case(t, "CorruptMediaDroppedUdp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rawSRTPPublisher(c, whipURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		pub.Tamper(func(b []byte, video bool) [][]byte {
			if !video {
				return [][]byte{b}
			}
			bad := append([]byte(nil), b...)
			bad[len(bad)-1] ^= 0xff
			return [][]byte{b, bad}
		})
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+h.Field("client_id")), 0, "the publisher in the API")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, "on_unpublish requests")
	})

	// CorruptMediaEndsTcpSession sends one packet framed as RTP that fails
	// SRTP over TCP: SRS can't unprotect it, and ends the session rather
	// than drop the packet and read the next frame from a stream it can no
	// longer trust.
	check.Case(t, "CorruptMediaEndsTcpSession", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rawSRTPPublisherTCP(c, whipURL(srs, "live", stream))
		h := waitHook(c, srs, "on_publish", stream)
		clientPath := "/api/v1/clients/" + h.Field("client_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")
		c.Must(pub.WriteRaw([]byte{0x80, 96, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0xde, 0xad}), "write a corrupt packet")
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after a corrupt packet")
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
	})

	// MalformedVideoSurvives replaces each video payload, after its RTP
	// header, with a malformed H.264 payload that passes SRTP: an empty one,
	// a lone FU-A header, a FU-A fragment with neither the start nor the end
	// bit, and a STAP-A whose NALU length runs past the packet. SRS parses
	// each without crashing, and keeps the publisher. There is no player,
	// because SRS relays a publisher's RTP to one unparsed, so a player's own
	// depacketizer, not SRS, would be what the malformed payload reaches.
	check.Case(t, "MalformedVideoSurvives", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rawSRTPPublisher(c, whipURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		bad := [][]byte{
			{},
			{0x7c},
			{0x7c, 0x01, 0xaa, 0xbb},
			{0x78, 0xff, 0xff, 0x01},
		}
		var n int
		pub.Rewrite(func(b []byte, video bool) [][]byte {
			if !video || len(b) < 12 {
				return [][]byte{b}
			}
			p := append(append([]byte(nil), b[:12]...), bad[n%len(bad)]...)
			n++
			return [][]byte{p}
		})
		var burst []rtc.Sample
		for _, s := range rtcSamples(c) {
			if s.DTS < 300*time.Millisecond {
				burst = append(burst, s)
			}
		}
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		c.Must(pub.Send(ctx, burst, nil), "send malformed video")
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+h.Field("client_id")), 0, "the publisher in the API")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, "on_unpublish requests")
	})

	// MalformedRtcpSurvives sends malformed RTCP that passes SRTP: a Sender
	// Report and a Receiver Report that count more report blocks than they
	// carry, a NACK feedback and a payload-specific feedback whose FCI is
	// truncated, and a Sender Report whose length runs past its end. SRS
	// parses each without crashing, and the stream plays on.
	check.Case(t, "MalformedRtcpSurvives", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rawSRTPPublisher(c, whipURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
		for _, b := range [][]byte{
			{0x9f, 200, 0, 1, 0, 0, 0, 1},
			{0x82, 201, 0, 7, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{0x81, 205, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0xff},
			{0x81, 206, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2},
			{0x80, 200, 0, 9, 0, 0, 0, 1},
		} {
			c.Must(pub.WriteRTCP(b), "write malformed RTCP")
		}
		checkRTCPlayed(c, waitPlayed(c, player, framesAfter(player, rtcSwitchFrames)))
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+h.Field("client_id")), 0, "the publisher in the API")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, "on_unpublish requests")
	})

	// UnknownPacketIgnoredUdp sends a packet of no known class, its first
	// byte neither STUN, DTLS nor RTP or RTCP: SRS drops it and the session
	// goes on.
	check.Case(t, "UnknownPacketIgnoredUdp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rawSRTPPublisher(c, whipURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
		c.Must(pub.WriteRaw([]byte{0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), "write an unknown packet")
		checkRTCPlayed(c, waitPlayed(c, player, framesAfter(player, rtcSwitchFrames)))
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+h.Field("client_id")), 0, "the publisher in the API")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, "on_unpublish requests")
	})

	// UnknownPacketEndsTcpSession sends the same unknown packet over TCP: SRS
	// ends the session.
	check.Case(t, "UnknownPacketEndsTcpSession", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rawSRTPPublisherTCP(c, whipURL(srs, "live", stream))
		h := waitHook(c, srs, "on_publish", stream)
		clientPath := "/api/v1/clients/" + h.Field("client_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")
		c.Must(pub.WriteRaw([]byte{0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), "write an unknown packet")
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after an unknown packet")
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
	})

	// BadStunIgnoredUdp sends a packet that classes as STUN, by its first
	// byte, but doesn't decode: SRS can't parse it and drops it, and the
	// session goes on.
	check.Case(t, "BadStunIgnoredUdp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rawSRTPPublisher(c, whipURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
		c.Must(pub.WriteRaw([]byte{0x00, 0x01, 0xff, 0xff, 0x21, 0x12, 0xa4, 0x42, 0, 0, 0, 0, 0, 0, 0, 0}), "write a bad STUN packet")
		checkRTCPlayed(c, waitPlayed(c, player, framesAfter(player, rtcSwitchFrames)))
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+h.Field("client_id")), 0, "the publisher in the API")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, "on_unpublish requests")
	})
}

// rawSRTPPublisher connects a raw WHIP publisher that protects its media with
// SRTP, as an encrypted WebRTC publisher does, closed when the case ends. It
// can corrupt what it sends with Tamper, Rewrite, WriteRTCP and WriteRaw.
func rawSRTPPublisher(c *check.C, url string, opts *rtc.Options) *rtc.RawPublisher {
	c.Helper()
	o := rtcOptions(opts)
	o.SRTP = true
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	pub, err := rtc.RawPublish(ctx, url, o, true)
	c.Must(err, "raw WHIP publish with SRTP")
	c.Cleanup(func() { pub.Close() })
	return pub
}

// rawSRTPPublisherTCP is rawSRTPPublisher over TCP.
func rawSRTPPublisherTCP(c *check.C, url string) *rtc.RawPublisher {
	c.Helper()
	return rawSRTPPublisher(c, url, &rtc.Options{TCP: true})
}
