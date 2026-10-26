// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"slices"
	"testing"
	"time"

	"github.com/pion/rtcp"

	"srs-integ/internal/check"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// rtcLoss adds to TestRtc_Common the cases of packets the network loses,
// which the client loses in its own socket, with no help from SRS: a
// publisher's packets never leave it, and a player's never reach its SRTP.
// SRS asks the publisher for the lost packets by NACK, and sends the player
// the ones it asks for again, on the media stream, since Common keeps
// nack_prefer_rtx off. The cases publish only video, as the recovery cases.
func rtcLoss(t *testing.T) {
	// LossPublisherPlain loses packets the publisher sends: SRS NACKs
	// exactly them, the publisher sends each again, and the player gets
	// them, late, where they belong.
	check.Case(t, "LossPublisherPlain", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		d := &rtc.Dropper{}
		pub, player, more := rtcRecoveryStart(c, srs, &rtc.Options{Dropper: d}, nil)
		ssrc := pub.Recorder.Record().Sent(h264Mime).SSRC
		d.Drop(ssrc, rtcDrop, 1)
		more()
		nacked := waitPublisherResent(c, pub, false)
		c.Check(slices.Equal(nacked, d.Dropped(ssrc)), "NACKed %v, want the lost %v", nacked, d.Dropped(ssrc))
		waitPlayerRecovered(c, player, false)
	})

	// LossPlayerPlain loses packets SRS sends the player: the player NACKs
	// exactly them, and SRS sends each again on the media stream.
	check.Case(t, "LossPlayerPlain", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		d := &rtc.Dropper{}
		_, player, more := rtcRecoveryStart(c, srs, nil, &rtc.Options{Dropper: d})
		ssrc := player.Recorder.Record().Received(h264Mime).SSRC
		d.Drop(ssrc, rtcDrop, 1)
		more()
		rec := waitPlayerRecovered(c, player, false)
		checkPlayerNacked(c, rec)
		missed := rec.Received(h264Mime).Missed
		c.Check(slices.Equal(missed, d.Dropped(ssrc)), "missed %v, want the lost %v", missed, d.Dropped(ssrc))
		c.Check(rec.Received(rtxMime) == nil, "an RTX stream, want none with nack_prefer_rtx off")
	})

	// LossPublisherResendLost loses a packet the publisher sends and its
	// first resend: SRS NACKs it again, and the second resend reaches the
	// player.
	check.Case(t, "LossPublisherResendLost", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		d := &rtc.Dropper{}
		pub, player, more := rtcRecoveryStart(c, srs, &rtc.Options{Dropper: d}, nil)
		ssrc := pub.Recorder.Record().Sent(h264Mime).SSRC
		d.Drop(ssrc, 1, 2)
		more()
		waitPlayerLate(c, player, 1)
		lost := checkLostTwice(c, d, ssrc)
		rec := pub.Recorder.Record()
		c.Check(nackTimes(rec.RTCPIn, ssrc, lost) >= 2, "SRS NACKed %v %v times, want twice or more", lost, nackTimes(rec.RTCPIn, ssrc, lost))
		resent := 0
		for _, seq := range rec.Sent(h264Mime).Repeated {
			if seq == lost {
				resent++
			}
		}
		c.Check(resent >= 2, "the publisher sent %v again %v times, want twice or more", lost, resent)
	})

	// LossPlayerResendLost loses a packet SRS sends the player and its first
	// resend: the player NACKs it again, and SRS sends it again.
	check.Case(t, "LossPlayerResendLost", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		d := &rtc.Dropper{}
		_, player, more := rtcRecoveryStart(c, srs, nil, &rtc.Options{Dropper: d})
		ssrc := player.Recorder.Record().Received(h264Mime).SSRC
		d.Drop(ssrc, 1, 2)
		more()
		rec := waitPlayerLate(c, player, 1)
		lost := checkLostTwice(c, d, ssrc)
		c.Check(slices.Equal(rec.Received(h264Mime).Late, []uint16{lost}), "late %v, want %v", rec.Received(h264Mime).Late, lost)
		c.Check(nackTimes(rec.RTCPOut, ssrc, lost) >= 2, "the player NACKed %v %v times, want twice or more", lost, nackTimes(rec.RTCPOut, ssrc, lost))
	})
}

