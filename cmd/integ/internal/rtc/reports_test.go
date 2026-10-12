// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"math"
	"strings"
	"testing"
	"time"

	"github.com/pion/rtcp"

	"srs-integ/internal/report"
)

// senderReports are the Sender Reports a session sent, by SSRC, in order.
func senderReports(rec *Record) map[uint32][]*rtcp.SenderReport {
	out := map[uint32][]*rtcp.SenderReport{}
	for _, p := range rec.RTCPOut {
		if sr, ok := p.(*rtcp.SenderReport); ok {
			out[sr.SSRC] = append(out[sr.SSRC], sr)
		}
	}
	return out
}

// ntpMillis is an NTP time in milliseconds, rounded as SRS reads it.
func ntpMillis(ntp uint64) int64 {
	return int64(ntp>>32)*1000 + int64(math.Round(float64(ntp&0xffffffff)*1000/(1<<32)))
}

// ssrcOf is the SSRC of the stream a session sent with a MIME type.
func ssrcOf(t *testing.T, rec *Record, mime string) uint32 {
	t.Helper()
	s := rec.Sent(mime)
	if s == nil {
		t.Fatalf("no %v stream sent: %v", mime, rec.Out)
	}
	return s.SSRC
}

// firstDTS is the decode time of the first sample of a kind.
func firstDTS(samples []Sample, video bool) time.Duration {
	for _, s := range samples {
		if s.Video == video {
			return s.DTS
		}
	}
	return -1
}

// TestPublishSenderReports checks the Sender Reports of a publisher that
// sends its own: two before each track's first packet, a second of media
// apart, then one at the first sample a second of media after the second,
// each mapping its RTP time to
// the NTP time of the same decode time at the track's clock rate, with the
// audio shifted; and none of pion's, which would come after a second.
func TestPublishSenderReports(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	const shift = 150 * time.Millisecond
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{SenderReports: true, AudioReportShift: shift})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()

	samples := readSamples(t, 2500*time.Millisecond)
	if err := pub.Send(ctx, samples, nil); err != nil {
		t.Fatal(err)
	}
	rec := pub.Recorder.Record()
	reports := senderReports(rec)
	video, audio := ssrcOf(t, rec, "video/H264"), ssrcOf(t, rec, "audio/opus")
	for _, track := range []struct {
		name  string
		ssrc  uint32
		clock int64
	}{{"video", video, 90000}, {"audio", audio, 48000}} {
		srs := reports[track.ssrc]
		if len(srs) != 3 {
			t.Errorf("%v: %v Sender Reports, want 3 (two at the start, then one 2s in)", track.name, len(srs))
			continue
		}
		for i, sr := range srs[:2] {
			if sr.PacketCount != 0 || sr.OctetCount != 0 {
				t.Errorf("%v: report %v counts %v packets, %v bytes, want before the first packet", track.name, i, sr.PacketCount, sr.OctetCount)
			}
		}
		if srs[2].PacketCount == 0 {
			t.Errorf("%v: report 2 counts no packets", track.name)
		}
		for i := 1; i < len(srs); i++ {
			ms := ntpMillis(srs[i].NTPTime) - ntpMillis(srs[i-1].NTPTime)
			ticks := int64(srs[i].RTPTime - srs[i-1].RTPTime)
			if ms < 1000 || ticks != ms*track.clock/1000 {
				t.Errorf("%v: reports %v and %v are %vms and %v ticks apart, want 1000ms or more at %v ticks a second", track.name, i-1, i, ms, ticks, track.clock)
			}
		}
	}
	vs, as := reports[video], reports[audio]
	if len(vs) > 0 && len(as) > 0 {
		got := time.Duration(ntpMillis(as[0].NTPTime)-ntpMillis(vs[0].NTPTime)) * time.Millisecond
		want := shift + firstDTS(samples, false) - firstDTS(samples, true)
		if got != want {
			t.Errorf("the first audio report is %v after the first video one, want %v", got, want)
		}
	}

	// pion's own reports, once a second of wall time, are off.
	n := len(rec.RTCPOut)
	time.Sleep(1100 * time.Millisecond)
	for _, p := range pub.Recorder.Record().RTCPOut[n:] {
		if sr, ok := p.(*rtcp.SenderReport); ok {
			t.Errorf("a Sender Report after the samples: %+v", sr)
		}
	}
}

// TestPublishReportFromPacketTime checks the first Sender Report of a track
// has the RTP time of its first packet: a player behind the fake server gets
// packets whose times map to the report's.
func TestPublishReportFromPacketTime(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{SenderReports: true})
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	samples := readSamples(t, 200*time.Millisecond)
	video := samples[0]
	for _, s := range samples {
		if s.Video {
			video = s
			break
		}
	}
	if err := pub.Write(video); err != nil {
		t.Fatal(err)
	}
	reports := senderReports(pub.Recorder.Record())
	ssrc := uint32(pub.video.ssrc)
	if len(reports[ssrc]) == 0 {
		t.Fatalf("no video Sender Report: %v", reports)
	}
	want := pub.video.sender.timestamp(video.DTS)
	if got := reports[ssrc][0].RTPTime; got != want {
		t.Errorf("first report RTP time %v, want %v, the first packet's", got, want)
	}
}

// TestPublishDefaultReports checks a publisher without SenderReports sends
// no Sender Report with its first packets: pion's come once a second.
func TestPublishDefaultReports(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer pub.Close()
	if err := pub.Send(ctx, readSamples(t, 200*time.Millisecond), nil); err != nil {
		t.Fatal(err)
	}
	if reports := senderReports(pub.Recorder.Record()); len(reports) != 0 {
		t.Errorf("Sender Reports %v, want none yet", reports)
	}
}

// TestPublishOneTrack publishes only audio, or only video, when the options
// offer no codec of the other kind: the offer has no section for it, a
// sample of it fails, and a player gets only the track sent.
func TestPublishOneTrack(t *testing.T) {
	for _, tc := range []struct {
		name  string
		opts  *Options
		video bool
	}{
		{"AudioOnly", &Options{Video: []string{}}, false},
		{"VideoOnly", &Options{Audio: []string{}}, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			sfu := newFakeSFU(t)
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			player, err := Play(ctx, sfu.URL+"/rtc/v1/whep/?app=live&stream=s", nil)
			if err != nil {
				t.Fatal(err)
			}
			defer player.Close()
			pub, err := Publish(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", tc.opts)
			if err != nil {
				t.Fatal(err)
			}
			defer pub.Close()

			missing := "m=audio"
			if !tc.video {
				missing = "m=video"
			}
			if strings.Contains(pub.Offer, missing) {
				t.Errorf("the offer has %v:\n%v", missing, pub.Offer)
			}
			var sent []Sample
			for _, s := range readSamples(t, time.Second) {
				if s.Video == tc.video {
					sent = append(sent, s)
				} else if err := pub.Write(s); err == nil {
					t.Fatalf("a sample of the track not offered was sent: %+v", s.Codec)
				}
			}
			if err := pub.Send(ctx, sent, nil); err != nil {
				t.Fatal(err)
			}
			u := report.Until{AudioFrames: 10}
			if tc.video {
				u = report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 10}
			}
			r, err := player.Until(ctx, u)
			if err != nil {
				t.Fatal(err)
			}
			if (r.Video != nil) != tc.video || (r.Audio != nil) != !tc.video {
				t.Errorf("the player got %v, want only the %v", r, map[bool]string{true: "video", false: "audio"}[tc.video])
			}
		})
	}
}
