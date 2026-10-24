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
// server.UDPTSStream, and sends each sequence header once per publish of
// that stream. So the group pushes the TS test file once, from its start to its
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

	// The push plays over HTTP-FLV. SRS writes the FLV header from the first
	// messages a player gets (guess_has_av), so a player that joins while the
	// GOP cache has the keyframe but not yet the audio PES after it, up to
	// 255ms later, gets no audio; the player joins once the GOP has audio.
	check.Case(t, "PushPlaysHttpFlv", check.Normal, func(c *check.C) {
		rec.gopWithAudio(c)
		checkSRTPlayed(c, httpFlvPlay(c, srs, server.UDPTSStream+".flv", nil, srtPlayedUntil))
	})

	// The player that joined before the push gets it from its start: each
	// AAC frame at its own time, 1024 samples after the one before, also
	// the frames that the TS carries in one PES, as its first PES of 255ms.
	check.Case(t, "AudioFramesTimed", check.Normal, func(c *check.C) {
		r := rec.until(c, report.Until{AudioHeader: true, AudioFrames: 30})
		checkAudio(c, &r, "aac", 1)
		c.CheckEqual(r.Audio.Backwards, 0, "audio frames that step back")
		c.Check(r.Audio.MaxGap <= 100*time.Millisecond, "the audio has a gap of %v, over 100ms", r.Audio.MaxGap)
	})

	// The recording player's first video frame is the first keyframe.
	check.Case(t, "StartsAtKeyframe", check.Normal, func(c *check.C) {
		r := rec.until(c, report.Until{VideoHeader: true, Keyframes: 1})
		checkVideo(c, &r, "h264")
		c.CheckEqual(r.Video.LeadingFrames, 0, "video frames before the first keyframe")
	})

	// The TS has B-frames, so a frame's presentation time is after its decode
	// time; the RTMP stream carries the difference as each video tag's
	// composition time.
	check.Case(t, "PushKeepsCompositionTime", check.Normal, func(c *check.C) {
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

// TestCaster_UdpTsRepush checks a push to the MPEG-TS over UDP caster after
// its RTMP output closed, as when the server closed the caster's publisher
// for a stream idle past its timeout, on a server of its own, since that ends
// the caster's stream: a first push sends casterLead of the TS test file and
// stops, a DELETE of the caster's RTMP publisher closes the output, then a
// new sender pushes the file again from its start, which the caster
// publishes again at its first datagrams after the close.
func TestCaster_UdpTsRepush(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, casterUdpTsConfig())

	// A player of the second publish gets each sequence header again, then
	// the stream from a keyframe, with times from the second push's start.
	check.Case(t, "SecondPushPlays", check.Normal, func(c *check.C) {
		stream := path.Base(server.UDPTSStream)
		sendUDPTS(c, srs, media.H264HighAACTS, casterLead)
		pub := waitHook(c, srs, "on_publish", stream)
		c.Must(kickOK(srs, pub.Field("client_id")), "delete the caster's publisher")
		waitHook(c, srs, "on_unpublish", stream)

		repushUDPTS(c, srs, media.H264HighAACTS, stream)
		ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(time.Second))
		defer cancel()
		r, err := flv.PlayRTMP(ctx, rtmpURL(srs, server.UDPTSStream), nil, srtPlayedUntil)
		c.Must(err, "play the second publish")
		checkSRTPlayed(c, r)
		c.Check(r.Video.FirstDTS < casterLead+check.Scaled(time.Second), "the first video frame at %v, not from the second push's start", r.Video.FirstDTS)
	})
}

// casterSlowHook is how long a slow hook server takes to accept the caster's
// RTMP publish: longer than SRS's 500ms pulse.
const casterSlowHook = 800 * time.Millisecond

// TestTimer_Caster_UdpTsOutput checks the RTMP publish the MPEG-TS over UDP
// caster makes to its own server while the hook server takes longer than
// SRS's 500ms pulse to accept it, on a server of its own, since a server has
// one caster stream.
func TestTimer_Caster_UdpTsOutput(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, casterUdpTsConfig())

	// A sender pushes the whole file at once while on_publish is slow, more
	// than the socket buffers between the caster's RTMP output and its RTMP
	// server hold: the output waits for the publish, and once the hook
	// accepts, the caster has published its stream once, with no new
	// connection and no unpublish, and a player gets it.
	check.Case(t, "SlowOnPublishOnePublish", check.Timer, func(c *check.C) {
		stream := path.Base(server.UDPTSStream)
		srs.Hooks.Script(hookOfStream("on_publish", stream), mock.OK().After(casterSlowHook))
		sendUDPTS(c, srs, media.H264HighAACTS, 30*time.Second)
		pub := waitHook(c, srs, "on_publish", stream)
		time.Sleep(time.Until(pub.Time.Add(casterSlowHook + 300*time.Millisecond)))
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_connect"})), 1, "on_connect requests")
		c.CheckEqual(len(srs.Hooks.Requests(hookOfStream("on_publish", stream))), 1, "on_publish requests")
		c.CheckEqual(len(srs.Hooks.Requests(hookOfStream("on_unpublish", stream))), 0, "on_unpublish requests")
		r := playUntilWithin(c, rtmpURL(srs, server.UDPTSStream), srtPlayedUntil, 500*time.Millisecond)
		checkSRTPlayed(c, r)
	})
}

