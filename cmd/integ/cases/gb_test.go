// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"fmt"
	"io"
	"math/rand/v2"
	"net"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/gb"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/report"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// The error codes of the GB publish API.
const (
	gbStreamBusy  = 1028 // a session of the id or the SSRC exists
	gbDataInvalid = 3008 // the body is not an object with an id and an SSRC, as strings
)

// gbUntil is when a player of a GB stream has seen it play: both sequence
// headers, a keyframe and 5 frames. GB has no metadata.
var gbUntil = report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5}

// gbLead is how much media a GB publisher sends at once before real time:
// SRS holds 2 video and 2 audio frames before it sends any to RTMP.
const gbLead = 200 * time.Millisecond

// gbBurst is how much media a GB publisher that sends no more in real time
// sends at once, enough for gbUntil: SRS sends the frames it holds only when
// more come.
const gbBurst = 2 * gbLead

// gbPublished is how long SRS may take to publish a GB stream once its media
// came, before the limit scale.
const gbPublished = 300 * time.Millisecond

// TestGb_Common checks GB28181 on the Common server, with an external SIP
// server's part played by the case: it asks for a session by the publish API,
// then sends PS over RTP over TCP to the GB port, and SRS publishes it to its
// own RTMP as live/<id>.
func TestGb_Common(t *testing.T) {
	t.Parallel()
	gbSignaling(t)
	gbMedia(t)
	gbLifecycle(t)
	gbBadInput(t)
}

func gbSignaling(t *testing.T) {
	// A session is answered with code 0 and the GB port, over TCP.
	check.Case(t, "PublishAnswer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		a := gbPublish(c, srs, streamName(c), gbSSRC())
		c.CheckEqual(a.Code, 0, "code")
		c.CheckEqual(a.Port, srs.Ports[server.GB], "port")
		c.Check(a.IsTCP, "is_tcp false, want true")
	})

	// A second session of the same id, or of the same SSRC, is refused while
	// the first one waits for its media.
	check.Case(t, "SameIdBusy", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		id := streamName(c)
		c.CheckEqual(gbPublish(c, srs, id, gbSSRC()).Code, 0, "code of the first session")
		a := gbPublish(c, srs, id, gbSSRC())
		c.CheckEqual(a.Code, gbStreamBusy, "code of a second session of the id")
		c.CheckEqual(a.Desc, "StreamBusy", "desc")
		c.CheckEqual(a.Port, 0, "port of a refused session")
	})
	check.Case(t, "SameSsrcBusy", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		ssrc := gbSSRC()
		c.CheckEqual(gbPublish(c, srs, streamName(c), ssrc).Code, 0, "code of the first session")
		c.CheckEqual(gbPublish(c, srs, streamName(c)+"-b", ssrc).Code, gbStreamBusy, "code of a session of another id with the SSRC")
	})

	// A body without an id or an SSRC as strings, or not a JSON object, is
	// refused with code 3008.
	for _, tc := range []struct{ name, body string }{
		{"MissingId", `{"ssrc":"0100000001"}`},
		{"MissingSsrc", `{"id":"cam"}`},
		{"IdNotString", `{"id":1,"ssrc":"0100000001"}`},
		{"SsrcNotString", `{"id":"cam","ssrc":100000001}`},
		{"BodyNotObject", `["cam","0100000001"]`},
		{"BodyNotJson", `id=cam&ssrc=0100000001`},
		{"BodyEmpty", ``},
	} {
		check.Case(t, tc.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			a := gbPost(c, srs, tc.body)
			c.CheckEqual(a.Code, gbDataInvalid, "code")
			c.CheckEqual(a.Port, 0, "port")
			checkGBServes(c, srs)
		})
	}

	// An SSRC that is not a decimal number, or is 0 or above 32 bits, which no
	// media can bind, is refused with code 3008, and the API takes the next
	// session.
	check.Case(t, "SsrcNotNumber", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		for _, ssrc := range []string{"camera", "12a", "-1", " 1", "", "0", "4294967296"} {
			a := gbPost(c, srs, `{"id":"`+streamName(c)+`","ssrc":"`+ssrc+`"}`)
			c.CheckEqual(a.Code, gbDataInvalid, fmt.Sprintf("code of SSRC %q", ssrc))
			c.CheckEqual(a.Port, 0, fmt.Sprintf("port of SSRC %q", ssrc))
		}
		checkGBServes(c, srs)
	})

	// An RTP SSRC takes 32 bits: a session of one above 2^31 gets its media.
	// GB28181 SSRCs, 10 decimal digits that start with 0 or 1, stay below.
	check.Case(t, "SsrcAbove31Bits", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, 3_000_000_000+gbSSRC())
		c.CheckEqual(s.answer.Code, 0, "code")
		s.sendThrough(c, gbLead)
		c.Check(untilPublishing(c, srs, "live/"+s.id, check.Scaled(gbPublished)), "live/%v not published in %v", s.id, check.Scaled(gbPublished))
	})
}

