// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/bluenviron/gortsplib/v5/pkg/base"
	"github.com/pion/rtcp"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/hls"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// The WebRTC source of the conversion matrix: a WHIP publisher, sending the
// Sender Reports a browser sends, to each player. SRS converts a WebRTC
// stream to RTMP only with rtc_to_rtmp, which transcodes its Opus to AAC at
// 48 kHz and maps the tracks to one clock by their Sender Reports, so it
// converts nothing before two reports of each track. Common keeps it off;
// the RtcToRtmp server and the shared remux and fmp4 servers turn it on. A
// cell is named WhipTo<Player>_<Codec>, such as WhipToHlsTs_HevcOpus.

// A whipCodec is the codecs a WHIP source publishes: its name in the case
// names, the test file, and its video and audio codecs, "" for none.
type whipCodec struct {
	name, file   string
	video, audio string
}

// whipCodecs are the codecs a WHIP source publishes, from the WebRTC test
// files: H.264 Baseline without B-frames, or HEVC, with Opus.
var whipCodecs = []whipCodec{
	{"H264Opus", media.H264BaselineOpus, "h264", "opus"},
	{"HevcOpus", media.HEVCOpus, "hevc", "opus"},
	{"OpusOnly", media.H264BaselineOpus, "", "opus"},
	{"H264Only", media.H264BaselineOpus, "h264", ""},
}

var whipH264Opus = whipCodecs[0]

// whipMedia holds each WebRTC test file's samples without their SEI NALs,
// made once. TestConvert_RtcToRtmp's SEI cases send the SEI NALs, which come
// before the IDR of a keyframe.
var whipMedia sync.Map

type whipFile struct {
	once    sync.Once
	samples []rtc.Sample
}

// samples are the samples of the codec's tracks, without SEI.
func (w whipCodec) samples(c *check.C) []rtc.Sample {
	c.Helper()
	all := rtcFileSamples(c, w.file)
	v, _ := whipMedia.LoadOrStore(w.file, &whipFile{})
	f := v.(*whipFile)
	f.once.Do(func() { f.samples = rtc.WithoutSEI(all) })
	return whipTracks(f.samples, w)
}

// whipTracks are the samples of the codec's tracks.
func whipTracks(all []rtc.Sample, w whipCodec) []rtc.Sample {
	var out []rtc.Sample
	for _, s := range all {
		if (s.Video && w.video != "") || (!s.Video && w.audio != "") {
			out = append(out, s)
		}
	}
	return out
}

// options offer the codec's tracks, and send the publisher's own Sender
// Reports, like a browser's but from the first packet.
func (w whipCodec) options() *rtc.Options {
	o := &rtc.Options{SenderReports: true}
	switch w.video {
	case "":
		o.Video = []string{}
	case "hevc":
		o.Video = []string{"H265"}
	}
	if w.audio == "" {
		o.Audio = []string{}
	}
	return o
}

// url is the WHIP URL of a stream in live, which asks for HEVC by vcodec.
func (w whipCodec) url(srs *server.Server, name string) string {
	u := whipURL(srs, "live", name)
	if w.video == "hevc" {
		u += "&vcodec=hevc"
	}
	return u
}

// whipPublish connects a WHIP publisher of the codec to the stream live/name,
// closed when the case ends.
func whipPublish(c *check.C, srs *server.Server, name string, w whipCodec) *rtc.Publisher {
	c.Helper()
	return rtcPublisher(c, w.url(srs, name), w.options())
}

// whipSend sends samples from their start, those before lead at once, then
// the rest in real time, until the case ends.
func whipSend(c *check.C, pub *rtc.Publisher, samples []rtc.Sample, lead time.Duration) {
	n := 0
	for n < len(samples) && samples[n].DTS < lead {
		n++
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		defer close(done)
		if pub.Send(ctx, samples[:n], nil) == nil {
			pub.Send(ctx, samples[n:], &rtmp.Pacer{})
		}
	}()
	c.Cleanup(func() {
		cancel()
		<-done
	})
}

