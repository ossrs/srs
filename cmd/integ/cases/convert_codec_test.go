// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"net/http"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// The codec paths of the conversion matrix that the cells don't show: a
// player that can't take a stream's codec, and a WebRTC codec that SRS
// can't convert. SRS converts AAC to Opus for WebRTC (rtmp_to_rtc) and Opus
// to AAC from it (rtc_to_rtmp), and HEVC as it is; it never transcodes
// video, so a WebRTC player that offers none of the stream's codecs is
// refused. VP9, AV1 and G.711 are relay-only, WebRTC to WebRTC, as SRS's
// WebRTC docs say: no conversion to RTMP.

// whepCodecRefused publishes file from src to a stream of its own, waits
// until a WHEP player of the stream's codec, video "h264" or "hevc", plays
// it, so SRS has the stream's tracks, then checks that SRS refuses the
// offer of video and audio codecs with 400, for each query appended to the
// WHEP URL.
func whepCodecRefused(c *check.C, srs *server.Server, src convertSource, file, video string, offerVideo, offerAudio []string, queries ...string) {
	c.Helper()
	name := streamName(c)
	url, opts := whepURL(srs, "live", name), (*rtc.Options)(nil)
	if video == "hevc" {
		url += "&vcodec=hevc"
		opts = &rtc.Options{Video: []string{"H265"}}
	}
	p := rtcPlayer(c, url, opts)
	src.realTime(c, srs, "live/"+name, file, 0)
	waitPlayed(c, p, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 2})
	offer := rtcOffer(c, false, offerVideo, offerAudio)
	for _, q := range append([]string{""}, queries...) {
		rtcRefused(c, whepURL(srs, "live", name)+q, offer, http.StatusBadRequest)
	}
}

// whepCodecCells adds the cases of WebRTC players that can't take a
// converted stream's codec, on a server with rtmp_to_rtc.
func whepCodecCells(t *testing.T, srsOf func(c *check.C) *server.Server) {
	// SRS converts the video of an RTMP, SRT or GB28181 stream as it is, so a
	// player that offers only H.264 for an HEVC stream, with vcodec=h264 too,
	// or only HEVC for an H.264 one, is refused with 400.
	for _, src := range []struct {
		source convertSource
		file   string
	}{{rtmpSource, media.HEVCAAC}, {srtSource, media.HEVCAACTS}, {gbSource, media.HEVCAAC}} {
		check.Case(t, src.source.name+"ToWhep_HevcAacToH264OnlyPlayer", check.Normal, func(c *check.C) {
			whepCodecRefused(c, srsOf(c), src.source, src.file, "hevc", []string{"H264"}, []string{"opus"}, "&vcodec=h264")
		})
	}
	check.Case(t, "RtmpToWhep_H264AacToHevcOnlyPlayer", check.Normal, func(c *check.C) {
		whepCodecRefused(c, srsOf(c), rtmpSource, media.H264BaselineAAC, "h264", []string{"H265"}, []string{"opus"}, "&vcodec=hevc")
	})

	// SRS transcodes AAC to Opus only, so a player that offers only G.711
	// is refused.
	check.Case(t, "RtmpToWhep_H264AacToG711OnlyPlayer", check.Normal, func(c *check.C) {
		whepCodecRefused(c, srsOf(c), rtmpSource, media.H264BaselineAAC, "h264", []string{"H264"}, []string{"PCMU", "PCMA"})
	})

	// A player that comes before the publish is answered the video codec it
	// asks for by vcodec, H.264 by default, from the tracks SRS offers for a
	// stream not yet published. WebRTC can't change a session's codec, so
	// when the stream's video is another codec, the player gets the Opus SRS
	// transcodes from the AAC, and no video.
	for _, w := range []struct {
		name, file, vcodec, video string
	}{
		{"RtmpToWhep_H264AacToEarlyHevcPlayer", media.H264BaselineAAC, "&vcodec=hevc", "H265"},
		{"RtmpToWhep_HevcAacToEarlyH264Player", media.HEVCAAC, "", "H264"},
	} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			srs := srsOf(c)
			name := streamName(c)
			p := rtcPlayer(c, whepURL(srs, "live", name)+w.vcodec, &rtc.Options{Video: []string{w.video}})
			c.CheckEqual(answerCodecs(p.Answer, "video"), []string{w.video}, "the player's video")
			rtmpSource.realTime(c, srs, "live/"+name, w.file, 0)
			r := waitPlayed(c, p, report.Until{AudioFrames: 10})
			c.Check(r.Video == nil, "video %v, want none", r.Video)
			if c.Check(r.Audio != nil, "no audio: %v", r) {
				c.CheckEqual(r.Audio.Codec, "opus", "audio codec")
			}
		})
	}
}