func gbMedia(t *testing.T) {
	// H.264 and AAC in PS play over RTMP: the codecs, the source's size and
	// audio, one sequence header each, timestamps that only go forward, in
	// sync.
	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.lead(c)
		r := playUntil(c, rtmpURL(srs, "live/"+s.id), gbUntil)
		checkGBPlayed(c, r, "h264")
		c.CheckEqual(r.Video.Profile, "Baseline", "profile")
	})

	// An H.264 frame of 2 slices, as an encoder with slices on sends it, goes
	// to RTMP as one message with both slices, a keyframe when they are IDR:
	// a player gets each frame whole, not a message per slice.
	check.Case(t, "SlicedFramesWhole", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264SlicesAAC, gbSSRC())
		for _, f := range s.until(gbLead) {
			if f.Video && annexBSlices(f.Data) != 2 {
				c.Fatalf("a frame of the test file has %v slices, want 2", annexBSlices(f.Data))
			}
		}
		s.lead(c)
		rec := &sliceRecorder{tagPlayer: playerOf(c, rtmpURL(srs, "live/"+s.id))}
		var a flv.Analyzer
		c.Must(readUntil(rec, &a, gbUntil, time.Second), "play")
		checkGBPlayed(c, &a.Report, "h264")
		c.CheckEqual(a.Report.Video.LeadingFrames, 0, "video frames before the first keyframe")
		c.Check(rec.frames > 0 && rec.whole == rec.frames, "%v of %v video messages with both slices of a frame", rec.whole, rec.frames)
	})

	// HEVC and AAC in PS play over RTMP.
	check.Case(t, "PublishPlayHevc", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.HEVCAAC, gbSSRC())
		s.lead(c)
		player := playerOf(c, rtmpURL(srs, "live/"+s.id))
		var a flv.Analyzer
		c.Must(readUntil(player, &a, gbUntil, time.Second), "play")
		checkGBPlayed(c, &a.Report, "hevc")
	})

	// HEVC goes to RTMP players in enhanced RTMP, as SRS sends HEVC from SRT
	// and WebRTC.
	check.Case(t, "HevcEnhancedRtmp", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.HEVCAAC, gbSSRC())
		s.lead(c)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err := flv.PlayRTMP(ctx, rtmpURL(srs, "live/"+s.id), nil, gbUntil)
		c.CheckNoError(err, "play with enhanced RTMP")
	})

	// The stream is published as live/<id> on the default vhost: on_publish
	// has them, and the media starts it.
	check.Case(t, "PublishedAsId", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.sendThrough(c, gbLead)
		h := waitHook(c, srs, "on_publish", s.id)
		c.CheckEqual(h.Field("app"), "live", "app")
		c.CheckEqual(h.Field("vhost"), "__defaultVhost__", "vhost")
		c.Check(untilPublishing(c, srs, "live/"+s.id, check.Scaled(gbPublished)), "live/%v not published in %v", s.id, check.Scaled(gbPublished))
	})
}

