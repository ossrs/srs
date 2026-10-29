// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"net/http"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/dash"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// The conversion matrix: each publish protocol, each play protocol, and each
// codec of the test media the pair carries. A cell is one case, named
// <Source>To<Player>_<Codec>, such as RtmpToHlsTs_HevcAac: the player's
// stream report must match what the publisher sent, with the codecs the pair
// converts to, or SRS refuses the player, or the cell states what the
// player gets instead. cmd/integ/README.md holds the matrix.

// A convertCodec is one codec pair of the test media: its name in the case
// names, the file, and its video and audio codecs, "" for none.
type convertCodec struct {
	name, file   string
	video, audio string
}

// convertCodecs are the codecs an RTMP source publishes, one test file each.
var convertCodecs = []convertCodec{
	{"H264Aac", media.H264HighAAC, "h264", "aac"},
	{"HevcAac", media.HEVCAAC, "hevc", "aac"},
	{"H264Mp3", media.H264HighMP3, "h264", "mp3"},
	{"AacOnly", media.AACOnly, "", "aac"},
	{"H264Only", media.H264Only, "h264", ""},
}

// A convertSource is how a cell publishes: over RTMP or over RTMPS, which
// feed the same stream inside SRS, over SRT, a TS file that SRS converts to
// RTMP (see convert_srt_test.go), or over GB28181, PS that SRS converts to
// RTMP (see convert_gb_test.go).
type convertSource struct {
	name string
	tls  bool
	srt  bool
	gb   bool
}

var (
	rtmpSource  = convertSource{name: "Rtmp"}
	rtmpsSource = convertSource{name: "Rtmps", tls: true}
	srtSource   = convertSource{name: "Srt", srt: true}
	gbSource    = convertSource{name: "Gb", gb: true}
)

// url is the publish URL of a stream path, such as live/s.
func (s convertSource) url(srs *server.Server, path string) string {
	if s.tls {
		return rtmpsURL(srs, path)
	}
	return rtmpURL(srs, path)
}

// opts are the client options: the test certificate for RTMPS.
func (s convertSource) opts(c *check.C) *rtmp.Options {
	if s.tls {
		return tlsOpts(c)
	}
	return nil
}

// realTime publishes a file in real time until the case ends, the media
// before lead at once, like startPublisher; over SRT, srtLead at once, or
// nothing with no lead, for a player that joined before; over GB28181, at
// least gbLead at once.
func (s convertSource) realTime(c *check.C, srs *server.Server, path, file string, lead time.Duration) {
	c.Helper()
	if s.srt {
		if lead > 0 {
			lead = srtLead
		}
		srtRealTime(c, srs, path, file, lead)
		return
	}
	if s.gb {
		gbRealTime(c, srs, path, file, lead)
		return
	}
	startPublisherWith(c, s.url(srs, path), media.Path(c, file), lead, s.opts(c), rtmp.FMLE)
}

// publisher publishes a stream without media over RTMP, until the case ends.
func (s convertSource) publisher(c *check.C, srs *server.Server, path string) *rtmp.Client {
	c.Helper()
	if s.srt || s.gb {
		c.Fatalf("an %v source has no RTMP publisher", s.name)
	}
	pub := dialRTMP(c, s.url(srs, path), s.opts(c))
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := pub.Publish(ctx, rtmp.FMLE)
	c.Must(err, "publish")
	return pub
}

// through publishes a file at once, from its start through the tags at d,
// in a burst slot, and keeps the stream published until the case ends, like
// publishThrough; over SRT, through the TS packets at d; over GB28181,
// through the frames at d+gbLead.
func (s convertSource) through(c *check.C, srs *server.Server, path, file string, d time.Duration) {
	c.Helper()
	if s.srt {
		srtThrough(c, srs, path, file, d)
		return
	}
	if s.gb {
		gbThrough(c, srs, path, file, d)
		return
	}
	publishTagsOn(c, srs, s.publisher(c, srs, path), readTags(c, file, 0, d+time.Millisecond))
}

// A convertWant is what a player of a cell gets: the video and audio codecs,
// "" for none, and the audio's sample rate, when it is not the one of the
// RTMP test files' codec, such as AAC that SRS transcodes from Opus at 48 kHz.
type convertWant struct {
	video, audio string
	rate         int
}

