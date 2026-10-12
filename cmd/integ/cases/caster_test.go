// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"crypto/rand"
	"fmt"
	"net"
	"os"
	"path"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// casterUdpTsConfig is a server of its own with the MPEG-TS over UDP caster,
// RTMP and HTTP-FLV with Common's latency, and hooks. A server has one
// stream caster, which Common gives to GB28181.
func casterUdpTsConfig() server.Config {
	cfg := server.RemuxConfig("flv")
	cfg.Listeners = append(cfg.Listeners, server.UDPTS)
	cfg.Hooks = true
	cfg.BelowPeak = true
	return cfg
}

// TestCaster_UdpTs checks the MPEG-TS over UDP stream caster, basic cases
// only, since it is no standard protocol: SRS converts the H.264 and AAC of
// the TS its UDP port gets, from any sender, to one RTMP stream,
// server.UDPTSStream, and sends each sequence header once in the caster's
// life. So the group pushes the TS test file once, from its start to its
// end, in datagrams of 7 TS packets in real time, and every case plays that
// stream; an RTMP player that joined before the push records all of it. The
// bad input is datagrams of other senders, which SRS adds to the same
// buffer, so none has a TS sync byte where the push's packets would lose
// theirs.
func TestCaster_UdpTs(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, casterUdpTsConfig())
	rec := recordRTMP(t, rtmpURL(srs, server.UDPTSStream))
	pushUDPTS(t, srs, media.H264HighAACTS)

	// The push plays over RTMP from the GOP cache: H.264 and AAC, each
	// sequence header once, from a keyframe.
	check.Case(t, "PushPlaysRtmp", check.Normal, func(c *check.C) {
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		r, err := flv.PlayRTMP(ctx, rtmpURL(srs, server.UDPTSStream), nil, srtPlayedUntil)
		c.Must(err, "play")
		checkSRTPlayed(c, r)
	})

	// The push plays over HTTP-FLV.
	check.Case(t, "PushPlaysHttpFlv", check.Normal, func(c *check.C) {
		checkSRTPlayed(c, httpFlvPlay(c, srs, server.UDPTSStream+".flv", nil, srtPlayedUntil))
	})

	// The player that joined before the push gets it from its start: each
	// AAC frame at its own time, 1024 samples after the one before, also
	// the frames that the TS carries in one PES, as its first PES of 255ms.
	check.Case(t, "AudioFramesTimed", check.Normal, func(c *check.C) {
		check.KnownBug(c, "the caster stamps every AAC frame of a PES with the PES's time, so the frames of a PES of several come at once and leave a gap after them")
		r := rec.until(c, report.Until{AudioHeader: true, AudioFrames: 30})
		checkAudio(c, &r, "aac", 1)
		c.CheckEqual(r.Audio.Backwards, 0, "audio frames that step back")
		c.Check(r.Audio.MaxGap <= 100*time.Millisecond, "the audio has a gap of %v, over 100ms", r.Audio.MaxGap)
	})

	// The recording player's first video frame is the first keyframe.
	check.Case(t, "StartsAtKeyframe", check.Normal, func(c *check.C) {
		check.KnownBug(c, "the caster sends each NALU of a frame as an RTMP message of its own, so the SEI before the first IDR comes as an inter frame before the keyframe")
		r := rec.until(c, report.Until{VideoHeader: true, Keyframes: 1})
		checkVideo(c, &r, "h264")
		c.CheckEqual(r.Video.LeadingFrames, 0, "video frames before the first keyframe")
	})

	// The TS has B-frames, so a frame's presentation time is after its decode
	// time; the RTMP stream carries the difference as each video tag's
	// composition time.
	check.Case(t, "PushKeepsCompositionTime", check.Normal, func(c *check.C) {
		check.KnownBug(c, "the caster stamps each H.264 frame with its decode time as its presentation time too, so every composition time is 0 and B-frames play out of order")
		player := dialRTMP(c, rtmpURL(srs, server.UDPTSStream), nil)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := player.Play(ctx)
		c.Must(err, "play")
		rec := &ctsRecorder{tagPlayer: player}
		var a flv.Analyzer
		c.Must(readUntil(rec, &a, report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 10}, time.Second), "play")
		c.Check(rec.nonZero > 0, "%v video frames, none with a composition time", rec.frames)
	})

	// Datagrams of bytes that are no TS, with no sync byte, are dropped: the
	// push plays on, its RTMP stream the same publish, and the API answers.
	check.Case(t, "GarbageIgnored", check.Normal, func(c *check.C) {
		var bad [][]byte
		for range 20 {
			bad = append(bad, noSyncBytes(srt.MessageSize))
		}
		checkCasterSurvives(c, srs, bad)
	})

	// Datagrams shorter than a TS packet are held until more bytes come,
	// then dropped with them.
	check.Case(t, "ShortDatagramsIgnored", check.Normal, func(c *check.C) {
		var bad [][]byte
		for _, n := range []int{1, 2, 10, 100, 187, 187} {
			bad = append(bad, noSyncBytes(n))
		}
		checkCasterSurvives(c, srs, bad)
	})

	// TS packets that don't parse, on a PID the push doesn't use: an
	// adaptation field longer than the packet, a PES header that is no PES,
	// a reserved adaptation field control, and the transport error flag.
	check.Case(t, "BadTsPacketsIgnored", check.Normal, func(c *check.C) {
		const pid = 0x1abc
		var bad [][]byte
		for _, mk := range []func([]byte){
			func(p []byte) { p[3] = 0x30; p[4] = 0xff },            // adaptation field and payload, the field 255 bytes
			func(p []byte) { p[1] |= 0x40; p[3] = 0x10; p[4] = 9 }, // a payload start that is no PES
			func(p []byte) { p[3] = 0x00 },                         // reserved adaptation field control
			func(p []byte) { p[1] |= 0x80; p[3] = 0x10 },           // transport error
		} {
			p := noSyncBytes(188)
			p[0], p[1], p[2] = 0x47, byte(pid>>8), byte(pid&0xff)
			mk(p)
			bad = append(bad, p)
		}
		checkCasterSurvives(c, srs, [][]byte{concat(bad)})
	})
}

