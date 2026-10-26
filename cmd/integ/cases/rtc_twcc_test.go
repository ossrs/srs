// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"fmt"
	"slices"
	"testing"
	"time"

	"github.com/pion/rtcp"

	"srs-integ/internal/check"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

const (
	// rtcTwccBurst is the video a TWCC loss case sends at once after the
	// lead: about 40 packets, which SRS receives between two feedbacks, but
	// by chance.
	rtcTwccBurst = 1500 * time.Millisecond
	// rtcTwccSkip is how many packets of the burst pass before the lost
	// ones, so the feedback covers packets before them as well as after.
	rtcTwccSkip = 4
	// rtcTwccLongLoss is how many packets in a row TwccLostBurst loses:
	// more than the 14 a status vector chunk holds, so a run length chunk
	// reports them.
	rtcTwccLongLoss = 20
)

// TestRtc_TwccLoss tests that the TWCC feedback SRS sends a publisher is
// right about each packet: the ones the publisher loses in its own socket
// are reported lost, and every other one received. A sender lowers its
// bitrate by this feedback, so a lost packet reported received, or one left
// out, hides the loss from it. Nack is off, so the publisher sends no lost
// packet again. It shares the RemuxOff server, whose other groups play no
// WebRTC.
func TestRtc_TwccLoss(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	// TwccLostOne loses one packet in the middle of a burst.
	check.Case(t, "TwccLostOne", check.Normal, func(c *check.C) {
		pub, d, ssrc := rtcTwccBurstLoss(c, srs, 1)
		checkTwccLoss(c, pub, d, ssrc, 1)
	})

	// TwccLostBurst loses rtcTwccLongLoss packets in a row in the middle of
	// a burst.
	check.Case(t, "TwccLostBurst", check.Normal, func(c *check.C) {
		pub, d, ssrc := rtcTwccBurstLoss(c, srs, rtcTwccLongLoss)
		checkTwccLoss(c, pub, d, ssrc, rtcTwccLongLoss)
	})

	// TwccLostAfterFeedback loses the first packet the publisher sends after
	// a feedback, in real time: the next feedback must start at it, the one
	// after the last packet of the feedback before, not at the packet SRS
	// receives next.
	check.Case(t, "TwccLostAfterFeedback", check.Normal, func(c *check.C) {
		d := &rtc.Dropper{}
		pub, _, more := rtcRecoveryStart(c, srs, &rtc.Options{TWCC: true, Dropper: d}, nil)
		ssrc := pub.Recorder.Record().Sent(h264Mime).SSRC
		more()
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		fbs := len(rtc.TWCCFeedback(pub.Recorder.Record().RTCPIn))
		_, err := pub.Recorder.Until(ctx, func(rec *rtc.Record) bool { return len(rtc.TWCCFeedback(rec.RTCPIn)) > fbs })
		c.Must(err, "a feedback in real time")
		d.Drop(ssrc, 1, 1)
		checkTwccLoss(c, pub, d, ssrc, 1)
	})
}

// rtcTwccBurstLoss publishes video with the transport-wide sequence number,
// sends the first 200ms of the WHIP test file at once, then rtcTwccBurst at
// once, of which the publisher loses n packets in a row after rtcTwccSkip.
func rtcTwccBurstLoss(c *check.C, srs *server.Server, n int) (*rtc.Publisher, *rtc.Dropper, uint32) {
	c.Helper()
	d := &rtc.Dropper{}
	pub := rtcPublisher(c, whipURL(srs, "live", streamName(c)), &rtc.Options{TWCC: true, Dropper: d})
	samples := rtcVideoSamples(c)
	split := func(dts time.Duration) int {
		i := 0
		for i < len(samples) && samples[i].DTS < dts {
			i++
		}
		return i
	}
	lead, burst := split(rtcLead), split(rtcLead+rtcTwccBurst)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	c.Must(pub.Send(ctx, samples[:lead], nil), "send the lead")
	ssrc := pub.Recorder.Record().Sent(h264Mime).SSRC
	d.DropAfter(ssrc, rtcTwccSkip, n, 1)
	c.Must(pub.Send(ctx, samples[lead:burst], nil), "send the burst")
	return pub, d, ssrc
}

// checkTwccLoss waits until the publisher has lost n packets of the stream
// ssrc, and a feedback covers a packet after them, then checks the
// feedbacks say each packet sent up to the last they cover was received,
// but the lost ones.
func checkTwccLoss(c *check.C, pub *rtc.Publisher, d *rtc.Dropper, ssrc uint32, n int) {
	c.Helper()
	// lostTWCC are the transport-wide sequence numbers of the lost packets.
	lostTWCC := func(rec *rtc.Record) []uint16 {
		var out []uint16
		for _, seq := range d.Dropped(ssrc) {
			out = append(out, rec.Sent(h264Mime).TWCC[seq])
		}
		return out
	}
	// covered is the index in rec.TWCC of the last packet the feedbacks
	// cover, or -1.
	covered := func(rec *rtc.Record, got map[uint16]bool) int {
		for i := len(rec.TWCC) - 1; i >= 0; i-- {
			if _, ok := got[rec.TWCC[i]]; ok {
				return i
			}
		}
		return -1
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	rec, err := pub.Recorder.Until(ctx, func(rec *rtc.Record) bool {
		lost := lostTWCC(rec)
		if len(lost) < n {
			return false
		}
		got, err := rtc.TWCCReceived(rtc.TWCCFeedback(rec.RTCPIn))
		return err != nil || covered(rec, got) > slices.Index(rec.TWCC, lost[n-1])
	})
	c.Must(err, "a feedback past the lost packets")
	got, err := rtc.TWCCReceived(rtc.TWCCFeedback(rec.RTCPIn))
	c.Must(err, "the TWCC feedbacks")

	lost := lostTWCC(rec)
	var missing, wrong []uint16
	for _, seq := range rec.TWCC[:covered(rec, got)+1] {
		received, ok := got[seq]
		if !ok {
			missing = append(missing, seq)
		} else if received == slices.Contains(lost, seq) {
			wrong = append(wrong, seq)
		}
	}
	ranges := twccRanges(rtc.TWCCFeedback(rec.RTCPIn))
	c.Check(len(missing) == 0, "packets no feedback covers %v, of the lost %v; feedbacks cover %v", missing, lost, ranges)
	c.Check(len(wrong) == 0, "packets reported wrong %v, of the lost %v; feedbacks cover %v", wrong, lost, ranges)
}

// twccRanges are the first and last transport-wide sequence numbers each
// feedback covers, such as 0-15.
func twccRanges(fbs []*rtcp.TransportLayerCC) []string {
	var out []string
	for _, f := range fbs {
		out = append(out, fmt.Sprintf("%v-%v", f.BaseSequenceNumber, f.BaseSequenceNumber+f.PacketStatusCount-1))
	}
	return out
}