func gbLifecycle(t *testing.T) {
	// A player that comes before the session and its media plays once they
	// come.
	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		id := streamName(c)
		player := playerOf(c, rtmpURL(srs, "live/"+id))
		s := gbStartID(c, srs, media.H264BaselineAAC, id, gbSSRC())
		s.lead(c)
		var a flv.Analyzer
		c.Must(readUntil(player, &a, gbUntil, time.Second), "play")
		checkGBPlayed(c, &a.Report, "h264")
	})

	// Several players at once each play it.
	check.Case(t, "ManyPlayers", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.lead(c)
		reports := make([]*report.Report, 3)
		var wg sync.WaitGroup
		for i := range reports {
			wg.Go(func() {
				ctx, cancel := context.WithTimeout(context.Background(), time.Second)
				defer cancel()
				reports[i], _ = flv.PlayRTMP(ctx, rtmpURL(srs, "live/"+s.id), nil, gbUntil)
			})
		}
		wg.Wait()
		for i, r := range reports {
			if c.Check(r != nil, "player %v failed", i) {
				checkGBPlayed(c, r, "h264")
			}
		}
	})

	// When the media connection closes, the session ends: the stream is
	// unpublished, its publisher leaves the API, and the id and SSRC take a
	// new session, whose media plays.
	check.Case(t, "DisconnectCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.sendThrough(c, gbBurst)
		playUntil(c, rtmpURL(srs, "live/"+s.id), gbUntil)
		pub := waitHook(c, srs, "on_publish", s.id)

		s.conn.Close()
		waitHook(c, srs, "on_unpublish", s.id)
		c.Check(untilUnpublished(c, srs, "live/"+s.id, 300*time.Millisecond), "live/%v still published", s.id)
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+pub.Field("client_id")), "the publisher in the API")

		c.CheckEqual(untilGBSession(c, srs, s.id, s.ssrc, 300*time.Millisecond), 0, "code of a new session of the id and SSRC")
		again := gbAttach(c, srs, media.H264BaselineAAC, s.id, s.ssrc)
		again.sendThrough(c, gbBurst)
		checkGBPlayed(c, playUntil(c, rtmpURL(srs, "live/"+s.id), gbUntil), "h264")
	})

	// A second media connection with the session's SSRC, once the first one
	// plays, takes the session over: the stream plays on from it, the first
	// one's close doesn't end the session, and more media plays. The first
	// connection closes only once the player has the second one's keyframe,
	// since SRS ends the session when it reads the close first;
	// CloseWhileReconnecting checks that race.
	check.Case(t, "ReconnectTakesOver", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.sendThrough(c, gbBurst)
		player := playerOf(c, rtmpURL(srs, "live/"+s.id))
		var a flv.Analyzer
		c.Must(readUntil(player, &a, gbUntil, time.Second), "play the first connection")

		first := s.conn
		s.reconnect(c, srs)
		s.sendThrough(c, gbBurst+time.Second)
		c.Must(readUntil(player, &a, report.Until{Keyframes: 2}, time.Second), "play the second connection")
		first.Close()
		s.sendThrough(c, gbBurst+time.Second+gbLead)
		c.Must(readUntil(player, &a, report.Until{VideoFrames: a.Report.Video.Frames + 3}, time.Second), "play on after the first connection closed")
		c.Check(streamPublishing(c, srs, "live/"+s.id), "live/%v not published after the first connection closed", s.id)
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: s.id})), 0, "on_unpublish requests")
	})

	// Media whose SSRC no session has is ignored: the session of another SSRC
	// isn't published, and the connection stays open.
	check.Case(t, "UnknownSsrcIgnored", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		c.Must(gbDial(c, srs, gbSSRC(), s.vcodec).Send(context.Background(), s.until(gbLead), nil), "send with another SSRC")
		checkGBOpen(c, s.conn, 100*time.Millisecond)
		c.Check(!streamPublishing(c, srs, "live/"+s.id), "live/%v published", s.id)
	})
}