// whipThrough sends samples from their start through those before d at once,
// in a burst slot of the server, and keeps the stream published until the
// case ends. Segments follow the media's times, which SRS takes from the
// Sender Reports, not from when the packets come.
func whipThrough(c *check.C, srs *server.Server, pub *rtc.Publisher, samples []rtc.Sample, d time.Duration) {
	c.Helper()
	free := srs.Burst(c)
	defer free()
	whipSendThrough(c, pub, samples, d)
}

// whipSendThrough sends samples from their start through those before d at
// once. It returns once the packets are sent, before SRS has read them: they
// wait in the socket of SRS's UDP listener.
func whipSendThrough(c *check.C, pub *rtc.Publisher, samples []rtc.Sample, d time.Duration) {
	c.Helper()
	n := 0
	for n < len(samples) && samples[n].DTS < d {
		n++
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	c.Must(pub.Send(ctx, samples[:n], nil), "publish the media")
}

// whipRate is the sample rate of the AAC that SRS transcodes from Opus.
const whipRate = 48000

// A whipPlayer is a play protocol of the WebRTC source's row of the matrix.
type whipPlayer struct {
	name string
	// video and audio map a published codec to the codec the player gets;
	// a codec the player doesn't carry is missing, and dropped.
	video, audio map[string]string
	// play publishes the codec to a stream of its own and plays it until
	// the report can show w.
	play func(c *check.C, srs *server.Server, codec whipCodec, w convertWant) *report.Report
	// none checks what the player gets of a stream of which it carries no
	// track.
	none func(c *check.C, srs *server.Server, codec whipCodec)
	// gap and sync are the largest gap between frames of a track, and the
	// largest A/V sync difference, of what the player gets.
	gap, sync time.Duration
	// knownBugs are the cells of the player, by codec name, that a known
	// SRS bug fails.
	knownBugs map[string]string
}

// want is what the player gets of a codec: AAC from Opus is at 48 kHz.
func (p whipPlayer) want(codec whipCodec) convertWant {
	w := convertWant{video: p.video[codec.video], audio: p.audio[codec.audio]}
	if codec.audio == "opus" && w.audio == "aac" {
		w.rate = whipRate
	}
	return w
}

// whipCells adds a case for each codec of the WebRTC source.
func whipCells(t *testing.T, srsOf func(c *check.C) *server.Server, p whipPlayer) {
	for _, codec := range whipCodecs {
		check.Case(t, "WhipTo"+p.name+"_"+codec.name, check.Normal, func(c *check.C) {
			if reason, ok := p.knownBugs[codec.name]; ok {
				check.KnownBug(c, reason)
			}
			srs := srsOf(c)
			w := p.want(codec)
			if w.video == "" && w.audio == "" {
				p.none(c, srs, codec)
				return
			}
			checkConverted(c, p.play(c, srs, codec, w), w, p.gap, p.sync)
		})
	}
}

// whipLead is how much of a real-time stream is sent at once, after the
// player has joined: SRS keeps no GOP of a stream it converts from WebRTC,
// so a player joins before the publisher.
const whipLead = 200 * time.Millisecond

// whipLive is a player that joins a stream before its publisher and plays it
// in real time: join connects it to live/name, and returns the function that
// reads it until the report meets u.
func whipLive(name string, video, audio map[string]string, join func(c *check.C, srs *server.Server, name string) func(u report.Until) *report.Report) whipPlayer {
	return whipPlayer{
		name: name, video: video, audio: audio,
		play: func(c *check.C, srs *server.Server, codec whipCodec, w convertWant) *report.Report {
			stream := streamName(c)
			read := join(c, srs, stream)
			whipSend(c, whipPublish(c, srs, stream, codec), codec.samples(c), whipLead)
			return read(convertUntil(w))
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}
}

// joinRTMP plays live/name over RTMP, and reads it for at most 1s.
func joinRTMP(c *check.C, srs *server.Server, name string) func(u report.Until) *report.Report {
	c.Helper()
	player := dialRTMP(c, rtmpURL(srs, "live/"+name), nil)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := player.Play(ctx)
	c.Must(err, "play")
	return func(u report.Until) *report.Report {
		c.Helper()
		player.SetDeadline(time.Now().Add(time.Second))
		defer player.SetDeadline(time.Time{})
		r, err := flv.Analyze(player, u)
		c.Must(err, "play")
		return r
	}
}

// joinHTTPFlv plays live/name over HTTP-FLV, and reads it for at most 1s.
func joinHTTPFlv(c *check.C, srs *server.Server, name string) func(u report.Until) *report.Report {
	c.Helper()
	p := httpFlvPlayer(c, srs, "live/"+name+".flv", nil)
	return func(u report.Until) *report.Report { return readHTTPFlv(c, p, u) }
}

// joinRemux plays live/name from an HTTP remux mount ext, and reads it for at
// most 1s.
func joinRemux(ext string) func(c *check.C, srs *server.Server, name string) func(u report.Until) *report.Report {
	return func(c *check.C, srs *server.Server, name string) func(u report.Until) *report.Report {
		c.Helper()
		p := remuxPlayerOf(c, srs, "live/"+name+"."+ext, nil)
		return func(u report.Until) *report.Report {
			c.Helper()
			c.Must(p.readUntil(u, time.Second), "play")
			return p.Report
		}
	}
}

// whipRemux is the player of an HTTP remux mount ext. Of a stream with no
// track it carries, an audio mount ends the stream at the first audio frame
// it can't carry, and stays open, sending nothing, while video plays.
func whipRemux(name, ext string, video, audio map[string]string) whipPlayer {
	p := whipLive(name, video, audio, joinRemux(ext))
	p.none = func(c *check.C, srs *server.Server, codec whipCodec) {
		stream := streamName(c)
		player := remuxPlayerOf(c, srs, "live/"+stream+"."+ext, nil)
		whipSend(c, whipPublish(c, srs, stream, codec), codec.samples(c), whipLead)
		if codec.audio != "" {
			c.CheckNoError(player.untilEnds(time.Second), "the player")
		} else {
			// The lead's 5 video frames come at once while it waits.
			c.CheckNoError(player.readNothing(200*time.Millisecond), "the player")
		}
		c.Check(player.Report.Video == nil && player.Report.Audio == nil, "the player got %v, want nothing", player.Report)
	}
	return p
}

// whipSegments are the players of a stream's segments, HLS or DASH: the
// publisher sends the media at once through d, or through audioOnly for a
// stream with no video, which SRS cuts by hls_aof_ratio, and play reads the
// segments until the report has the sequence headers and least of media.
//
// The packets of a WebRTC burst are sent before SRS reads them, and SRS reads
// them after the bursts of the cases before, so the case keeps its burst slot
// until the HLS playlist lists least of media: SRS has then read the burst
// and written its segments, and the next burst starts.
func whipSegments(name string, video, audio map[string]string, d, audioOnly, least time.Duration,
	play func(c *check.C, srs *server.Server, stream string, u report.Until) *report.Report) whipPlayer {
	return whipPlayer{
		name: name, video: video, audio: audio,
		play: func(c *check.C, srs *server.Server, codec whipCodec, w convertWant) *report.Report {
			stream := streamName(c)
			through := d
			if codec.video == "" {
				through = audioOnly
			}
			pub := whipPublish(c, srs, stream, codec)
			free := srs.Burst(c)
			defer free()
			whipSendThrough(c, pub, codec.samples(c), through)
			waitPlaylist(c, httpClient, flvURL(srs, "live/"+stream+".m3u8"), listsMedia(least))
			free()
			return play(c, srs, stream, segmentsOf(w, least))
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}
}

// listsMedia is a playlist whose segments last d or more in all.
func listsMedia(d time.Duration) func(*hls.Playlist) bool {
	return func(pl *hls.Playlist) bool {
		var all time.Duration
		for _, s := range pl.Segments {
			all += s.Duration
		}
		return all >= d
	}
}

// sharedRtcToRtmp is the config of the shared RtcToRtmp server: WebRTC with
// rtc_to_rtmp, and the players of what it converts that Common has: RTMP,
// HTTP-FLV, HLS in TS and DASH, both with 1s fragments so a WebRTC stream
// sent at once makes segments from a few seconds, RTSP and SRT, and every
// hook on the mock. Common keeps rtc_to_rtmp off: it transcodes each WebRTC
// stream's audio, and refuses a WebRTC publish whose audio it can't
// transcode, such as G.711.
//
// HTTP-FLV doesn't guess the header's tracks (guess_has_av off): the AAC SRS
// transcodes from Opus comes a few frames after the video, so a player that
// joins before the publisher, as it must since SRS keeps no GOP of such a
// stream, may get its first messages without audio, and a header without
// audio, whose audio SRS then drops (seen 1 time in 12).
func sharedRtcToRtmp() server.Config {
	cfg := server.HLSConfig(server.HLS{Fragment: time.Second})
	cfg.Env["SRS_VHOST_HTTP_REMUX_ENABLED"] = "on"
	cfg.Env["SRS_VHOST_HTTP_REMUX_MOUNT"] = "[vhost]/[app]/[stream].flv"
	cfg.Env["SRS_VHOST_HTTP_REMUX_GUESS_HAS_AV"] = "off"
	cfg.Hooks = true
	cfg = server.WithDASH(cfg, server.DASH{Fragment: time.Second})
	cfg = server.WithRTC(cfg, server.RTC{RTCToRTMP: true})
	cfg = server.WithSRT(cfg, server.SRTOptions{})
	return server.WithRTSP(cfg, server.RTSPOptions{})
}

// rtcToRtmpDashSegment is how long the first DASH segment of the RtcToRtmp
// server lasts: SRS cuts it at the first keyframe where it already lasts
// the 1s fragment, measured to its last frame, so at 2s on the 1s GOP (see
// commonDashSegment).
const rtcToRtmpDashSegment = 2 * time.Second

var (
	whipRtmpPlayer    = whipLive("Rtmp", same("h264", "hevc"), map[string]string{"opus": "aac"}, joinRTMP)
	whipHTTPFlvPlayer = whipLive("HttpFlv", same("h264", "hevc"), map[string]string{"opus": "aac"}, joinHTTPFlv)

	// whipHLSTsPlayer plays the RtcToRtmp server's HLS: 1s TS segments on
	// the 1s GOP, or with no video, one of 2.1s (hls_aof_ratio 2.1).
	whipHLSTsPlayer = whipSegments("HlsTs", same("h264", "hevc"), map[string]string{"opus": "aac"},
		2200*time.Millisecond, 2500*time.Millisecond, 1500*time.Millisecond, playWhipHLS)

	// whipDashPlayer plays the RtcToRtmp server's DASH, the first 2s
	// segment, once the HLS segment that ends with it is there (see
	// dashPlayer): the HLS of 1.5s of media ends at 2s, or with no video,
	// at 2.1s.
	whipDashPlayer = whipSegments("Dash", same("h264", "hevc"), map[string]string{"opus": "aac"},
		rtcToRtmpDashSegment+1200*time.Millisecond, 2500*time.Millisecond, 1500*time.Millisecond,
		func(c *check.C, srs *server.Server, stream string, u report.Until) *report.Report {
			return playDashCell(c, flvURL(srs, "live/"+stream+".mpd"), u)
		})
)

// TestConvert_RtcToRtmp converts WebRTC streams to RTMP, HTTP-FLV, HLS in TS
// and DASH, with rtc_to_rtmp on the shared RtcToRtmp server, and shows what
// the conversion takes from the Sender Reports, the sequence headers it
// sends once, and that SRT and RTSP players get nothing.
func TestConvert_RtcToRtmp(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RtcToRtmp", sharedRtcToRtmp())
	srsOf := func(*check.C) *server.Server { return srs }
	for _, p := range []whipPlayer{whipRtmpPlayer, whipHTTPFlvPlayer, whipHLSTsPlayer, whipDashPlayer} {
		whipCells(t, srsOf, p)
	}
	whipRelayOnlyCells(t, srs)

	// WebRTC sends the parameter sets before each keyframe; SRS sends a
	// sequence header only when they change, so a player of 3 keyframes gets
	// one, and one AAC sequence header.
	for _, codec := range whipCodecs[:2] {
		check.Case(t, "WhipToRtmp_HeaderOnce"+codec.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			read := joinRTMP(c, srs, stream)
			whipThrough(c, srs, whipPublish(c, srs, stream, codec), codec.samples(c), 2200*time.Millisecond)
			r := read(report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 3, AudioFrames: 50})
			checkConverted(c, r, whipRtmpPlayer.want(codec), 100*time.Millisecond, 100*time.Millisecond)
		})
	}

	// SRS maps each track's RTP times to one clock by its Sender Reports, so
	// audio whose reports say it is 150ms later than the video sent with it
	// plays 150ms later, more than the 40ms AAC's transcoding adds; jitter
	// correction keeps an offset under 250ms.
	check.Case(t, "WhipToRtmp_SyncFromReports", check.Normal, func(c *check.C) {
		const shift = 150 * time.Millisecond
		stream := streamName(c)
		read := joinRTMP(c, srs, stream)
		opts := whipH264Opus.options()
		opts.AudioReportShift = shift
		pub := rtcPublisher(c, whipH264Opus.url(srs, stream), opts)
		whipSend(c, pub, whipH264Opus.samples(c), whipLead)
		r := read(report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5, AudioFrames: 10})
		if c.Check(r.Video != nil && r.Audio != nil, "no video or audio: %v", r) {
			d := r.Audio.FirstDTS - r.Video.FirstDTS
			c.Check(d >= shift && d <= shift+80*time.Millisecond, "the first audio %v after the first video, want %v to %v", d, shift, shift+80*time.Millisecond)
		}
	})

	// A publisher that sends no Sender Report before its media, as pion
	// doesn't for its first second, gets nothing converted, while a WebRTC
	// player of the stream plays it: SRS drops what it can't map yet.
	check.Case(t, "WhipToRtmp_NothingBeforeReports", check.Normal, func(c *check.C) {
		stream := streamName(c)
		player := dialRTMP(c, rtmpURL(srs, "live/"+stream), nil)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Play(ctx)
		c.Must(err, "play")
		whep := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		whipSend(c, pub, whipH264Opus.samples(c), whipLead)
		waitPlayed(c, whep, rtcPlayedUntil)
		checkNothingRTMP(c, player, 100*time.Millisecond)
	})

	// A keyframe with an SEI before its IDR, as x264 writes in its first
	// frame and x265 in each keyframe, goes to RTMP as a keyframe: the RTMP
	// player's first frame is a keyframe.
	for _, codec := range whipCodecs[:2] {
		check.Case(t, "WhipToRtmp_SeiKeyframe"+codec.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			read := joinRTMP(c, srs, stream)
			samples := whipTracks(rtcFileSamples(c, codec.file), codec)
			whipSend(c, whipPublish(c, srs, stream, codec), samples, whipLead)
			r := read(report.Until{VideoHeader: true, VideoFrames: 5})
			if c.Check(r.Video != nil, "no video: %v", r) {
				c.Check(r.Video.Keyframes >= 1 && r.Video.LeadingFrames == 0, "%v keyframes, %v frames before the first, want the first frame a keyframe", r.Video.Keyframes, r.Video.LeadingFrames)
			}
		})
	}

	// SRS converts no WebRTC stream to SRT or RTSP: an SRT player gets
	// nothing while an RTMP player plays the stream, and RTSP has no such
	// stream to DESCRIBE.
	check.Case(t, "WhipToSrt_NotConverted", check.Normal, func(c *check.C) {
		stream := streamName(c)
		player := srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request")}, stream)
		read := joinRTMP(c, srs, stream)
		whipSend(c, whipPublish(c, srs, stream, whipH264Opus), whipH264Opus.samples(c), whipLead)
		read(playedRTMPUntil)
		ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
		defer cancel()
		r, _ := player.Analyze(ctx, playedUntil)
		c.Check(r.Video == nil && r.Audio == nil, "the SRT player got %v, want nothing", r)
	})
	check.Case(t, "WhipToRtsp_NotConverted", check.Normal, func(c *check.C) {
		stream := streamName(c)
		read := joinRTMP(c, srs, stream)
		whipSend(c, whipPublish(c, srs, stream, whipH264Opus), whipH264Opus.samples(c), whipLead)
		read(playedRTMPUntil)
		res, err := rtspDial(c, srs).Do(base.Describe, rtspURL(srs, "live/"+stream), nil, 300*time.Millisecond)
		if c.CheckNoError(err, "DESCRIBE") {
			c.CheckEqual(res.StatusCode, base.StatusNotFound, "DESCRIBE status")
		}
	})
}

