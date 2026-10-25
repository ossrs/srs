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
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// rtcRebind adds to TestRtc_Common the cases of a client whose address
// changes during a session, as when a phone goes from Wi-Fi to 4G or a NAT
// rebinds: the client moves its socket to another local port, which SRS
// sees as it sees another IP. SRS moves a UDP session to the address of a
// STUN binding request signed with the session's ICE password, and drops RTP
// and RTCP from an address it doesn't know; pion sends a binding request
// every check interval, 20ms.
func rtcRebind(t *testing.T) {
	// PublisherSwitchPort moves the publisher to another port in the middle
	// of the stream: SRS sends its RTCP there, and no more to the old port,
	// the player plays on, and the publisher stays the same client, with no
	// on_unpublish.
	check.Case(t, "PublisherSwitchPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		r := &rtc.Rebinder{}
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{Rebinder: r, TWCC: true})
		h := waitHook(c, srs, "on_publish", stream)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		_, err := r.Rebind()
		c.Must(err, "move the publisher to another port")
		waitPlayed(c, player, framesAfter(player, rtcSwitchFrames))
		old := r.Media(0)
		checkRTCPlayed(c, waitPlayed(c, player, framesAfter(player, rtcFeedbackFrames)))
		c.Check(r.Media(1) > 0, "no RTCP on the publisher's new port")
		c.CheckEqual(r.Media(0), old, "RTCP on the publisher's old port after it moved")
		c.CheckEqual(apiCode(c, srs, "/api/v1/clients/"+h.Field("client_id")), 0, "the publisher in the API")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, "on_unpublish requests")
	})

	// PlayerSwitchPort moves the player to another port in the middle of
	// the stream: SRS sends the media there, and no more to the old port.
	check.Case(t, "PlayerSwitchPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		r := &rtc.Rebinder{}
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Rebinder: r})
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		_, err := r.Rebind()
		c.Must(err, "move the player to another port")
		waitPlayed(c, player, framesAfter(player, rtcSwitchFrames))
		old := r.Media(0)
		checkRTCPlayed(c, waitPlayed(c, player, framesAfter(player, rtcSwitchFrames)))
		c.Check(r.Media(1) > 0, "no media on the player's new port")
		c.CheckEqual(r.Media(0), old, "media on the player's old port after it moved")
	})

	// SwitchBackToOldPort moves the player to another port and back: SRS
	// sends the media to the first port again, and no more to the second.
	check.Case(t, "SwitchBackToOldPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		r := &rtc.Rebinder{}
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Rebinder: r})
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		_, err := r.Rebind()
		c.Must(err, "move the player to another port")
		waitPlayed(c, player, framesAfter(player, rtcSwitchFrames))
		c.Check(r.Media(1) > 0, "no media on the player's second port")
		c.Must(r.Use(0), "move the player back to its first port")
		first := r.Media(0)
		waitPlayed(c, player, framesAfter(player, rtcSwitchFrames))
		second := r.Media(1)
		checkRTCPlayed(c, waitPlayed(c, player, framesAfter(player, rtcSwitchFrames)))
		c.Check(r.Media(0) > first, "no media on the player's first port after it moved back")
		c.CheckEqual(r.Media(1), second, "media on the player's second port after it moved back")
	})

	// SwitchMediaBeforeStun moves the publisher to another port and holds
	// its binding requests: SRS drops the media from the new port, so the
	// player gets none, until the next binding request moves the session
	// and the player plays on.
	check.Case(t, "SwitchMediaBeforeStun", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		r := &rtc.Rebinder{}
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{Rebinder: r})
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		c.Must(r.HoldSTUN(true), "hold the publisher's binding requests")
		_, err := r.Rebind()
		c.Must(err, "move the publisher to another port")
		time.Sleep(2 * rtcFrame)
		held := player.Report().Video.Frames
		time.Sleep(3 * rtcFrame)
		c.CheckEqual(player.Report().Video.Frames, held, "video frames from the new port before its binding request")

		c.Must(r.HoldSTUN(false), "send the publisher's binding requests")
		waitPlayed(c, player, framesAfter(player, rtcSwitchFrames))
	})

	// SwitchUnsignedBindingIgnored binds the player's session from a new
	// socket with another password: SRS doesn't move the session there, as
	// RFC 8445 section 7.3 says, so the socket gets no media; one signed
	// with the answer's password gets it, which shows the case would see a
	// move. The player holds its own binding requests meanwhile, or the next
	// one would move the session back within 20ms; once it sends them again,
	// it plays on.
	check.Case(t, "SwitchUnsignedBindingIgnored", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		r := &rtc.Rebinder{}
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Rebinder: r})
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		c.Must(r.HoldSTUN(true), "hold the player's binding requests")
		n := rtcIntrude(c, player.Offer, withICE(player.Answer, "", "wrongpassword0123456789"))
		c.CheckEqual(n, 0, "media to a binding signed with another password")
		n = rtcIntrude(c, player.Offer, player.Answer)
		c.Check(n > 0, "no media to a binding signed with the answer's password")

		c.Must(r.HoldSTUN(false), "send the player's binding requests")
		waitPlayed(c, player, framesAfter(player, rtcSwitchFrames))
	})

	// TcpSwitchEndsSession closes the TCP connection of a publisher's
	// session after DTLS, as a client that leaves its network does, and
	// binds the session from a new connection: SRS ended the session with
	// the first connection, so it closes the new one. A TCP session doesn't
	// move to another address, unlike a UDP one; this passes from the start
	// and holds that limit.
	check.Case(t, "TcpSwitchEndsSession", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		pub, err := rtc.RawPublish(ctx, whipURL(srs, "live", stream)+"&encrypt=false", rtcOptions(&rtc.Options{TCP: true}), true)
		c.Must(err, "raw WHIP publish over TCP")
		c.Cleanup(func() { pub.Close() })
		waitHook(c, srs, "on_publish", stream)

		pub.Abort()
		waitHook(c, srs, "on_unpublish", stream)
		_, err = rtcBind(c, pub.Offer, pub.Answer, &rtc.Options{TCP: true}, 300*time.Millisecond)
		c.Check(errors.Is(err, rtc.ErrClosed), "binding from a new TCP connection: %v, want closed", err)
	})
}

// rtcFrame is the time of a video frame of the WHIP test file, 25 fps.
const rtcFrame = 40 * time.Millisecond

// rtcSwitchFrames is how many more video frames a case waits for after a
// client moves, longer than ICE's 20ms check interval.
const rtcSwitchFrames = 2

// rtcFeedbackFrames is how many more video frames a publisher case waits for
// RTCP from SRS, longer than its 100ms TWCC feedback interval: without TWCC,
// SRS sends a publisher only a Receiver Report each second.
const rtcFeedbackFrames = 3

// framesAfter is when a player has n video frames more than now, and has
// seen the stream play.
func framesAfter(p rtcReceiver, n int) report.Until {
	u := rtcPlayedUntil
	if v := p.Report().Video; v != nil {
		u.VideoFrames = v.Frames + n
	}
	return u
}

// rtcIntrude binds a session from a new socket for 100ms, and returns how
// many media packets came to it.
func rtcIntrude(c *check.C, offer, answer string) int {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	n, err := rtc.Intrude(ctx, offer, answer, nil)
	c.Must(err, "bind from a new socket")
	return n
}