func gbBadInput(t *testing.T) {
	// SRS skips bad input it can recover from, at the next PS pack, and the
	// stream plays on: the player gets the next keyframe, and the connection
	// stays open.
	for _, tc := range []struct {
		name string
		bad  func(c *check.C, s *gbStream)
	}{
		// A pack whose headers are broken.
		{"CorruptPackSkipped", func(c *check.C, s *gbStream) {
			m, err := gb.NewMuxer(s.vcodec)
			c.Must(err, "muxer")
			f := s.frames[s.next]
			pack := m.Pack(f)
			for i := 14; i < 24; i++ {
				pack[i] ^= 0xff
			}
			c.Must(s.pub.WriteRTP(pack[:min(len(pack), gb.MaxPayload)], f.PTS, false), "write the broken pack")
		}},
		// An RTP packet of bytes that are no PS.
		{"GarbagePayloadSkipped", func(c *check.C, s *gbStream) {
			c.Must(s.pub.WriteRTP([]byte(strings.Repeat("garbage!", 40)), 0, false), "write garbage")
		}},
		// An RTCP packet, a sender report.
		{"RtcpDropped", func(c *check.C, s *gbStream) {
			sr := []byte{0x80, 200, 0, 6, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			c.Must(s.pub.WriteFramed(sr), "write RTCP")
		}},
	} {
		check.Case(t, tc.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
			s.sendThrough(c, gbBurst)
			player := playerOf(c, rtmpURL(srs, "live/"+s.id))
			var a flv.Analyzer
			c.Must(readUntil(player, &a, gbUntil, time.Second), "play")

			tc.bad(c, s)
			s.sendThrough(c, gbBurst+time.Second)
			c.Must(readUntil(player, &a, report.Until{Keyframes: 2}, time.Second), "play after the bad input")
			checkGBOpen(c, s.conn, 0)
			c.Check(streamPublishing(c, srs, "live/"+s.id), "live/%v not published", s.id)
		})
	}

	// A first packet that is no RTP is skipped before the session is bound.
	check.Case(t, "NotRtpBeforeMedia", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		c.Must(s.pub.WriteFramed([]byte{1, 2, 3}), "write a packet that is no RTP")
		s.lead(c)
		checkGBPlayed(c, playUntil(c, rtmpURL(srs, "live/"+s.id), gbUntil), "h264")
	})

	// SRS ends the session on bad input it can't recover from: it closes the
	// connection, and the id and SSRC take a new session. DisconnectCleanup
	// checks that an ended session's stream is unpublished.
	for _, tc := range []struct {
		name string
		bad  func(c *check.C, s *gbStream)
	}{
		// A frame of length 0.
		{"ZeroLengthEnds", func(c *check.C, s *gbStream) {
			c.Must(s.pub.WriteFramed(nil), "write an empty frame")
		}},
		// A packet over 1,500 bytes that is no PS, which SRS can't tell from
		// a large one with a wrong length.
		{"LargeGarbageEnds", func(c *check.C, s *gbStream) {
			c.Must(s.pub.WriteRTP([]byte(strings.Repeat("garbage!", 400)), 0, false), "write garbage")
		}},
		// 17 packets in a row that are no PS, one more than SRS tries to
		// recover from.
		{"TooMuchGarbageEnds", func(c *check.C, s *gbStream) {
			for range 17 {
				c.Must(s.pub.WriteRTP([]byte(strings.Repeat("garbage!", 40)), 0, false), "write garbage")
			}
		}},
	} {
		check.Case(t, tc.name, check.Normal, func(c *check.C) {
			srs := server.Common(c)
			s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
			s.lead(c)
			playUntil(c, rtmpURL(srs, "live/"+s.id), gbUntil)

			tc.bad(c, s)
			checkGBClosed(c, s.conn)
			c.CheckEqual(untilGBSession(c, srs, s.id, s.ssrc, 300*time.Millisecond), 0, "code of a new session of the id and SSRC")
		})
	}
}

// TestGb_Reconnect checks a device that reconnects while SRS still connects
// the RTMP output of its first media connection. It has a server of its own,
// so a crash in that race takes down no Common shard.
func TestGb_Reconnect(t *testing.T) {
	t.Parallel()
	srs := crashServer(t, server.WithGB(server.PlayConfig(server.Play{}), server.GBOptions{}))

	// The second connection takes the session over, as in
	// ReconnectTakesOver, and SRS stays up.
	check.Case(t, "WhileConnecting", check.Normal, func(c *check.C) {
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.sendThrough(c, gbLead)
		s.reconnect(c, srs)
		s.sendThrough(c, gbBurst+time.Second)
		time.Sleep(50 * time.Millisecond)
		res, err := httpClient.Get(srs.APIURL("/api/v1/versions"))
		if !c.CheckNoError(err, "the API after the second connection") {
			return
		}
		res.Body.Close()
		// When the race doesn't hit, the session may never play again, so
		// the play gives up well inside the case's limit.
		ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
		defer cancel()
		_, err = flv.PlayRTMP(ctx, rtmpURL(srs, "live/"+s.id), nil, report.Until{Keyframes: 1})
		c.CheckNoError(err, "play")
	})

	// A device whose first media connection closes as its second one comes:
	// when SRS reads the close first, it ends the session, and the second
	// connection may come while the session ends. Each try sends the second
	// connection's media a little later after the close, so one of them comes
	// in that moment. SRS stays up, and each session either plays on from its
	// second connection or ends and frees its id and SSRC.
	check.Case(t, "CloseWhileReconnecting", check.Normal, func(c *check.C) {
		const tries = 16
		streams := make([]*gbStream, tries)
		firsts := make([]net.Conn, tries)
		for i := range streams {
			s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
			s.sendThrough(c, gbLead)
			streams[i], firsts[i] = s, s.conn
		}
		for _, s := range streams {
			if !untilPublishing(c, srs, "live/"+s.id, check.Scaled(gbPublished)) {
				c.Fatalf("live/%v not published in %v", s.id, check.Scaled(gbPublished))
			}
			s.reconnect(c, srs)
		}

		var wg sync.WaitGroup
		for i, s := range streams {
			frames := s.frames[s.next:len(s.until(gbBurst))]
			wg.Go(func() {
				firsts[i].Close()
				time.Sleep(time.Duration(i) * 100 * time.Microsecond)
				s.pub.Send(context.Background(), frames, nil)
			})
		}
		wg.Wait()

		time.Sleep(50 * time.Millisecond)
		res, err := httpClient.Get(srs.APIURL("/api/v1/versions"))
		if !c.CheckNoError(err, "the API after the second connections") {
			return
		}
		res.Body.Close()
		for _, s := range streams {
			if !streamPublishing(c, srs, "live/"+s.id) {
				c.CheckEqual(untilGBSession(c, srs, s.id, s.ssrc, 100*time.Millisecond), 0, "code of a new session of live/"+s.id)
			}
		}
	})
}