// A convertPlayer is a play protocol of the matrix.
type convertPlayer struct {
	name string
	// video and audio map a published codec to the codec the player gets;
	// a codec the player doesn't carry is missing, and dropped.
	video, audio map[string]string
	// file is the test file a codec publishes for this player, when it
	// isn't the codec's own, such as H.264 without B-frames for WebRTC and
	// RTSP; nil keeps the codec's.
	file func(codec convertCodec) string
	// play publishes the file from src to a stream of its own and plays it
	// until the report can show w.
	play func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report
	// none checks what the player gets of a stream of which it carries no
	// track.
	none func(c *check.C, srs *server.Server, src convertSource, codec convertCodec)
	// gap and sync are the largest gap between frames of a track, and the
	// largest A/V sync difference, of what the player gets.
	gap, sync time.Duration
	// knownBugs are the cells of the player, by codec name, that a known
	// SRS bug fails.
	knownBugs map[string]string
}

// want is what the player gets of a codec.
func (p convertPlayer) want(codec convertCodec) convertWant {
	return convertWant{video: p.video[codec.video], audio: p.audio[codec.audio]}
}

// fileOf is the test file a codec publishes for the player.
func (p convertPlayer) fileOf(codec convertCodec) string {
	if p.file != nil {
		return p.file(codec)
	}
	return codec.file
}

// convertCells adds a case for each codec of the RTMP source, and one for
// the RTMPS source with the first codec the player carries: RTMPS is RTMP
// over TLS into the same stream, so the codec paths are those of RTMP.
func convertCells(t *testing.T, srsOf func(c *check.C) *server.Server, p convertPlayer) {
	for _, codec := range convertCodecs {
		convertCell(t, srsOf, rtmpSource, p, codec)
	}
	for _, codec := range convertCodecs {
		if w := p.want(codec); w.video != "" || w.audio != "" {
			convertCell(t, srsOf, rtmpsSource, p, codec)
			break
		}
	}
}

// convertCell adds the case of a cell.
func convertCell(t *testing.T, srsOf func(c *check.C) *server.Server, src convertSource, p convertPlayer, codec convertCodec) {
	check.Case(t, src.name+"To"+p.name+"_"+codec.name, check.Normal, func(c *check.C) {
		if reason, ok := p.knownBugs[codec.name]; ok {
			check.KnownBug(c, reason)
		}
		srs := srsOf(c)
		w := p.want(codec)
		if w.video == "" && w.audio == "" {
			p.none(c, srs, src, codec)
			return
		}
		checkConverted(c, p.play(c, srs, src, p.fileOf(codec), w), w, p.gap, p.sync)
	})
}

// convertUntil is when a real-time player has seen a stream of w play: the
// sequence headers its codecs carry, a keyframe and 5 frames of each track.
func convertUntil(w convertWant) report.Until {
	u := report.Until{AudioHeader: w.audio == "aac"}
	if w.video != "" {
		u.VideoHeader, u.Keyframes, u.VideoFrames = true, 1, 5
	}
	if w.audio != "" {
		u.AudioFrames = 5
	}
	return u
}

// segmentsOf is when a segment player has seen a stream of w play: the
// sequence headers its codecs carry, and d of media.
func segmentsOf(w convertWant, d time.Duration) report.Until {
	return report.Until{VideoHeader: w.video != "", AudioHeader: w.audio == "aac", Duration: d}
}

// checkConverted checks a player's report against what the publisher sent:
// the codecs w, the source's 768x320 from its first keyframe with one
// sequence header, the audio at the rate of w or of its codec, stereo, AAC with
// one sequence header, and timestamps that only go forward, without a gap
// over gap, in sync within sync.
func checkConverted(c *check.C, r *report.Report, w convertWant, gap, sync time.Duration) {
	c.Helper()
	if w.video == "" {
		c.Check(r.Video == nil, "video %v, want none", r.Video)
	} else if c.Check(r.Video != nil, "no video: %v", r) {
		checkVideo(c, r, w.video)
		c.Check(r.Video.Keyframes >= 1, "no keyframe: %v", r)
		c.CheckEqual(r.Video.LeadingFrames, 0, "video frames before the first keyframe")
	}
	if w.audio == "" {
		c.Check(r.Audio == nil, "audio %v, want none", r.Audio)
	} else if c.Check(r.Audio != nil, "no audio: %v", r) {
		a := r.Audio
		c.CheckEqual(a.Codec, w.audio, "audio codec")
		rate, headers := 44100, 0
		switch w.audio {
		case "opus":
			rate = 48000
		case "aac":
			headers = 1
		}
		if w.rate != 0 {
			rate = w.rate
		}
		c.Check(a.SampleRate == rate && a.Channels == 2, "audio %vHz %vch, want %vHz 2ch", a.SampleRate, a.Channels, rate)
		c.CheckEqual(a.SequenceHeaders, headers, "audio sequence headers")
	}
	c.CheckNoError(r.CheckTimestamps(gap), "timestamps")
	if w.video != "" && w.audio != "" {
		c.Check(r.AVSync <= sync, "A/V sync %v, want at most %v", r.AVSync, sync)
	}
}