// casterLead is the media the push sends at once, by its PCR: past the
// keyframe at 1s, so the GOP a player gets from the cache comes after the
// first PESes of the test file's audio, which have several AAC frames each
// (AudioFramesTimed).
const casterLead = 1200 * time.Millisecond

// pushUDPTS sends a TS file to the server's MPEG-TS over UDP caster until
// the group ends, casterLead of it at once, then the rest in real time, and
// waits for the on_publish of the caster's RTMP stream.
func pushUDPTS(t *testing.T, srs *server.Server, file string) {
	t.Helper()
	conn, err := net.Dial("udp", srs.Addr(server.UDPTS))
	if err != nil {
		t.Fatalf("dial the caster: %v", err)
	}
	f, err := os.Open(media.Path(t, file))
	if err != nil {
		conn.Close()
		t.Fatalf("open the media: %v", err)
	}
	s := srt.NewSender(f)
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	t.Cleanup(func() {
		cancel()
		conn.Close()
		<-done
		f.Close()
	})
	if err := s.SendThrough(conn, casterLead); err != nil {
		done <- nil
		t.Fatalf("push the lead: %v", err)
	}
	go func() { done <- s.Stream(ctx, conn, nil, nil) }()

	wctx, wcancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer wcancel()
	if _, err := srs.Hooks.Wait(wctx, mock.Match{Action: "on_publish", Stream: path.Base(server.UDPTSStream)}); err != nil {
		t.Fatalf("the caster's on_publish: %v", err)
	}
}

// casterLiveAfter is how long after the bad input a frame must come to a
// new player to have been converted after it: the GOP cache it gets first
// comes at once.
const casterLiveAfter = 100 * time.Millisecond

// checkCasterSurvives sends each datagram from a sender of its own, then
// checks the server stays up: the API answers, a new RTMP player gets the
// stream from its GOP and 3 video frames converted after the bad input,
// and the caster's RTMP stream is still its first publish.
func checkCasterSurvives(c *check.C, srs *server.Server, bad [][]byte) {
	c.Helper()
	conn, err := net.Dial("udp", srs.Addr(server.UDPTS))
	c.Must(err, "dial the caster")
	defer conn.Close()
	for _, b := range bad {
		_, err := conn.Write(b)
		c.Must(err, "send the bad input")
	}
	since := time.Now()

	var v struct {
		Code int `json:"code"`
	}
	apiGet(c, srs, "/api/v1/versions", &v)
	c.CheckEqual(v.Code, 0, "the API's code")

	r := playCasterAfter(c, srs, since)
	checkSRTPlayed(c, r)

	stream := path.Base(server.UDPTSStream)
	c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})), 1, "on_publish requests")
	c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: stream})), 0, "on_unpublish requests")
}

