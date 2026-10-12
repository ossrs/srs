// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"os"
	"testing"
	"time"

	"github.com/bluenviron/gortsplib/v5/pkg/base"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// The SRT source of the conversion matrix: an SRT publisher of an MPEG-TS
// test file, to each player. With srt_to_rtmp, on wherever SRT is, SRS
// converts the TS to an RTMP stream, which every RTMP player and remux plays,
// and with rtmp_to_rtc to WebRTC too. It converts H.264, HEVC and AAC; other
// TS codecs, such as Opus, it drops, and SRT players get the TS as sent. A
// cell is named SrtTo<Player>_<Codec>, such as SrtToHlsTs_HevcAac.

// srtCodecs are the codecs an SRT source publishes, one TS test file each,
// its video with B-frames.
var srtCodecs = []convertCodec{
	{"H264Aac", media.H264HighAACTS, "h264", "aac"},
	{"HevcAac", media.HEVCAACTS, "hevc", "aac"},
	{"H264Opus", media.H264OpusTS, "h264", "opus"},
	{"AacOnly", media.AACOnlyTS, "", "aac"},
	{"H264Only", media.H264OnlyTS, "h264", ""},
}

// srtConverted are the codecs of the stream SRS converts a codec to: the
// same, without Opus, which it drops.
func srtConverted(codec convertCodec) convertCodec {
	if codec.audio == "opus" {
		codec.audio = ""
	}
	return codec
}

// srtSync is the largest A/V sync difference of a stream from the TS test
// files: they put the audio of a moment up to about 190ms from the video in
// file order (TestGeneratedTS), and SRS converts the frames in the order
// they come.
const srtSync = 250 * time.Millisecond

// srtCells adds a case for each codec of the SRT source.
func srtCells(t *testing.T, srsOf func(c *check.C) *server.Server, p convertPlayer) {
	for _, codec := range srtCodecs {
		check.Case(t, srtSource.name+"To"+p.name+"_"+codec.name, check.Normal, func(c *check.C) {
			if reason, ok := p.knownBugs[codec.name]; ok {
				check.KnownBug(c, reason)
			}
			srs := srsOf(c)
			w := p.want(codec)
			if w.video == "" && w.audio == "" {
				p.none(c, srs, srtSource, srtConverted(codec))
				return
			}
			checkConverted(c, p.play(c, srs, srtSource, p.fileOf(codec), w), w, p.gap, max(p.sync, srtSync))
		})
	}
}

// srtRealTime publishes a TS file over SRT to a stream path, such as live/s,
// in real time until the case ends, srtLead of it at once.
func srtRealTime(c *check.C, srs *server.Server, path, file string) {
	c.Helper()
	startSRTPublisher(c, srs, srt.Options{StreamID: srtID(path, "publish")}, file)
}

// srtThrough publishes a TS file over SRT to a stream path at once, in a
// burst slot of the server, and keeps the stream published until the case
// ends. It sends through the packets at d+srtLead by their PCR: ffmpeg's TS
// muxer sends a frame before its PCR reaches the frame's time, and the audio
// after the video, so this sends the frames at d of both tracks.
func srtThrough(c *check.C, srs *server.Server, path, file string, d time.Duration) {
	c.Helper()
	conn := dialSRT(c, srs, srt.Options{StreamID: srtID(path, "publish")})
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	defer f.Close()
	free := srs.Burst(c)
	defer free()
	c.Must(srt.NewSender(f).SendThrough(conn, d+srtLead), "publish the media")
}

// srtSegmentsWait is how long a segment player of a stream sent at once over
// SRT waits for its files: SRS has written them a moment after the send, and
// a known bug's player, which gets none, must not wait past the Normal limit.
const srtSegmentsWait = 300 * time.Millisecond

// playHLSWithin plays HLS until u, for at most d.
func playHLSWithin(c *check.C, url string, u report.Until, d time.Duration) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	r, err := hls.PlayHTTP(ctx, httpClient, url, u)
	noPaddingBug(c, err)
	c.Must(err, "play")
	return r
}