// same maps each codec to itself.
func same(codecs ...string) map[string]string {
	m := map[string]string{}
	for _, codec := range codecs {
		m[codec] = codec
	}
	return m
}

// convertLead is how much of a real-time stream is sent before a player
// joins: the player gets it from the GOP cache, then the rest in real time.
const convertLead = 200 * time.Millisecond

var (
	// rtmpPlayer plays over RTMP from the GOP cache.
	rtmpPlayer = convertPlayer{
		name: "Rtmp", video: same("h264", "hevc"), audio: same("aac", "mp3"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			src.realTime(c, srs, path, file, convertLead)
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			r, err := flv.PlayRTMP(ctx, rtmpURL(srs, path), nil, convertUntil(w))
			c.Must(err, "play")
			return r
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}

	// httpFlvConvert plays HTTP-FLV from the GOP cache.
	httpFlvConvert = convertPlayer{
		name: "HttpFlv", video: same("h264", "hevc"), audio: same("aac", "mp3"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			src.realTime(c, srs, path, file, convertLead)
			return httpFlvPlay(c, srs, path+".flv", nil, convertUntil(w))
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}

	// hlsTsPlayer plays the Common server's HLS, 10s TS segments, of a
	// stream published at once: one segment, or with no video, one of 21s
	// (hls_aof_ratio 2.1).
	hlsTsPlayer = convertPlayer{
		name: "HlsTs", video: same("h264", "hevc"), audio: same("aac", "mp3"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			d := 10 * time.Second
			if w.video == "" {
				d = 23 * time.Second
			}
			src.through(c, srs, path, file, d)
			return playHLS(c, httpClient, flvURL(srs, path+".m3u8"), segmentsOf(w, 8*time.Second))
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}

	// dashPlayer plays the Common server's DASH, 11s segments on the 1s
	// GOP (commonDashSegment), of a stream published at once: with no video,
	// the 23s that make the first HLS segment (hls_aof_ratio 2.1).
	dashPlayer = convertPlayer{
		name: "Dash", video: same("h264", "hevc"), audio: same("aac", "mp3"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			d := commonDashSegment + time.Second
			if w.video == "" {
				d = 23 * time.Second
			}
			src.through(c, srs, path, file, d)
			// SRS writes the HLS and the DASH of a stream as it reads it, so
			// once the first HLS segment is there, the DASH one is there too
			// or a moment later: a DASH player that waits longer waits for
			// files that never come.
			waitPlaylist(c, httpClient, flvURL(srs, path+".m3u8"), hasSegment(0))
			return playDashCell(c, flvURL(srs, path+".mpd"), segmentsOf(w, 8*time.Second))
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}

	// rtspPlayer plays RTSP over TCP: the publisher holds the stream after
	// its sequence headers, which make the RTSP tracks, until the player is
	// in, since SRS keeps no GOP for RTSP (see rtspPublisher).
	rtspPlayer = convertPlayer{
		name: "Rtsp", video: same("h264", "hevc"), audio: same("aac"),
		file: baselineH264,
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			start := rtspPublisherOn(c, src.publisher(c, srs, path), file)
			p := rtspPlay(c, srs, path, nil)
			start()
			return rtspUntil(c, p, convertUntil(w))
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}
)

// baselineH264 is H.264 without B-frames for H.264 with AAC, which WebRTC
// and RTSP players take as it is; the other files are the codec's own.
func baselineH264(codec convertCodec) string {
	if codec.file == media.H264HighAAC {
		return media.H264BaselineAAC
	}
	return codec.file
}

// TestConvert_Common converts RTMP and RTMPS streams on the Common server to
// its players: RTMP, HTTP-FLV, HLS in TS, DASH and RTSP. SRS converts no
// RTMP stream to SRT, and to WebRTC only with rtmp_to_rtc, off on Common.
// WebRTC streams play over WHEP, and not over RTMP: rtc_to_rtmp is off too.
// SRT streams play, with srt_to_rtmp, as RTMP streams do, and over SRT.
// GB28181 streams, which SRS publishes to its own RTMP, play as RTMP streams
// do.
func TestConvert_Common(t *testing.T) {
	t.Parallel()
	common := func(c *check.C) *server.Server { return server.Common(c) }
	for _, p := range []convertPlayer{rtmpPlayer, httpFlvConvert, hlsTsPlayer, dashPlayer, rtspPlayer} {
		convertCells(t, common, p)
	}
	whipCommonCells(t)
	srtCommonCells(t)
	gbCommonCells(t)

	for _, src := range []convertSource{rtmpSource, rtmpsSource} {
		// SRS has no RTMP to SRT conversion: an SRT player of an RTMP
		// stream is accepted and gets nothing, while an RTMP player gets
		// the stream, the lead from the GOP cache, and the SRT player gets
		// nothing of the next 100ms either.
		check.Case(t, src.name+"ToSrt_NotConverted", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			name := streamName(c)
			path := "live/" + name
			player := srtPlayer(c, srs, srt.Options{StreamID: srtID(path, "request")}, name)
			src.realTime(c, srs, path, media.H264HighAAC, convertLead)
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			_, err := flv.PlayRTMP(ctx, rtmpURL(srs, path), nil, playedUntil)
			c.Must(err, "an RTMP player of the stream")
			ctx, cancel = context.WithTimeout(context.Background(), 100*time.Millisecond)
			defer cancel()
			r, _ := player.Analyze(ctx, playedUntil)
			c.Check(r.Video == nil && r.Audio == nil, "the SRT player got %v, want nothing", r)
		})

		// With rtmp_to_rtc off, SRS refuses a WHEP player of an RTMP
		// stream: 500, since the stream is published but not converted.
		check.Case(t, src.name+"ToWhep_Disabled", check.Normal, func(c *check.C) {
			srs := server.Common(c)
			name := streamName(c)
			src.realTime(c, srs, "live/"+name, media.H264BaselineAAC, 0)
			checkWhepDisabled(c, srs, name)
		})
	}
}

// checkWhepDisabled checks that SRS refuses a WHEP player of the published
// stream live/name, with rtmp_to_rtc off: 500 RtcDisabled.
func checkWhepDisabled(c *check.C, srs *server.Server, name string) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := rtc.Play(ctx, whepURL(srs, "live", name), &rtc.Options{Client: httpClient})
	var se *rtc.StatusError
	if c.Check(errors.As(err, &se), "WHEP play: %v, want refused", err) {
		c.CheckEqual(se.Code, http.StatusInternalServerError, "status")
		c.Check(strings.Contains(se.Body, "RtcDisabled"), "body %q, want RtcDisabled", se.Body)
	}
}

// sharedTsMountConfig is the config of the shared TsMount server: the TS
// remux mount with hooks, the HTTPS server, RTMPS, SRT and the GB28181
// caster, and WebRTC with rtmp_to_rtc, which converts each RTMP stream to
// WebRTC, with its AAC transcoded to Opus, and rtc_to_rtmp, which converts
// each WebRTC stream to RTMP, with its Opus transcoded to AAC. Common keeps both off, since they
// would transcode the audio of every stream of every case; the TsMount
// groups publish few streams, in real time, and convert none but the
// Convert cells.
func sharedTsMountConfig() server.Config {
	cfg := remuxGroup("ts", server.Remux{})
	cfg.Listeners = append(cfg.Listeners, server.HTTPSServer, server.RTMPS)
	cfg = server.WithGB(server.WithSRT(cfg, server.SRTOptions{}), server.GBOptions{})
	return server.WithRTC(cfg, server.RTC{RTMPToRTC: true, RTCToRTMP: true})
}

// whepPlayer plays WebRTC by WHEP from the TsMount server, with rtmp_to_rtc.
// SRS keeps no GOP for WebRTC, so the player connects before the publisher:
// SRS subscribes it once its DTLS handshake is done, which is before the
// publisher's RTMP handshake, and it gets the stream from its first
// keyframe. Before the stream is published, SRS makes the tracks a WHEP
// player asks for by vcodec, H.264 by default. SRS drops B-frames
// (keep_bframe off), so a player of H.264 High waits for fewer frames. SRS
// transcodes AAC and MP3 to Opus.
var whepPlayer = convertPlayer{
	name: "Whep", video: same("h264", "hevc"), audio: map[string]string{"aac": "opus", "mp3": "opus"},
	file: baselineH264,
	play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
		name := streamName(c)
		url := whepURL(srs, "live", name)
		var opts *rtc.Options
		if w.video == "hevc" {
			url += "&vcodec=hevc"
			opts = &rtc.Options{Video: []string{"H265"}}
		}
		p := rtcPlayer(c, url, opts)
		src.realTime(c, srs, "live/"+name, file, 0)
		u := convertUntil(w)
		if u.VideoFrames > 0 {
			u.VideoFrames = 2
		}
		return waitPlayed(c, p, u)
	},
	gap: 200 * time.Millisecond, sync: 200 * time.Millisecond,
}

// TestConvert_TsMount converts RTMP, RTMPS, SRT and GB28181 streams to
// HTTP-TS, and to WebRTC with rtmp_to_rtc, and WebRTC streams to HTTP-TS with
// rtc_to_rtmp, on the shared TsMount server.
func TestConvert_TsMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "TsMount", sharedTsMountConfig())
	srsOf := func(*check.C) *server.Server { return srs }
	ts := remuxConvert("HttpTs", "ts", same("h264", "hevc"), same("aac", "mp3"))
	convertCells(t, srsOf, ts)
	convertCells(t, srsOf, whepPlayer)
	whipCells(t, srsOf, whipTsPlayer)
	srtCells(t, srsOf, ts)
	srtCells(t, srsOf, whepPlayer)
	gbCells(t, srsOf, ts)
	gbCells(t, srsOf, whepPlayer)
	whepCodecCells(t, srsOf)
}