// playCasterAfter plays the caster's stream over RTMP until it has the
// sequence headers, a keyframe, and 3 video frames that came casterLiveAfter
// after since.
func playCasterAfter(c *check.C, srs *server.Server, since time.Time) *report.Report {
	c.Helper()
	player := dialRTMP(c, rtmpURL(srs, server.UDPTSStream), nil)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_, err := player.Play(ctx)
	c.Must(err, "play")
	player.SetDeadline(time.Now().Add(time.Second))
	defer player.SetDeadline(time.Time{})

	var a flv.Analyzer
	after := since.Add(casterLiveAfter)
	live := 0
	for live < 3 || !srtPlayedUntil.Done(&a.Report) {
		tag, err := player.ReadTag()
		c.Must(err, fmt.Sprintf("read, %v video frames after the bad input: %v", live, &a.Report))
		frames := videoFrameCount(&a.Report)
		c.Must(a.Add(tag), "analyze")
		if videoFrameCount(&a.Report) > frames && time.Now().After(after) {
			live++
		}
	}
	return &a.Report
}

// videoFrameCount is the number of video frames of a report.
func videoFrameCount(r *report.Report) int {
	if r.Video == nil {
		return 0
	}
	return r.Video.Frames
}

// noSyncBytes is n random bytes, none of them 0x47, the TS sync byte.
func noSyncBytes(n int) []byte {
	b := make([]byte, n)
	rand.Read(b)
	for i := range b {
		if b[i] == 0x47 {
			b[i] = 0x46
		}
	}
	return b
}

// concat joins byte slices.
func concat(l [][]byte) []byte {
	var b []byte
	for _, p := range l {
		b = append(b, p...)
	}
	return b
}

// ctsRecorder counts the H.264 frames a player reads, and those with a
// composition time.
type ctsRecorder struct {
	tagPlayer
	frames, nonZero int
}

func (r *ctsRecorder) ReadTag() (*media.FLVTag, error) {
	tag, err := r.tagPlayer.ReadTag()
	if err != nil {
		return nil, err
	}
	// A legacy FLV AVC tag: the codec id 7, the packet type 1 for a frame,
	// then the composition time in 24 bits.
	if d := tag.Data; tag.Type == media.TagVideo && len(d) >= 5 && d[0]&0x0f == 7 && d[1] == 1 {
		r.frames++
		if d[2]|d[3]|d[4] != 0 {
			r.nonZero++
		}
	}
	return tag, nil
}

// rtmpRecorder is an RTMP player of a group that reads its stream until the
// group ends, for the cases to check what it got.
type rtmpRecorder struct {
	mu sync.Mutex
	a  flv.Analyzer
	// err is why it stopped reading, nil while it reads.
	err error
}

// recordRTMP plays a stream over RTMP until the group ends: it joins at
// once, so it gets a stream published next from its start.
func recordRTMP(t *testing.T, url string) *rtmpRecorder {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	player, err := rtmp.Dial(ctx, url, nil)
	if err != nil {
		t.Fatalf("dial the recording player: %v", err)
	}
	if _, err := player.Play(ctx); err != nil {
		player.Close()
		t.Fatalf("play the recording player: %v", err)
	}

	rec := &rtmpRecorder{}
	done := make(chan struct{})
	go func() {
		defer close(done)
		for {
			tag, err := player.ReadTag()
			if err == nil {
				rec.mu.Lock()
				err = rec.a.Add(tag)
				rec.mu.Unlock()
			}
			if err != nil {
				rec.mu.Lock()
				rec.err = err
				rec.mu.Unlock()
				return
			}
		}
	}()
	t.Cleanup(func() {
		player.Close()
		<-done
	})
	return rec
}

// until waits, for at most 400ms, until the recording meets u,
// and returns a copy of its report.
func (r *rtmpRecorder) until(c *check.C, u report.Until) report.Report {
	c.Helper()
	deadline := time.Now().Add(400 * time.Millisecond)
	for {
		r.mu.Lock()
		rep, err := r.a.Report, r.err
		if rep.Video != nil {
			v := *rep.Video
			rep.Video = &v
		}
		if rep.Audio != nil {
			a := *rep.Audio
			rep.Audio = &a
		}
		r.mu.Unlock()
		if u.Done(&rep) {
			return rep
		}
		if err != nil || time.Now().After(deadline) {
			c.Must(fmt.Errorf("the recording player has %v, before %v: %v", &rep, u, err), "record")
		}
		time.Sleep(10 * time.Millisecond)
	}
}
