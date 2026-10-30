// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"net"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/dash"
	"srs-integ/internal/flv"
	"srs-integ/internal/gb"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// The GB28181 source of the conversion matrix: a device's PS over RTP over
// TCP, for a session the case asks for by the publish API, to each player.
// SRS publishes a GB stream to its own RTMP as live/<id>, so every RTMP
// player and remux plays it, RTSP with rtmp_to_rtsp and WebRTC with
// rtmp_to_rtc too. A cell is named GbTo<Player>_<Codec>, such as
// GbToHlsTs_HevcAac.

// gbH264Only names the video of H264BaselineAAC without its audio, as a
// device with its audio off sends it, without B-frames, since SRS takes each
// frame's DTS as its PTS.
const gbH264Only = "gb:h264-only"

// gbCodecs are the codecs a GB source publishes: H.264 or HEVC with AAC, the
// one audio codec SRS takes from PS, and H.264 alone. A device sends no audio
// without video.
var gbCodecs = []convertCodec{
	{"H264Aac", media.H264BaselineAAC, "h264", "aac"},
	{"HevcAac", media.HEVCAAC, "hevc", "aac"},
	{"H264Only", gbH264Only, "h264", ""},
}

// gbFramesOf returns the frames a GB stream of a test file sends, from its
// first keyframe on, and its video codec; gbH264Only is the video frames of
// H264BaselineAAC.
func gbFramesOf(c *check.C, file string) ([]gb.Frame, string) {
	c.Helper()
	if file != gbH264Only {
		return gbFrames(c, file)
	}
	frames, vcodec := gbFrames(c, media.H264BaselineAAC)
	var video []gb.Frame
	for _, f := range frames {
		if f.Video {
			video = append(video, f)
		}
	}
	return video, vcodec
}

// gbHold is the media SRS holds of a GB stream, so a publisher sends that
// much more at once: it holds the last PS pack until the next one comes, and
// sends a frame on only while it holds 2 frames of each track the stream has
// sent, which gbLead covers.
const gbHold = gbLead

// gbRealTime publishes a file over GB28181 as the stream path, which must be
// live/<id>, in real time until the case ends, the media before lead at once.
// SRS holds 2 frames of each track, which delays each of them alike.
func gbRealTime(c *check.C, srs *server.Server, path, file string, lead time.Duration) {
	c.Helper()
	s := gbStartID(c, srs, file, gbID(c, path), gbSSRC())
	s.leadBy(c, lead)
}

// gbThrough publishes a file over GB28181 as the stream path at once, in a
// burst slot of the server, and keeps the stream published until the case
// ends. So SRS sends the frames at d, it sends the media SRS holds after them
// (gbHold) too.
func gbThrough(c *check.C, srs *server.Server, path, file string, d time.Duration) {
	c.Helper()
	s := gbStartID(c, srs, file, gbID(c, path), gbSSRC())
	// A small send buffer keeps the burst slot until SRS has read most of the
	// burst, not only until the kernel has it.
	if tc, ok := s.conn.(*net.TCPConn); ok {
		c.Must(tc.SetWriteBuffer(burstBuffer), "set the send buffer")
	}
	free := srs.Burst(c)
	defer free()
	s.sendThrough(c, d+gbHold)
}

// gbID is the id of a GB stream path: SRS publishes a session as live/<id>.
func gbID(c *check.C, path string) string {
	c.Helper()
	id, ok := strings.CutPrefix(path, "live/")
	if !ok {
		c.Fatalf("a GB stream is live/<id>, not %v", path)
	}
	return id
}

// gbCells adds a case for each codec of the GB source.
func gbCells(t *testing.T, srsOf func(c *check.C) *server.Server, p convertPlayer) {
	for _, codec := range gbCodecs {
		check.Case(t, gbSource.name+"To"+p.name+"_"+codec.name, check.Normal, func(c *check.C) {
			if reason, ok := p.knownBugs[codec.name]; ok {
				check.KnownBug(c, reason)
			}
			srs := srsOf(c)
			w := p.want(codec)
			if w.video == "" && w.audio == "" {
				p.none(c, srs, gbSource, codec)
				return
			}
			checkConverted(c, p.play(c, srs, gbSource, p.fileOf(codec), w), w, p.gap, p.sync)
		})
	}
}

