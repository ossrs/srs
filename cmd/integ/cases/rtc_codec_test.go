// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"fmt"
	"net/http"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// rtcCodec is a stream a WHIP publisher sends: its test file, the codecs it
// offers and the vcodec it asks for, and what a player reports.
type rtcCodec struct {
	file          string
	video, audio  string
	vcodec        string
	report        string
	profile       string
	rate, channel int
}

var (
	rtcHevc = rtcCodec{media.HEVCOpus, "H265", "opus", "hevc", "hevc", "Main", 48000, 2}
	rtcVp9  = rtcCodec{media.VP9Opus, "VP9", "opus", "vp9", "vp9", "0", 48000, 2}
	rtcAv1  = rtcCodec{media.AV1Opus, "AV1", "opus", "av1", "av1", "Main", 48000, 2}
	rtcPcmu = rtcCodec{media.PCMU, "H264", "PCMU", "", "h264", "Baseline", 8000, 1}
	rtcPcma = rtcCodec{media.PCMA, "H264", "PCMA", "", "h264", "Baseline", 8000, 1}
)

// audioReport is the codec of the stream's audio in a report.
func (r rtcCodec) audioReport() string {
	if r.audio == "opus" {
		return "opus"
	}
	if r.audio == "PCMU" {
		return "pcmu"
	}
	return "pcma"
}

// everyCodec is a player that offers every codec SRS takes.
var everyCodec = rtc.Every()

// rtcCodecs adds the codec cases to TestRtc_Common: each codec SRS takes,
// published by WHIP and played by WHEP, a player that doesn't offer the
// stream's codec, and a player that comes before the publisher. SRS never
// transcodes between WebRTC sessions, so a player gets the stream's codec
// or is refused.
func rtcCodecs(t *testing.T) {
	// PublishPlayHevc, PublishPlayVp9 and PublishPlayAv1 publish with
	// vcodec, and PublishPlayPcmu and PublishPlayPcma offer only G.711: a
	// player that offers every codec is answered with the stream's codecs
	// only, and plays them.
	for _, w := range []struct {
		name string
		rtcCodec
	}{{"PublishPlayHevc", rtcHevc}, {"PublishPlayVp9", rtcVp9}, {"PublishPlayAv1", rtcAv1}, {"PublishPlayPcmu", rtcPcmu}, {"PublishPlayPcma", rtcPcma}} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			pub := codecPublisher(c, srs, stream, w.rtcCodec)
			c.CheckEqual(fmt.Sprint(answerCodecs(pub.Answer, "video"), answerCodecs(pub.Answer, "audio")),
				fmt.Sprint([]string{w.video}, []string{w.audio}), "the publisher's answer")
			player := rtcPlayer(c, whepURL(srs, "live", stream), everyCodec)
			c.CheckEqual(fmt.Sprint(answerCodecs(player.Answer, "video"), answerCodecs(player.Answer, "audio")),
				fmt.Sprint([]string{w.video}, []string{w.audio}), "the player's answer")
			sendRTCSamples(c, rtcFileSamples(c, w.file), pub, player)
			checkCodecPlayed(c, w.rtcCodec, waitPlayed(c, player, rtcPlayedUntil))
		})
	}

	// HevcToAvcOnlyPlayer, Vp9ToAvcOnlyPlayer and Av1ToAvcOnlyPlayer play a
	// stream with a player that offers only H.264: refused with 400, and
	// with vcodec=h264 too, while the same offer with the stream's codec is
	// answered. AvcToPlayerWithoutAvc plays the WHIP file with a player that
	// offers every video codec but H.264.
	for _, w := range []struct {
		name string
		rtcCodec
	}{{"HevcToAvcOnlyPlayer", rtcHevc}, {"Vp9ToAvcOnlyPlayer", rtcVp9}, {"Av1ToAvcOnlyPlayer", rtcAv1}} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			codecPublisher(c, srs, stream, w.rtcCodec)
			offer := rtcOffer(c, false, []string{"H264"}, []string{"opus"})
			rtcRefused(c, whepURL(srs, "live", stream), offer, http.StatusBadRequest)
			rtcRefused(c, whepURL(srs, "live", stream)+"&vcodec=h264", offer, http.StatusBadRequest)
			rtcAnswered(c, whepURL(srs, "live", stream), rtcOffer(c, false, []string{w.video}, []string{"opus"}))
		})
	}

	check.Case(t, "AvcToPlayerWithoutAvc", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
		offer := rtcOffer(c, false, []string{"H265", "VP9", "AV1"}, []string{"opus"})
		rtcRefused(c, whepURL(srs, "live", stream), offer, http.StatusBadRequest)
		rtcAnswered(c, whepURL(srs, "live", stream), rtcOffer(c, false, []string{"H265", "VP9", "AV1", "H264"}, []string{"opus"}))
	})

	// PcmuToOpusOnlyPlayer and PcmaToOpusOnlyPlayer play a G.711 stream
	// with a player that offers only Opus, and OpusToG711OnlyPlayer the
	// WHIP file with one that offers only G.711: refused with 400, while the
	// stream's audio codec is answered.
	for _, w := range []struct {
		name string
		rtcCodec
	}{{"PcmuToOpusOnlyPlayer", rtcPcmu}, {"PcmaToOpusOnlyPlayer", rtcPcma}} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			codecPublisher(c, srs, stream, w.rtcCodec)
			offer := rtcOffer(c, false, []string{"H264"}, []string{"opus"})
			rtcRefused(c, whepURL(srs, "live", stream), offer, http.StatusBadRequest)
			rtcAnswered(c, whepURL(srs, "live", stream), rtcOffer(c, false, []string{"H264"}, []string{w.audio}))
		})
	}

	check.Case(t, "OpusToG711OnlyPlayer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
		offer := rtcOffer(c, false, []string{"H264"}, []string{"PCMU", "PCMA"})
		rtcRefused(c, whepURL(srs, "live", stream), offer, http.StatusBadRequest)
		rtcAnswered(c, whepURL(srs, "live", stream), rtcOffer(c, false, []string{"H264"}, []string{"PCMU", "opus"}))
	})

	// PlayBeforePublishHevc and PlayBeforePublishAv1 play with vcodec
	// before the stream is published: SRS answers that codec, and the
	// player gets the stream once it is published with it.
	for _, w := range []struct {
		name string
		rtcCodec
	}{{"PlayBeforePublishHevc", rtcHevc}, {"PlayBeforePublishAv1", rtcAv1}} {
		check.Case(t, w.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			stream := streamName(c)
			player := rtcPlayer(c, whepURL(srs, "live", stream)+"&vcodec="+w.vcodec, everyCodec)
			c.CheckEqual(fmt.Sprint(answerCodecs(player.Answer, "video")), fmt.Sprint([]string{w.video}), "the player's video")
			pub := codecPublisher(c, srs, stream, w.rtcCodec)
			sendRTCSamples(c, rtcFileSamples(c, w.file), pub, player)
			checkCodecPlayed(c, w.rtcCodec, waitPlayed(c, player, rtcPlayedUntil))
		})
	}

	// PlayBeforePublishDefault plays with no vcodec before the stream is
	// published: SRS answers H.264, the first codec it has for a stream not
	// yet published.
	check.Case(t, "PlayBeforePublishDefault", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		player := rtcPlayer(c, whepURL(srs, "live", stream), everyCodec)
		c.CheckEqual(fmt.Sprint(answerCodecs(player.Answer, "video"), answerCodecs(player.Answer, "audio")),
			fmt.Sprint([]string{"H264"}, []string{"opus"}), "the player's answer")
	})

	// PlayBeforePublishVp9 plays with vcodec=vp9 before the stream is
	// published: refused, since SRS has tracks for H.264, HEVC and AV1 only
	// for a stream not yet published, while vcodec=av1 is answered.
	check.Case(t, "PlayBeforePublishVp9", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		offer := rtcOffer(c, false, []string{"VP9", "AV1"}, []string{"opus"})
		rtcRefused(c, whepURL(srs, "live", streamName(c))+"&vcodec=vp9", offer, http.StatusBadRequest)
		rtcAnswered(c, whepURL(srs, "live", streamName(c))+"&vcodec=av1", offer)
	})
}