// srtConvertPlayer plays SRT: SRS relays the TS as it is sent, Opus too. It
// keeps no GOP for SRT, so the player connects before the publisher.
var srtConvertPlayer = convertPlayer{
	name: "Srt", video: same("h264", "hevc"), audio: same("aac", "opus"),
	play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
		name := streamName(c)
		path := "live/" + name
		player := srtPlayer(c, srs, srt.Options{StreamID: srtID(path, "request")}, name)
		src.realTime(c, srs, path, file, convertLead)
		return srtAnalyzeUntil(c, player, convertUntil(w), time.Second)
	},
	gap: 100 * time.Millisecond, sync: srtSync,
}

// srtDashBugs are the known bugs of DASH of the SRT source: DASH writes no
// MPD for a stream with no video or no audio, as is the one of H.264 with
// Opus once SRS drops the Opus.
var srtDashBugs = map[string]string{
	"H264Opus": "DASH writes no MPD for a stream with no audio, as SRS converts H.264 with Opus over SRT, so a player gets 404",
	"AacOnly":  "DASH writes no MPD for a stream with no video, so a player gets 404",
	"H264Only": "DASH writes no MPD for a stream with no audio, so a player gets 404",
}

// srtWhepBugs are the known bugs of WHEP of the SRT source, with rtmp_to_rtc:
// the Opus SRS transcodes from AAC whose times don't start near 0, as
// ffmpeg's TS starts at 1.4s, has its first packets stamped more than a
// second before the rest.
var srtWhepBugs = func() map[string]string {
	bug := "SRS stamps the first Opus packets it transcodes from AAC with the AAC's time in ms taken as samples, so they come more than a second before the next ones in RTP time when the AAC doesn't start near 0"
	return map[string]string{"H264Aac": bug, "HevcAac": bug, "AacOnly": bug}
}()

// srtCommonCells adds the SRT source's cells of the Common server, which has
// srt_to_rtmp: RTMP, HTTP-FLV, HLS in TS, DASH and SRT; RTSP, which SRS
// doesn't convert SRT to; and WHEP, refused with rtmp_to_rtc off.
func srtCommonCells(t *testing.T) {
	common := func(c *check.C) *server.Server { return server.Common(c) }
	dash := dashPlayer
	dash.knownBugs = srtDashBugs
	for _, p := range []convertPlayer{rtmpPlayer, httpFlvConvert, hlsTsPlayer, dash, srtConvertPlayer} {
		srtCells(t, common, p)
	}

	// SRS bridges an SRT stream to RTMP and WebRTC, not to RTSP: DESCRIBE is
	// 404 while an RTMP player plays the stream.
	check.Case(t, "SrtToRtsp_NotConverted", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		path := "live/" + streamName(c)
		srtRealTime(c, srs, path, media.H264HighAACTS)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := flv.PlayRTMP(ctx, rtmpURL(srs, path), nil, playedRTMPUntil)
		c.Must(err, "an RTMP player of the stream")
		res, err := rtspDial(c, srs).Do(base.Describe, rtspURL(srs, path), nil, 300*time.Millisecond)
		if c.CheckNoError(err, "DESCRIBE") {
			c.CheckEqual(res.StatusCode, base.StatusNotFound, "DESCRIBE status")
		}
	})

	// With rtmp_to_rtc off, SRS refuses a WHEP player of an SRT stream as of
	// an RTMP one.
	check.Case(t, "SrtToWhep_Disabled", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		path := "live/" + name
		srtRealTime(c, srs, path, media.H264HighAACTS)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := flv.PlayRTMP(ctx, rtmpURL(srs, path), nil, playedRTMPUntil)
		c.Must(err, "an RTMP player of the stream")
		checkWhepDisabled(c, srs, name)
	})

	// SRS sends a sequence header when the parameter sets change, not at
	// each keyframe, which TS repeats them before: a player of 3 keyframes
	// gets one of each track.
	for _, codec := range srtCodecs[:2] {
		check.Case(t, "SrtToRtmp_HeaderOnce"+codec.name, check.Normal, func(c *check.C) {
			if codec.video == "hevc" {
				check.KnownBug(c, "SRS converts each HEVC keyframe over SRT with a new sequence header, since it takes every PPS as a change, so players get one per keyframe")
			}
			srs := server.Common(c)
			name := streamName(c)
			read := joinRTMP(c, srs, name)
			srtThrough(c, srs, "live/"+name, codec.file, 2200*time.Millisecond)
			r := read(report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 3, AudioFrames: 50})
			checkConverted(c, r, rtmpPlayer.want(codec), 100*time.Millisecond, srtSync)
		})
	}
}