// crashServer starts a server for a group whose case may crash it, and stops
// it when the group ends, whatever its exit. Its directory is removed when
// the group passes.
func crashServer(t *testing.T, cfg server.Config) *server.Server {
	t.Helper()
	s, err := server.Start(t.Name(), cfg)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		s.Stop()
		if !t.Failed() {
			os.RemoveAll(s.Dir)
		}
	})
	return s
}

// gbMediaTimeout is the media_connect_timeout of the timer server: a session
// whose media doesn't come that long ends.
const gbMediaTimeout = time.Second

// gbSessionTick is how often SRS checks a session.
const gbSessionTick = 300 * time.Millisecond

// sharedSegmentsKeepGB is the GB setting of the SegmentsKeep timer server.
var sharedSegmentsKeepGB = server.GBOptions{MediaConnectTimeout: gbMediaTimeout}

// TestTimer_Gb_MediaTimeout checks media_connect_timeout, 1s, on the shared
// timer server SegmentsKeep, whose other groups use no GB: a session whose
// media doesn't come ends, and frees its id and SSRC.
func TestTimer_Gb_MediaTimeout(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// A session with no media connection ends after the timeout.
	check.Case(t, "NoConnectionEnds", check.Timer, func(c *check.C) {
		id, ssrc := streamName(c), gbSSRC()
		c.CheckEqual(gbPublish(c, srs, id, ssrc).Code, 0, "code")
		checkGBReleased(c, srs, id, ssrc, time.Now())
	})

	// A connection that sends nothing doesn't bind the session, so it ends
	// too.
	check.Case(t, "SilentConnectionEnds", check.Timer, func(c *check.C) {
		id, ssrc := streamName(c), gbSSRC()
		c.CheckEqual(gbPublish(c, srs, id, ssrc).Code, 0, "code")
		start := time.Now()
		gbConn(c, srs)
		checkGBReleased(c, srs, id, ssrc, start)
	})

	// A session whose media came stays past the timeout, and plays on.
	check.Case(t, "MediaKeepsSession", check.Timer, func(c *check.C) {
		s := gbStart(c, srs, media.H264BaselineAAC, gbSSRC())
		s.lead(c)
		r := playUntilWithin(c, rtmpURL(srs, "live/"+s.id), report.Until{VideoHeader: true, AudioHeader: true, Duration: gbLead + gbMediaTimeout*3/2}, 3*time.Second)
		checkGBPlayed(c, r, "h264")
		c.CheckEqual(gbPublish(c, srs, s.id, gbSSRC()).Code, gbStreamBusy, "code of a new session of the id")
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Action: "on_unpublish", Stream: s.id})), 0, "on_unpublish requests")
	})
}

// gbSlowHook is how long a slow hook server takes to accept the RTMP publish
// of a GB stream: longer than SRS's 500ms pulse, shorter than the 1s
// media_connect_timeout of the timer server.
const gbSlowHook = 800 * time.Millisecond