// playedRTMPUntil is when an RTMP player has seen a converted WebRTC stream
// play: both sequence headers, a keyframe and 5 video frames.
var playedRTMPUntil = report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}

// checkNothingRTMP checks an RTMP player gets no media for d.
func checkNothingRTMP(c *check.C, player *rtmp.Client, d time.Duration) {
	c.Helper()
	player.SetDeadline(time.Now().Add(d))
	defer player.SetDeadline(time.Time{})
	r, _ := flv.Analyze(player, playedRTMPUntil)
	c.Check(r.Video == nil && r.Audio == nil, "the RTMP player got %v, want nothing", r)
}

// whepWhipPlayer plays WebRTC by WHEP from Common, the stream as WHIP sent
// it: SRS forwards the packets. The player connects before the publisher,
// and offers HEVC by vcodec for an HEVC stream. Like whepPlayer, its gap and
// sync limits are 200ms: the analyzer counts each track's times from its
// first packet, not by SRS's Sender Reports, which TestTimer_Convert_Reports
// checks, and reads the tracks on their own goroutines, which take the
// lead's burst apart.
var whepWhipPlayer = whipPlayer{
	name: "Whep", video: same("h264", "hevc"), audio: same("opus"),
	play: func(c *check.C, srs *server.Server, codec whipCodec, w convertWant) *report.Report {
		stream := streamName(c)
		url := whepURL(srs, "live", stream)
		var opts *rtc.Options
		if codec.video == "hevc" {
			url += "&vcodec=hevc"
			opts = &rtc.Options{Video: []string{"H265"}}
		}
		p := rtcPlayer(c, url, opts)
		whipSend(c, whipPublish(c, srs, stream, codec), codec.samples(c), whipLead)
		return waitPlayed(c, p, convertUntil(w))
	},
	gap: 200 * time.Millisecond, sync: 200 * time.Millisecond,
}