// whipRelayOnlyCells adds the cases of the WebRTC codecs that SRS relays to
// WebRTC players only, on a server with rtc_to_rtmp.
func whipRelayOnlyCells(t *testing.T, srs *server.Server) {
	// VP9 and AV1 are not converted to RTMP: an RTMP player gets the AAC SRS
	// transcodes from the Opus, and no video, while a WHEP player plays the
	// stream as it was sent.
	for _, w := range []struct {
		name string
		rtcCodec
	}{{"WhipToRtmp_Vp9Opus", rtcVp9}, {"WhipToRtmp_Av1Opus", rtcAv1}} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			read := joinRTMP(c, srs, stream)
			pub := relayPublisher(c, srs, stream, w.rtcCodec)
			if pub == nil {
				c.Fatalf("WHIP publish of %v refused", w.video)
			}
			whep := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Video: []string{w.video}, Audio: []string{w.audio}})
			relaySend(c, pub, whep, w.rtcCodec)
			checkCodecPlayed(c, w.rtcCodec, waitPlayed(c, whep, rtcPlayedUntil))
			r := read(report.Until{AudioHeader: true, AudioFrames: 10})
			c.Check(r.Video == nil, "video %v, want none", r.Video)
			if c.Check(r.Audio != nil, "no audio: %v", r) {
				c.CheckEqual(r.Audio.Codec, "aac", "audio codec")
				c.Check(r.Audio.SampleRate == whipRate && r.Audio.Channels == 2, "audio %vHz %vch, want %vHz 2ch", r.Audio.SampleRate, r.Audio.Channels, whipRate)
			}
		})
	}

	// G.711 is not converted to RTMP, since SRS can't transcode it to AAC: an
	// RTMP player gets the H.264 and no audio, while a WHEP player plays the
	// stream as it was sent.
	for _, w := range []struct {
		name string
		rtcCodec
	}{{"WhipToRtmp_H264Pcmu", rtcPcmu}, {"WhipToRtmp_H264Pcma", rtcPcma}} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			read := joinRTMP(c, srs, stream)
			pub := relayPublisher(c, srs, stream, w.rtcCodec)
			if pub == nil {
				c.Fatalf("WHIP publish of %v refused", w.audio)
			}
			whep := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Video: []string{w.video}, Audio: []string{w.audio}})
			relaySend(c, pub, whep, w.rtcCodec)
			checkCodecPlayed(c, w.rtcCodec, waitPlayed(c, whep, rtcPlayedUntil))
			r := read(report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 10})
			c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
			if c.Check(r.Video != nil, "no video: %v", r) {
				c.CheckEqual(r.Video.Codec, "h264", "video codec")
				c.CheckEqual(r.Video.LeadingFrames, 0, "video frames before the first keyframe")
			}
		})
	}
}

// relayPublisher publishes a stream by WHIP with a codec, sending Sender
// Reports, which SRS needs to convert it; nil when SRS refuses the publish
// with an HTTP status.
func relayPublisher(c *check.C, srs *server.Server, stream string, w rtcCodec) *rtc.Publisher {
	c.Helper()
	url := whipURL(srs, "live", stream)
	if w.vcodec != "" {
		url += "&vcodec=" + w.vcodec
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	pub, err := rtc.Publish(ctx, url, rtcOptions(&rtc.Options{Video: []string{w.video}, Audio: []string{w.audio}, SenderReports: true}))
	var se *rtc.StatusError
	if errors.As(err, &se) {
		c.Check(se.Code >= 400, "WHIP publish status %v", se.Code)
		return nil
	}
	c.Must(err, "WHIP publish")
	c.Cleanup(func() { pub.Close() })
	return pub
}

// relaySend waits until the publisher gets the player's keyframe request,
// which shows SRS has subscribed the player, then sends the codec's test file
// like sendRTCSamples. The player asks again until then, as requestKeyframe
// does, since a request before SRS has subscribed it goes nowhere.
func relaySend(c *check.C, pub *rtc.Publisher, player *rtc.Player, w rtcCodec) {
	c.Helper()
	requestKeyframe(c, pub, player, "the player")
	whipSend(c, pub, rtcFileSamples(c, w.file), rtcLead)
}