// codecPublisher publishes a stream by WHIP with a codec: it offers only the
// codec's video and audio, and asks for its vcodec.
func codecPublisher(c *check.C, srs *server.Server, stream string, w rtcCodec) *rtc.Publisher {
	c.Helper()
	url := whipURL(srs, "live", stream)
	if w.vcodec != "" {
		url += "&vcodec=" + w.vcodec
	}
	return rtcPublisher(c, url, &rtc.Options{Video: []string{w.video}, Audio: []string{w.audio}})
}

// checkCodecPlayed checks the report of a player of a codec's stream: the
// video's codec, profile and size with one sequence header from the first
// frame, the audio's codec, rate and channels, and timestamps that only go
// forward, without gaps.
func checkCodecPlayed(c *check.C, w rtcCodec, r *report.Report) {
	c.Helper()
	if !c.Check(r.Video != nil && r.Audio != nil, "no video or audio: %v", r) {
		return
	}
	v, a := r.Video, r.Audio
	c.CheckEqual(v.Codec, w.report, "video codec")
	c.CheckEqual(v.Profile, w.profile, "video profile")
	c.Check(v.Width == 768 && v.Height == 320, "size %vx%v, want 768x320", v.Width, v.Height)
	c.CheckEqual(v.SequenceHeaders, 1, "video sequence headers")
	c.CheckEqual(v.LeadingFrames, 0, "frames before the first keyframe")
	c.CheckEqual(a.Codec, w.audioReport(), "audio codec")
	c.Check(a.SampleRate == w.rate && a.Channels == w.channel, "audio %vHz %vch, want %vHz %vch", a.SampleRate, a.Channels, w.rate, w.channel)
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
}