// whipCommonCells add the WebRTC source's cells of the Common server: WHEP,
// which plays the stream as it was sent, and RTMP, which gets nothing, since
// Common keeps rtc_to_rtmp off.
func whipCommonCells(t *testing.T) {
	whipCells(t, func(c *check.C) *server.Server { return server.Common(c) }, whepWhipPlayer)

	// With rtc_to_rtmp off, an RTMP player of a WebRTC stream gets
	// nothing, while a WebRTC player plays it.
	check.Case(t, "WhipToRtmp_NotConverted", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		player := dialRTMP(c, rtmpURL(srs, "live/"+stream), nil)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Play(ctx)
		c.Must(err, "play")
		whep := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		whipSend(c, whipPublish(c, srs, stream, whipH264Opus), whipH264Opus.samples(c), whipLead)
		waitPlayed(c, whep, rtcPlayedUntil)
		checkNothingRTMP(c, player, 100*time.Millisecond)
	})
}

// The WebRTC source's players on the shared remux and fmp4 servers, which
// have rtc_to_rtmp: HTTP-TS; HTTP MP3, which doesn't carry the AAC SRS
// transcodes from Opus; HTTP AAC; and HLS in fmp4, 3s segments
// (fmp4Segment), or with no video, 5s ones (hls_aof_ratio 2.5).
var (
	whipTsPlayer   = whipRemux("HttpTs", "ts", same("h264", "hevc"), map[string]string{"opus": "aac"})
	whipMp3Player  = whipRemux("HttpMp3", "mp3", nil, nil)
	whipAacPlayer  = whipRemux("HttpAac", "aac", nil, map[string]string{"opus": "aac"})
	whipFmp4Player = whipSegments("HlsFmp4", same("h264", "hevc"), map[string]string{"opus": "aac"},
		fmp4Segment+200*time.Millisecond, 5500*time.Millisecond, 3*time.Second, playWhipHLS)
)