// gbRtspPlayer plays RTSP over TCP. SRS keeps no GOP for RTSP, so the
// player joins while SRS holds a keyframe as the oldest video frame of a GB
// stream (see gbHold), which it then sends first: the device sends its first
// GOP and the keyframe after it at once, with the audio of the next 100ms or,
// with no audio, the next video frame, whose pack SRS holds until the next
// one comes, the player joins, and the device sends gbLead more at once and
// the rest in real time.
var gbRtspPlayer = convertPlayer{
	name: "Rtsp", video: same("h264", "hevc"), audio: same("aac"),
	play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
		s := gbStart(c, srs, file, gbSSRC())
		held := 1
		if file == gbH264Only {
			held = 2
		}
		n := 0
		s.frames, n = gbJoinAt(s.frames, held, 100*time.Millisecond)
		c.Must(s.pub.Send(context.Background(), s.frames[:n], nil), "send the media")
		s.next = n
		p := rtspPlay(c, srs, "live/"+s.id, nil)
		s.leadBy(c, s.frames[n-1].DTS-s.frames[0].DTS+gbLead)
		return rtspUntil(c, p, convertUntil(w))
	},
	gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
}

// gbJoinAt orders frames so a player can join a GB stream at its second
// keyframe: it returns a copy whose first n frames are the frames before
// that keyframe, the keyframe and the held-1 video frames after it, and the
// audio of ahead after the keyframe, with the other frames after them in
// order. SRS then holds that keyframe as its oldest video frame.
func gbJoinAt(frames []gb.Frame, held int, ahead time.Duration) ([]gb.Frame, int) {
	var at time.Duration
	keys := 0
	for _, f := range frames {
		if f.Video && f.Key {
			if keys++; keys == 2 {
				at = f.DTS
				break
			}
		}
	}
	var head, rest []gb.Frame
	videos := 0
	for _, f := range frames {
		switch {
		case f.DTS < at:
			head = append(head, f)
		case f.Video && videos < held:
			head = append(head, f)
			videos++
		case !f.Video && f.DTS < at+ahead:
			head = append(head, f)
		default:
			rest = append(rest, f)
		}
	}
	return append(head, rest...), len(head)
}

// gbCommonCells adds the GB source's cells of the Common server: RTMP,
// HTTP-FLV and RTSP; SRT, which SRS doesn't convert RTMP
// to; and WHEP, refused with rtmp_to_rtc off.
func gbCommonCells(t *testing.T) {
	common := func(c *check.C) *server.Server { return server.Common(c) }
	for _, p := range []convertPlayer{rtmpPlayer, httpFlvConvert, gbRtspPlayer} {
		gbCells(t, common, p)
	}
	gbVideoOnlyCells(t)

	// SRS has no RTMP to SRT conversion, so an SRT player of a GB stream is
	// accepted and gets nothing, while an RTMP player plays it.
	check.Case(t, "GbToSrt_NotConverted", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		path := "live/" + name
		player := srtPlayer(c, srs, srt.Options{StreamID: srtID(path, "request")}, name)
		gbRealTime(c, srs, path, media.H264BaselineAAC, gbLead)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := flv.PlayRTMP(ctx, rtmpURL(srs, path), nil, gbUntil)
		c.Must(err, "an RTMP player of the stream")
		ctx, cancel = context.WithTimeout(context.Background(), 100*time.Millisecond)
		defer cancel()
		r, _ := player.Analyze(ctx, playedUntil)
		c.Check(r.Video == nil && r.Audio == nil, "the SRT player got %v, want nothing", r)
	})

	// With rtmp_to_rtc off, SRS refuses a WHEP player of a GB stream as of an
	// RTMP one.
	check.Case(t, "GbToWhep_Disabled", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		path := "live/" + name
		gbRealTime(c, srs, path, media.H264BaselineAAC, gbLead)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := flv.PlayRTMP(ctx, rtmpURL(srs, path), nil, gbUntil)
		c.Must(err, "an RTMP player of the stream")
		checkWhepDisabled(c, srs, name)
	})

	// The FLV test files have B-frames, which a device sends with a PES PTS
	// after its DTS: SRS carries the difference to RTMP as each video tag's
	// composition time, the file's own, frame by frame from the first.
	for _, v := range []struct{ name, file string }{{"H264", media.H264HighAAC}, {"Hevc", media.HEVCAAC}} {
		check.Case(t, "GbToRtmp_"+v.name+"Cts", check.Normal, func(c *check.C) {
			frames, _ := gbFramesOf(c, v.file)
			var want []int32
			for _, f := range frames {
				if f.Video {
					want = append(want, int32(f.PTS.Milliseconds()-f.DTS.Milliseconds()))
				}
			}
			srs := server.Common(c)
			path := "live/" + streamName(c)
			player := dialRTMP(c, rtmpURL(srs, path), nil)
			ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(time.Second))
			defer cancel()
			_, err := player.Play(ctx)
			c.Must(err, "play")
			gbThrough(c, srs, path, v.file, 2200*time.Millisecond)
			checkVideoCTS(c, player, want)
		})
	}
}