// TestTimer_Gb_RtmpOutput checks the RTMP publish SRS makes of a GB stream to
// itself, on the shared timer server SegmentsKeep, while the hook server
// takes longer than SRS's 500ms pulse to accept it.
func TestTimer_Gb_RtmpOutput(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// The device sends the whole file at once while on_publish is slow, more
	// than the socket buffers between SRS's RTMP output and its RTMP server
	// hold on macOS (Linux grows them to hold it): the output waits for the
	// publish, and once the hook accepts, the session
	// has published the stream once, with no new connection and no
	// unpublish, and a player gets it.
	check.Case(t, "SlowOnPublishOnePublish", check.Timer, func(c *check.C) {
		id := streamName(c)
		srs.Hooks.Script(hookOfStream("on_publish", id), mock.OK().After(gbSlowHook))
		s := gbStartID(c, srs, media.H264HighAAC, id, gbSSRC())
		s.sendThrough(c, 30*time.Second)
		pub := waitHook(c, srs, "on_publish", id)
		time.Sleep(time.Until(pub.Time.Add(gbSlowHook + 300*time.Millisecond)))
		c.CheckEqual(len(srs.Hooks.Requests(hookOfStream("on_connect", id))), 1, "on_connect requests")
		c.CheckEqual(len(srs.Hooks.Requests(hookOfStream("on_publish", id))), 1, "on_publish requests")
		c.CheckEqual(len(srs.Hooks.Requests(hookOfStream("on_unpublish", id))), 0, "on_unpublish requests")
		r := playUntilWithin(c, rtmpURL(srs, "live/"+id), report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1}, 500*time.Millisecond)
		checkGBPlayed(c, r, "h264")
	})
}

// checkGBReleased checks that a session with no media holds its id and SSRC
// until the timeout after start, and frees them by the next check after it.
func checkGBReleased(c *check.C, srs *server.Server, id string, ssrc uint32, start time.Time) {
	c.Helper()
	c.CheckEqual(gbPublish(c, srs, id, ssrc+1).Code, gbStreamBusy, "code of a new session of the id at once")
	deadline := start.Add(gbMediaTimeout + 2*gbSessionTick + 200*time.Millisecond)
	for {
		if gbPublish(c, srs, id, ssrc).Code == 0 {
			break
		}
		if time.Now().After(deadline) {
			c.Fatalf("the session of %v still holds its id after %v", id, time.Since(start))
		}
		time.Sleep(20 * time.Millisecond)
	}
	held := time.Since(start)
	c.Check(held >= gbMediaTimeout-50*time.Millisecond, "the session ended after %v, before the %v timeout", held, gbMediaTimeout)
	c.Logf("the session ended after %v", held)
}

// gbSSRCs hands out the SSRCs of the cases, each its own: 9 digits, which
// GB28181 writes with a leading 0 for live media.
var gbSSRCs = func() *atomic.Uint32 {
	var n atomic.Uint32
	n.Store(100_000_000 + rand.Uint32N(800_000_000))
	return &n
}()

// gbSSRC returns an SSRC of its own for a case.
func gbSSRC() uint32 { return gbSSRCs.Add(1) }

// gbFrameCache holds the frames of each test file, read once.
var gbFrameCache sync.Map

type gbFileFrames struct {
	once   sync.Once
	frames []gb.Frame
	vcodec string
	err    error
}

// gbFrames returns the frames of a test file from its first keyframe on, as
// a device starts, and its video codec.
func gbFrames(c *check.C, file string) ([]gb.Frame, string) {
	c.Helper()
	path := media.Path(c, file)
	v, _ := gbFrameCache.LoadOrStore(path, &gbFileFrames{})
	e := v.(*gbFileFrames)
	e.once.Do(func() {
		f, err := os.Open(path)
		if err != nil {
			e.err = err
			return
		}
		defer f.Close()
		frames, vcodec, err := gb.ReadFLV(f)
		for i, fr := range frames {
			if fr.Video && fr.Key {
				e.frames, e.vcodec = frames[i:], vcodec
				break
			}
		}
		if e.err = err; err == nil && e.frames == nil {
			e.err = errors.New("no keyframe")
		}
	})
	c.Must(e.err, "read "+file)
	return e.frames, e.vcodec
}

// framesUntil is the frames before media time d, from the first one.
func framesUntil(frames []gb.Frame, d time.Duration) []gb.Frame {
	base := frames[0].DTS
	for i, f := range frames {
		if f.DTS-base >= d {
			return frames[:i]
		}
	}
	return frames
}

// gbPost sends a body to the publish API and returns the answer.
func gbPost(c *check.C, srs *server.Server, body string) *gb.Answer {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	a, err := gb.Post(ctx, httpClient, srs.APIURL(""), []byte(body))
	c.Must(err, "publish API")
	return a
}