// sharedMp3MountConfig and sharedAacMountConfig are the configs of the
// shared audio mount servers: the remux mount with hooks, RTMPS, SRT, the
// GB28181 caster, and WebRTC with rtc_to_rtmp, so the WebRTC streams of the
// Convert cells play there.
func sharedMp3MountConfig() server.Config { return audioMountConfig("mp3") }
func sharedAacMountConfig() server.Config { return audioMountConfig("aac") }

func audioMountConfig(ext string) server.Config {
	cfg := remuxGroup(ext, server.Remux{})
	cfg.Listeners = append(cfg.Listeners, server.RTMPS)
	cfg = server.WithGB(server.WithSRT(cfg, server.SRTOptions{}), server.GBOptions{})
	return server.WithRTC(cfg, server.RTC{RTCToRTMP: true})
}

// TestConvert_Mp3Mount converts RTMP, RTMPS, WebRTC, SRT and GB28181 streams
// to HTTP MP3, on the shared Mp3Mount server: the MP3 of a stream, nothing of
// AAC.
func TestConvert_Mp3Mount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Mp3Mount", sharedMp3MountConfig())
	srsOf := func(*check.C) *server.Server { return srs }
	mp3 := remuxConvert("HttpMp3", "mp3", nil, same("mp3"))
	convertCells(t, srsOf, mp3)
	whipCells(t, srsOf, whipMp3Player)
	srtCells(t, srsOf, mp3)
	gbCells(t, srsOf, mp3)
}