// playWhipHLS plays a stream's HLS until u, for at most 300ms scaled by the
// limit scale: the segments are on disk once the playlist lists them, which
// whipSegments waits for.
func playWhipHLS(c *check.C, srs *server.Server, stream string, u report.Until) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(300*time.Millisecond))
	defer cancel()
	r, err := hls.PlayHTTP(ctx, httpClient, flvURL(srs, "live/"+stream+".m3u8"), u)
	c.Must(err, "play")
	return r
}

// TestTimer_Convert_Reports checks what a WebRTC player gets to sync its
// tracks by, on the shared timer server SegmentsKeep, and for streams
// converted to WebRTC on a server of the TsMount config: the Sender Reports
// that map each track's RTP times to one clock.
func TestTimer_Convert_Reports(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// A WHEP player of a WHIP publisher that sends Sender Reports gets a
	// report of each track within 2.5s, more than the second between the
	// reports a sender usually sends.
	check.Case(t, "WhipToWhep_SenderReports", check.Timer, func(c *check.C) {
		stream := streamName(c)
		p := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		whipSend(c, whipPublish(c, srs, stream, whipH264Opus), whipH264Opus.samples(c), whipLead)
		waitPlayed(c, p, rtcPlayedUntil)
		rec, ok := waitReports(c, p)
		c.Check(ok, "a Sender Report of each of the tracks %v", streamsOf(rec))
	})

	// The reports keep the publisher's sync: with the publisher's audio
	// reports 150ms later than its video, the player's first audio maps
	// 150ms after its first video, plus the first samples' own offset, by
	// SRS's reports; reports of SRS's own clock would map them together.
	check.Case(t, "WhipToWhep_ReportsKeepSync", check.Timer, func(c *check.C) {
		const shift = 150 * time.Millisecond
		stream := streamName(c)
		p := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		opts := whipH264Opus.options()
		opts.AudioReportShift = shift
		samples := whipH264Opus.samples(c)
		whipSend(c, rtcPublisher(c, whipH264Opus.url(srs, stream), opts), samples, whipLead)
		waitPlayed(c, p, rtcPlayedUntil)
		rec, ok := waitReports(c, p)
		if !c.Check(ok, "a Sender Report of each of the tracks %v", streamsOf(rec)) {
			return
		}
		video, vok := reportedStart(rec, "video/")
		audio, aok := reportedStart(rec, "audio/")
		if c.Check(vok && aok, "no video or audio stream: %v", streamsOf(rec)) {
			want := shift + firstDTS(samples, false) - firstDTS(samples, true)
			d := audio - video
			c.Check(d >= want-20*time.Millisecond && d <= want+40*time.Millisecond,
				"by the reports the first audio %v after the first video, want %v", d, want)
		}
	})

	// With init_rate_from_sdp on, as on this server, SRS knows each track's
	// clock rate from the SDP, but the RTP times of a publisher that has
	// sent no Sender Report yet map to no clock: its WHEP player gets no
	// report for 1.2s after it plays, more than SRS's 1s report timer.
	check.Case(t, "WhipToWhep_NoReportsBeforePublisher", check.Timer, func(c *check.C) {
		stream := streamName(c)
		p := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		opts := whipH264Opus.options()
		opts.SenderReports, opts.NoSenderReports = false, true
		whipSend(c, rtcPublisher(c, whipH264Opus.url(srs, stream), opts), whipH264Opus.samples(c), whipLead)
		waitPlayed(c, p, rtcPlayedUntil)
		time.Sleep(1200 * time.Millisecond)
		for _, pkt := range p.Recorder.Record().RTCPIn {
			if sr, ok := pkt.(*rtcp.SenderReport); ok {
				c.Check(false, "a Sender Report of %v before the publisher's first, want none", sr.SSRC)
				break
			}
		}
	})

	// A stream SRS converts to WebRTC, with rtmp_to_rtc, from RTMP, SRT or
	// GB28181, has no publisher's reports: SRS stamps both tracks by the
	// RTMP times of its frames, so its reports map them to that clock. These
	// run on a server of the TsMount config, its own in the timer process.
	conv := server.Shared(t, "TsMount", sharedTsMountConfig())
	for _, s := range []struct {
		src  convertSource
		file string
	}{{rtmpSource, media.H264BaselineAAC}, {srtSource, media.H264HighAACTS}, {gbSource, media.H264BaselineAAC}} {
		check.Case(t, s.src.name+"ToWhep_SenderReports", check.Timer, func(c *check.C) {
			stream := streamName(c)
			p := rtcPlayer(c, whepURL(conv, "live", stream), nil)
			s.src.realTime(c, conv, "live/"+stream, s.file, convertLead)
			waitPlayed(c, p, rtcPlayedUntil)
			rec, ok := waitReports(c, p)
			c.Check(ok, "a Sender Report of each of the tracks %v", streamsOf(rec))
		})
	}

	// The reports of an RTMP stream map each track to the RTMP times: the
	// player's first audio maps as far after its first video as the first
	// AAC frame is after the first video frame in the file.
	check.Case(t, "RtmpToWhep_ReportsKeepSync", check.Timer, func(c *check.C) {
		stream := streamName(c)
		p := rtcPlayer(c, whepURL(conv, "live", stream), nil)
		rtmpSource.realTime(c, conv, "live/"+stream, media.H264BaselineAAC, convertLead)
		waitPlayed(c, p, rtcPlayedUntil)
		rec, ok := waitReports(c, p)
		if !c.Check(ok, "a Sender Report of each of the tracks %v", streamsOf(rec)) {
			return
		}
		video, vok := reportedStart(rec, "video/")
		audio, aok := reportedStart(rec, "audio/")
		if c.Check(vok && aok, "no video or audio stream: %v", streamsOf(rec)) {
			tags := readTags(c, media.H264BaselineAAC, 0, time.Second)
			want := firstFrameTime(tags, media.TagAudio) - firstFrameTime(tags, media.TagVideo)
			d := audio - video
			c.Check(d >= want-20*time.Millisecond && d <= want+40*time.Millisecond,
				"by the reports the first audio %v after the first video, want %v", d, want)
		}
	})
}