// pushUDPTS sends a TS file to the server's MPEG-TS over UDP caster until
// the group ends, casterLead of it at once, then the rest in real time, and
// waits for the on_publish of the caster's RTMP stream.
func pushUDPTS(t *testing.T, srs *server.Server, file string) {
	t.Helper()
	streamUDPTS(t, srs, file)

	wctx, wcancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer wcancel()
	if _, err := srs.Hooks.Wait(wctx, mock.Match{Action: "on_publish", Stream: path.Base(server.UDPTSStream)}); err != nil {
		t.Fatalf("the caster's on_publish: %v", err)
	}
}

// streamUDPTS sends a TS file to the server's MPEG-TS over UDP caster from a
// sender of its own until the test ends, casterLead of it at once, then the
// rest in real time.
func streamUDPTS(tb testing.TB, srs *server.Server, file string) {
	tb.Helper()
	p := newUDPTSPush(tb, srs, file)
	if err := p.s.SendThrough(p.conn, casterLead); err != nil {
		tb.Fatalf("push the lead: %v", err)
	}
	p.stream()
}

// repushUDPTS pushes a TS file to the server's MPEG-TS over UDP caster after
// its RTMP output closed, casterLead of it at once, then the rest in real
// time, and waits for the caster to publish stream again. The caster finds
// its output closed only when a write fails, which a write before the server
// closed the socket doesn't, so the push sends 40ms of media each 40ms until
// the caster publishes again, which takes the caster's RTMP connect and
// publish with their hooks: on an idle host long before the keyframe at 1s
// in the lead, under load in up to 600ms scaled by -integ.limit-scale.
func repushUDPTS(c *check.C, srs *server.Server, file, stream string) {
	c.Helper()
	p := newUDPTSPush(c, srs, file)
	m := mock.Match{Action: "on_publish", Stream: stream}
	start, wait := time.Now(), check.Scaled(600*time.Millisecond)
	for at := 40 * time.Millisecond; len(srs.Hooks.Requests(m)) < 2; at += 40 * time.Millisecond {
		if time.Since(start) > wait {
			c.Fatalf("no new publish of the caster %v after the push started, %v of it sent", time.Since(start), at-40*time.Millisecond)
		}
		c.Must(p.s.SendThrough(p.conn, at), "push")
		for i := 0; i < 8 && len(srs.Hooks.Requests(m)) < 2; i++ {
			time.Sleep(5 * time.Millisecond)
		}
	}
	c.Must(p.s.SendThrough(p.conn, casterLead), "push the lead")
	p.stream()
}

// udpTSPush is a sender of a TS file to the server's MPEG-TS over UDP caster,
// from a UDP socket of its own, which is closed when the test ends.
type udpTSPush struct {
	s       *srt.Sender
	conn    net.Conn
	ctx     context.Context
	done    chan error
	started bool
}

func newUDPTSPush(tb testing.TB, srs *server.Server, file string) *udpTSPush {
	tb.Helper()
	conn, err := net.Dial("udp", srs.Addr(server.UDPTS))
	if err != nil {
		tb.Fatalf("dial the caster: %v", err)
	}
	f, err := os.Open(media.Path(tb, file))
	if err != nil {
		conn.Close()
		tb.Fatalf("open the media: %v", err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	p := &udpTSPush{s: srt.NewSender(f), conn: conn, ctx: ctx, done: make(chan error, 1)}
	tb.Cleanup(func() {
		cancel()
		conn.Close()
		if p.started {
			<-p.done
		}
		f.Close()
	})
	return p
}

// stream sends the rest of the file in real time until the test ends.
func (p *udpTSPush) stream() {
	p.started = true
	go func() { p.done <- p.s.Stream(p.ctx, p.conn, nil, nil) }()
}

// sendUDPTS sends the first d of a TS file, by its PCR, to the server's
// MPEG-TS over UDP caster at once, from a sender of its own that then stops.
func sendUDPTS(c *check.C, srs *server.Server, file string, d time.Duration) {
	c.Helper()
	conn, err := net.Dial("udp", srs.Addr(server.UDPTS))
	c.Must(err, "dial the caster")
	defer conn.Close()
	f, err := os.Open(media.Path(c, file))
	c.Must(err, "open the media")
	defer f.Close()
	c.Must(srt.NewSender(f).SendThrough(conn, d), "push")
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
	// keyAt is when the last keyframe came, and gopAudio whether an audio
	// frame came after it.
	keyAt    time.Time
	gopAudio bool
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
				rec.track(tag)
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

// track keeps when the last keyframe came, and whether an audio frame came
// after it, so SRS's GOP cache has audio.
func (r *rtmpRecorder) track(tag *media.FLVTag) {
	switch {
	case tag.Type == 9 && len(tag.Data) > 1 && tag.Data[0]>>4 == 1 && tag.Data[1] != 0:
		r.keyAt, r.gopAudio = time.Now(), false
	case tag.Type == 8 && len(tag.Data) > 1 && !(tag.Data[0]>>4 == 10 && tag.Data[1] == 0):
		r.gopAudio = true
	}
}

// gopWithAudio waits, out of the case's time and for at most 2s, until SRS's
// GOP cache has audio and its next keyframe, which clears it, is at least
// 300ms away on the stream's 1s GOP, so a player that joins now gets audio
// in its first messages.
func (r *rtmpRecorder) gopWithAudio(c *check.C) {
	c.Helper()
	c.Untimed(func() {
		deadline := time.Now().Add(2 * time.Second)
		for time.Now().Before(deadline) {
			r.mu.Lock()
			ok := r.gopAudio && time.Since(r.keyAt) < 700*time.Millisecond
			r.mu.Unlock()
			if ok {
				return
			}
			time.Sleep(5 * time.Millisecond)
		}
		c.Errorf("the recording player got no GOP with audio in 2s")
	})
}