// TestConvert_AacMount converts RTMP, RTMPS, WebRTC, SRT and GB28181 streams
// to HTTP AAC, on the shared AacMount server: the AAC of a stream, nothing of
// MP3.
func TestConvert_AacMount(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "AacMount", sharedAacMountConfig())
	srsOf := func(*check.C) *server.Server { return srs }
	aac := remuxConvert("HttpAac", "aac", nil, same("aac"))
	convertCells(t, srsOf, aac)
	whipCells(t, srsOf, whipAacPlayer)
	srtCells(t, srsOf, aac)
	gbCells(t, srsOf, aac)
}

// remuxConvert plays an HTTP remux mount ext, joining a real-time stream
// convertLead in: TS from the GOP cache, and the audio mounts from the next
// frame, since they have no cache. A stream whose audio an audio mount
// can't carry ends without a frame; one with no audio sends nothing while
// its video plays, and stays open.
func remuxConvert(name, ext string, video, audio map[string]string) convertPlayer {
	return convertPlayer{
		name: name, video: video, audio: audio,
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			src.realTime(c, srs, path, file, convertLead)
			p := remuxPlayerOf(c, srs, path+"."+ext, nil)
			c.Must(p.readUntil(convertUntil(w), time.Second), "play")
			return p.Report
		},
		none: func(c *check.C, srs *server.Server, src convertSource, codec convertCodec) {
			path := "live/" + streamName(c)
			src.realTime(c, srs, path, codec.file, convertLead)
			p := remuxPlayerOf(c, srs, path+"."+ext, nil)
			if codec.audio != "" {
				c.CheckNoError(p.untilEnds(time.Second), "the player")
			} else {
				// 5 video frames come in real time while the player waits.
				c.CheckNoError(p.readNothing(200*time.Millisecond), "the player")
			}
			c.Check(p.Report.Video == nil && p.Report.Audio == nil, "the player got %v, want nothing", p.Report)
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}
}