// gbPublish asks for a session of id and ssrc and returns the answer.
func gbPublish(c *check.C, srs *server.Server, id string, ssrc uint32) *gb.Answer {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	a, err := gb.Publish(ctx, httpClient, srs.APIURL(""), id, ssrc)
	c.Must(err, "publish API")
	return a
}

// gbConn connects to the GB port until the case ends.
func gbConn(c *check.C, srs *server.Server) net.Conn {
	c.Helper()
	conn, err := net.DialTimeout("tcp", srs.Addr(server.GB), time.Second)
	c.Must(err, "dial the GB port")
	c.Cleanup(func() { conn.Close() })
	return conn
}

// gbDial is a new media connection of ssrc.
func gbDial(c *check.C, srs *server.Server, ssrc uint32, vcodec string) *gb.Publisher {
	c.Helper()
	pub, err := gb.NewPublisher(gbConn(c, srs), ssrc, vcodec)
	c.Must(err, "publisher")
	return pub
}

// A gbStream is a session and its media connection, with the frames of a
// test file to send.
type gbStream struct {
	id     string
	ssrc   uint32
	answer *gb.Answer
	conn   net.Conn
	pub    *gb.Publisher
	vcodec string
	frames []gb.Frame
	next   int // the next frame to send
}

// gbStart asks for a session of a stream of its own and connects its media.
func gbStart(c *check.C, srs *server.Server, file string, ssrc uint32) *gbStream {
	c.Helper()
	return gbStartID(c, srs, file, streamName(c), ssrc)
}

// gbStartID is gbStart for a stream id.
func gbStartID(c *check.C, srs *server.Server, file, id string, ssrc uint32) *gbStream {
	c.Helper()
	answer := gbPublish(c, srs, id, ssrc)
	s := gbAttach(c, srs, file, id, ssrc)
	s.answer = answer
	return s
}

// gbAttach connects the media of a session the case has asked for.
func gbAttach(c *check.C, srs *server.Server, file, id string, ssrc uint32) *gbStream {
	c.Helper()
	s := &gbStream{id: id, ssrc: ssrc}
	s.frames, s.vcodec = gbFramesOf(c, file)
	s.conn = gbConn(c, srs)
	var err error
	s.pub, err = gb.NewPublisher(s.conn, ssrc, s.vcodec)
	c.Must(err, "publisher")
	return s
}

// reconnect sends the next frames on a new media connection of the SSRC.
func (s *gbStream) reconnect(c *check.C, srs *server.Server) {
	c.Helper()
	s.conn = gbConn(c, srs)
	var err error
	s.pub, err = gb.NewPublisher(s.conn, s.ssrc, s.vcodec)
	c.Must(err, "publisher")
}

// until is the frames from the first one before media time d.
func (s *gbStream) until(d time.Duration) []gb.Frame {
	return framesUntil(s.frames, d)
}

// sendThrough sends at once the frames not sent yet before media time d.
func (s *gbStream) sendThrough(c *check.C, d time.Duration) {
	c.Helper()
	end := len(s.until(d))
	if end <= s.next {
		return
	}
	c.Must(s.pub.Send(context.Background(), s.frames[s.next:end], nil), "send the media")
	s.next = end
}

// lead sends gbLead at once, then the rest in real time until the case ends.
func (s *gbStream) lead(c *check.C) {
	c.Helper()
	s.leadBy(c, gbLead)
}

// leadBy sends the frames before media time d at once, then the rest in real
// time until the case ends.
func (s *gbStream) leadBy(c *check.C, d time.Duration) {
	c.Helper()
	s.sendThrough(c, d)
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		defer close(done)
		s.pub.Send(ctx, s.frames[s.next:], &rtmp.Pacer{})
	}()
	c.Cleanup(func() {
		cancel()
		s.conn.Close()
		<-done
	})
}

// checkGBPlayed checks the report of a player of a GB stream of a test file:
// its codecs, the source's size and audio, one sequence header each, and
// timestamps that only go forward, without gaps, in sync.
func checkGBPlayed(c *check.C, r *report.Report, vcodec string) {
	c.Helper()
	if !c.Check(r.Video != nil && r.Audio != nil, "no video or audio: %v", r) {
		return
	}
	checkVideo(c, r, vcodec)
	checkAudio(c, r, "aac", 1)
	c.Check(r.Video.Keyframes >= 1, "no keyframe: %v", r)
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
	c.Check(r.AVSync <= 100*time.Millisecond, "A/V sync %v, want at most 100ms", r.AVSync)
}