// gbVideoOnlyCells adds the case of the latency of a GB stream with no
// audio, which SRS holds as one with audio.
func gbVideoOnlyCells(t *testing.T) {
	// A player of a real-time GB stream with no audio gets its first frames
	// as one of a stream with audio does, after the lead.
	check.Case(t, "GbToRtmp_LatencyH264Only", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, gbH264Only, gbSSRC())
		s.lead(c)
		playUntilWithin(c, rtmpURL(srs, "live/"+s.id), convertUntil(convertWant{video: "h264"}), 300*time.Millisecond)
	})
}

// The segment players of the GB source play the 1s fragments of the shared
// GbSegments server, and the fmp4 segments of Fmp4, from the first segment: a
// GB stream costs SRS more than an RTMP one, as it reads the PS and publishes
// it again over RTMP, so a GB segment cell sends no more than it needs.
var (
	// gbHlsTsPlayer plays GbSegments' HLS: the first segment, which ends at
	// the keyframe at 2s, not 1s: SRS stamps the first keyframe 1ms after the
	// sequence header of the same time, so at 1s the segment lasts 999ms.
	gbHlsTsPlayer = convertPlayer{
		name: "HlsTs", video: same("h264", "hevc"), audio: same("aac"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			src.through(c, srs, path, file, 2*time.Second)
			return playHLSWithin(c, flvURL(srs, path+".m3u8"), segmentsOf(w, 1500*time.Millisecond), 300*time.Millisecond)
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}

	// gbDashPlayer plays GbSegments' DASH: the first 2s segment, once the
	// second HLS segment, which ends at 3s, is there: SRS has written the MPD
	// at 2s, so the play waits 100ms scaled by the limit scale.
	gbDashPlayer = convertPlayer{
		name: "Dash", video: same("h264", "hevc"), audio: same("aac"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			src.through(c, srs, path, file, rtcToRtmpDashSegment+time.Second)
			waitPlaylist(c, httpClient, flvURL(srs, path+".m3u8"), hasSegment(1))
			ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(100*time.Millisecond))
			defer cancel()
			r, err := dash.PlayHTTP(ctx, httpClient, flvURL(srs, path+".mpd"), segmentsOf(w, 1500*time.Millisecond))
			c.Must(err, "play")
			return r
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}

	// gbFmp4Player plays Fmp4's HLS: the first segment, which ends at the
	// keyframe a second after fmp4Segment, as gbHlsTsPlayer's does: at
	// fmp4Segment the segment lasts 1ms less.
	gbFmp4Player = convertPlayer{
		name: "HlsFmp4", video: same("h264", "hevc"), audio: same("aac"),
		play: func(c *check.C, srs *server.Server, src convertSource, file string, w convertWant) *report.Report {
			path := "live/" + streamName(c)
			src.through(c, srs, path, file, fmp4Segment+time.Second)
			return playHLSWithin(c, flvURL(srs, path+".m3u8"), segmentsOf(w, 3*time.Second), fmp4PlayWait)
		},
		gap: 100 * time.Millisecond, sync: 100 * time.Millisecond,
	}
)

// sharedGbSegmentsConfig is the config of the shared GbSegments server: the
// GB28181 caster, and HLS in TS and DASH with 1s fragments, so a GB stream
// sent at once makes segments from a few seconds. Common cuts 10s segments,
// which a GB stream would take 10 times the media to make, and RtcToRtmp,
// which has 1s fragments, transcodes the audio of its WebRTC cells, so the
// GB bursts there made both too slow.
func sharedGbSegmentsConfig() server.Config {
	cfg := server.HLSConfig(server.HLS{Fragment: time.Second})
	cfg = server.WithDASH(cfg, server.DASH{Fragment: time.Second})
	return server.WithGB(cfg, server.GBOptions{})
}

// TestConvert_GbSegments converts GB28181 streams to HLS in TS and DASH, on
// the shared GbSegments server.
func TestConvert_GbSegments(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "GbSegments", sharedGbSegmentsConfig())
	srsOf := func(*check.C) *server.Server { return srs }
	gbCells(t, srsOf, gbHlsTsPlayer)
	gbCells(t, srsOf, gbDashPlayer)
}