// firstFrameTime is the time of the first audio or video tag that is not a
// sequence header.
func firstFrameTime(tags []*media.FLVTag, typ uint8) time.Duration {
	for _, tag := range tags {
		if tag.Type == typ && len(tag.Data) > 1 && tag.Data[1] != 0 {
			return time.Duration(tag.Timestamp) * time.Millisecond
		}
	}
	return 0
}

// waitReports waits at most 2.5s, more than the second between the reports
// a sender usually sends, until a player has a Sender Report of each of its
// two tracks, and returns its record then, or at the timeout and false.
func waitReports(c *check.C, p *rtc.Player) (*rtc.Record, bool) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2500*time.Millisecond)
	defer cancel()
	rec, err := p.Recorder.Until(ctx, func(rec *rtc.Record) bool {
		reported := map[uint32]bool{}
		for _, pkt := range rec.RTCPIn {
			if sr, ok := pkt.(*rtcp.SenderReport); ok {
				reported[sr.SSRC] = true
			}
		}
		for ssrc := range rec.In {
			if !reported[ssrc] {
				return false
			}
		}
		return len(rec.In) == 2
	})
	return rec, err == nil
}

// reportedStart is the time that the last Sender Report of the received
// stream of the kind, "video/" or "audio/", maps the stream's first packet
// to, from its RTP time at the codec's clock rate.
func reportedStart(rec *rtc.Record, kind string) (time.Duration, bool) {
	for ssrc, s := range rec.In {
		if !strings.HasPrefix(strings.ToLower(s.MimeType), kind) {
			continue
		}
		var last *rtcp.SenderReport
		for _, pkt := range rec.RTCPIn {
			if sr, ok := pkt.(*rtcp.SenderReport); ok && sr.SSRC == ssrc {
				last = sr
			}
		}
		if last == nil {
			return 0, false
		}
		rate := 90000.0
		if kind == "audio/" {
			rate = 48000
		}
		at := time.Duration(float64(last.NTPTime) / (1 << 32) * float64(time.Second))
		back := time.Duration(float64(last.RTPTime-s.FirstTimestamp) / rate * float64(time.Second))
		return at - back, true
	}
	return 0, false
}

// firstDTS is the decode time of the first video or audio sample.
func firstDTS(samples []rtc.Sample, video bool) time.Duration {
	for _, s := range samples {
		if s.Video == video {
			return s.DTS
		}
	}
	return 0
}

// streamsOf describes the streams a session received, by SSRC.
func streamsOf(rec *rtc.Record) string {
	if rec == nil {
		return "none"
	}
	var parts []string
	for ssrc, s := range rec.In {
		parts = append(parts, fmt.Sprintf("%v %v", ssrc, s.MimeType))
	}
	sort.Strings(parts)
	return strings.Join(parts, ", ")
}