// rtcLossRtx adds to TestRtc_PreferRtx the cases of lost packets with RTX:
// a publisher sends the packets SRS NACKs on its RTX stream, and SRS sends a
// player the ones it lost on its own.
func rtcLossRtx(t *testing.T, srs *server.Server) {
	// LossPublisherRtx loses packets a publisher with RTX sends: SRS NACKs
	// exactly them, the publisher sends each on its RTX stream, and the
	// player gets them on the media stream.
	check.Case(t, "LossPublisherRtx", check.Normal, func(c *check.C) {
		d := &rtc.Dropper{}
		pub, player, more := rtcRecoveryStart(c, srs, &rtc.Options{RTX: true, Dropper: d}, nil)
		ssrc := pub.Recorder.Record().Sent(h264Mime).SSRC
		d.Drop(ssrc, rtcDrop, 1)
		more()
		nacked := waitPublisherResent(c, pub, true)
		c.Check(slices.Equal(nacked, d.Dropped(ssrc)), "NACKed %v, want the lost %v", nacked, d.Dropped(ssrc))
		c.CheckEqual(len(pub.Recorder.Record().Sent(h264Mime).Repeated), 0, "packets sent again on the media stream")
		waitPlayerRecovered(c, player, false)
	})

	// LossPlayerRtx loses packets SRS sends a player with RTX: the player
	// NACKs exactly them, and SRS sends each on the RTX stream.
	check.Case(t, "LossPlayerRtx", check.Normal, func(c *check.C) {
		d := &rtc.Dropper{}
		_, player, more := rtcRecoveryStart(c, srs, nil, &rtc.Options{RTX: true, Dropper: d})
		ssrc := player.Recorder.Record().Received(h264Mime).SSRC
		d.Drop(ssrc, rtcDrop, 1)
		more()
		rec := waitPlayerRecovered(c, player, true)
		checkPlayerNacked(c, rec)
		missed := rec.Received(h264Mime).Missed
		c.Check(slices.Equal(missed, d.Dropped(ssrc)), "missed %v, want the lost %v", missed, d.Dropped(ssrc))
	})
}

// rtcLossNackOff adds to TestRtc_NackTwccOff the case of lost packets with
// nack off.
func rtcLossNackOff(t *testing.T, srs *server.Server) {
	// LossPublisherNackOff loses packets the publisher sends: SRS NACKs
	// none, and the player never gets them.
	check.Case(t, "LossPublisherNackOff", check.Normal, func(c *check.C) {
		d := &rtc.Dropper{}
		pub, player, more := rtcRecoveryStart(c, srs, &rtc.Options{Dropper: d}, nil)
		ssrc := pub.Recorder.Record().Sent(h264Mime).SSRC
		d.Drop(ssrc, rtcDrop, 1)
		more()
		// SRS has the packets after the lost ones at once.
		time.Sleep(rtcNackQuiet)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Recorder.Until(ctx, func(rec *rtc.Record) bool {
			v := rec.Received(h264Mime)
			return v != nil && len(v.Missed) >= rtcDrop
		})
		c.Must(err, "the player misses the lost packets")
		c.CheckEqual(len(d.Dropped(ssrc)), rtcDrop, "lost packets")
		rec := pub.Recorder.Record()
		c.CheckEqual(len(rtc.Nacked(rec.RTCPIn, ssrc)), 0, "packets SRS NACKed")
		c.CheckEqual(len(rec.Sent(h264Mime).Repeated), 0, "packets the publisher sent again")
		c.CheckEqual(len(player.Recorder.Record().Received(h264Mime).Late), 0, "packets the player got late")
	})
}

// waitPlayerLate waits at most 1s until the player has got n missed video
// packets late.
func waitPlayerLate(c *check.C, p *rtc.Player, n int) *rtc.Record {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	rec, err := p.Recorder.Until(ctx, func(rec *rtc.Record) bool {
		v := rec.Received(h264Mime)
		return v != nil && len(v.Late) >= n
	})
	if err != nil {
		c.Fatalf("the player gets %v lost packets: %v; video %+v", n, err, rec.Received(h264Mime))
	}
	return rec
}

// checkLostTwice checks the dropper lost one packet of the stream ssrc,
// twice, and returns its sequence number.
func checkLostTwice(c *check.C, d *rtc.Dropper, ssrc uint32) uint16 {
	c.Helper()
	lost := d.Dropped(ssrc)
	if len(lost) != 2 || lost[0] != lost[1] {
		c.Fatalf("lost %v, want a packet twice", lost)
	}
	return lost[0]
}

// nackTimes is how many of the NACKs among pkts ask for the packet seq of
// the stream ssrc.
func nackTimes(pkts []rtcp.Packet, ssrc uint32, seq uint16) int {
	n := 0
	for _, p := range pkts {
		if nack, ok := p.(*rtcp.TransportLayerNack); ok && nack.MediaSSRC == ssrc {
			for _, pair := range nack.Nacks {
				if slices.Contains(pair.PacketList(), seq) {
					n++
				}
			}
		}
	}
	return n
}