// annexBSlices counts the slices, IDR or not, of an H.264 frame in Annex B.
func annexBSlices(b []byte) int {
	n := 0
	for i := 0; i+3 < len(b); i++ {
		if b[i] == 0 && b[i+1] == 0 && b[i+2] == 1 {
			if t := b[i+3] & 0x1f; t == 1 || t == 5 {
				n++
			}
			i += 2
		}
	}
	return n
}

// sliceRecorder counts the H.264 frames a player reads, and those whose
// message has 2 slices, the NALUs of an AVC tag with their 4-byte sizes.
type sliceRecorder struct {
	tagPlayer
	frames, whole int
}

func (r *sliceRecorder) ReadTag() (*media.FLVTag, error) {
	tag, err := r.tagPlayer.ReadTag()
	if err != nil {
		return nil, err
	}
	// A legacy FLV AVC tag: the codec id 7, the packet type 1 for a frame,
	// the composition time in 24 bits, then the NALUs.
	if d := tag.Data; tag.Type == media.TagVideo && len(d) >= 5 && d[0]&0x0f == 7 && d[1] == 1 {
		r.frames++
		slices := 0
		for p := 5; p+4 < len(d); {
			size := int(d[p])<<24 | int(d[p+1])<<16 | int(d[p+2])<<8 | int(d[p+3])
			if t := d[p+4] & 0x1f; t == 1 || t == 5 {
				slices++
			}
			p += 4 + size
		}
		if slices == 2 {
			r.whole++
		}
	}
	return tag, nil
}

// checkGBOpen checks that SRS keeps a media connection open: nothing to read
// and no close within d, at least 10ms.
func checkGBOpen(c *check.C, conn net.Conn, d time.Duration) {
	c.Helper()
	conn.SetReadDeadline(time.Now().Add(max(d, 10*time.Millisecond)))
	defer conn.SetReadDeadline(time.Time{})
	_, err := conn.Read(make([]byte, 1))
	c.Check(isTimeout(err), "the media connection: %v, want it open", err)
}

// checkGBClosed checks that SRS closes a media connection within 300ms.
func checkGBClosed(c *check.C, conn net.Conn) {
	c.Helper()
	conn.SetReadDeadline(time.Now().Add(300 * time.Millisecond))
	defer conn.SetReadDeadline(time.Time{})
	_, err := io.Copy(io.Discard, conn)
	c.Check(err == nil || !isTimeout(err), "the media connection: %v, want it closed", err)
}

// untilPublishing polls the API until a stream is published, for at most d.
func untilPublishing(c *check.C, srs *server.Server, stream string, d time.Duration) bool {
	c.Helper()
	deadline := time.Now().Add(d)
	for !streamPublishing(c, srs, stream) {
		if time.Now().After(deadline) {
			return false
		}
		time.Sleep(10 * time.Millisecond)
	}
	return true
}

// untilUnpublished polls the API until a stream is not published, for at
// most d.
func untilUnpublished(c *check.C, srs *server.Server, stream string, d time.Duration) bool {
	c.Helper()
	deadline := time.Now().Add(d)
	for streamPublishing(c, srs, stream) {
		if time.Now().After(deadline) {
			return false
		}
		time.Sleep(5 * time.Millisecond)
	}
	return true
}

// playUntilWithin plays a stream until u, for at most d scaled by the limit
// scale.
func playUntilWithin(c *check.C, url string, u report.Until, d time.Duration) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(d))
	defer cancel()
	r, err := flv.PlayRTMP(ctx, url, nil, u)
	c.Must(err, "play")
	return r
}

// checkGBServes checks that the publish API still takes a session.
func checkGBServes(c *check.C, srs *server.Server) {
	c.Helper()
	c.CheckEqual(gbPublish(c, srs, streamName(c), gbSSRC()).Code, 0, "code of a new session")
}

// untilGBSession asks for a session of id and ssrc until it gets one, for at
// most d, since SRS frees an ended session's id a moment after it ends, and
// returns the last code.
func untilGBSession(c *check.C, srs *server.Server, id string, ssrc uint32, d time.Duration) int {
	c.Helper()
	deadline := time.Now().Add(d)
	for {
		code := gbPublish(c, srs, id, ssrc).Code
		if code != gbStreamBusy || time.Now().After(deadline) {
			return code
		}
		time.Sleep(10 * time.Millisecond)
	}
}