// TestConvert_Fmp4 converts RTMP, RTMPS, WebRTC, SRT and GB28181 streams to
// HLS in fmp4, 3s segments (fmp4Segment), on a server of the Fmp4 config of
// TestHls_Fmp4 but its own: on one server the cells' bursts beside
// TestHls_Fmp4's kept it busy for whole segment requests, and the cells
// whose players wait for what SRS converts from SRT and GB28181 missed their
// time. It has no hls_entry_prefix: the players read the HLS from the
// server, not through a CDN.
func TestConvert_Fmp4(t *testing.T) {
	t.Parallel()
	cfg := sharedFmp4()
	delete(cfg.Env, "SRS_VHOST_HLS_HLS_ENTRY_PREFIX")
	srs := server.Group(t, cfg)
	srsOf := func(*check.C) *server.Server { return srs }
	whipCells(t, srsOf, whipFmp4Player)
	fmp4 := convertPlayer{
		name: "HlsFmp4", video: same("h264", "hevc"), audio: same("aac", "mp3"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			d := 2 * fmp4Segment
			if w.video == "" {
				// With no video, a segment is cut at hls_aof_ratio times
				// the fragment, 5s.
				d = 12 * time.Second
			}
			src.through(c, srs, path, file, d)
			url, u := flvURL(srs, path+".m3u8"), segmentsOf(w, 5*time.Second)
			if src.srt || src.gb {
				return playHLSWithin(c, url, u, fmp4PlayWait)
			}
			return playHLS(c, httpClient, url, u)
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}
	convertCells(t, srsOf, fmp4)
	srtCells(t, srsOf, fmp4)
	gbCells(t, srsOf, gbFmp4Player)
}

// playDashCell plays the DASH of a conversion cell whose first HLS segment is
// there. The MPD comes at once or a moment later, so a cell whose MPD SRS
// never writes fails in 100ms. Once the MPD is there, the play may take most
// of the case's limit: the shared server answers a segment late while it
// takes the bursts of other segment cases. Both waits are scaled by the
// limit scale.
func playDashCell(c *check.C, mpd string, u report.Until) *report.Report {
	c.Helper()
	deadline := time.Now().Add(check.Scaled(100 * time.Millisecond))
	for {
		res, _ := httpGet(c, httpClient, mpd)
		if res.StatusCode == http.StatusOK {
			break
		}
		if res.StatusCode != http.StatusNotFound || !time.Now().Before(deadline) {
			c.Fatalf("MPD %v: status %v", mpd, res.Status)
		}
		time.Sleep(10 * time.Millisecond)
	}
	ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(400*time.Millisecond))
	defer cancel()
	r, err := dash.PlayHTTP(ctx, httpClient, mpd, u)
	c.Must(err, "play")
	return r
}

// fmp4PlayWait is how long a player of the HLS in fmp4 of an SRT or GB28181
// stream, which SRS converts as it reads it, waits, before playHLSWithin
// scales it. It may take most of the case's limit: the server, busy with the
// bursts of the other fmp4 cells, may take 300ms to connect a GB session's
// RTMP output, or to answer a segment.
const fmp4PlayWait = 400 * time.Millisecond
