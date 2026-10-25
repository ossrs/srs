// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"sort"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// TestRtc_Common publishes by WHIP and plays by WHEP, or by the legacy JSON
// API, on the Common server. The publisher sends the H.264 baseline and Opus
// test file, its first 200ms at once and the rest in real time. SRS keeps no
// GOP for WebRTC players, so a case connects its players before the publisher
// sends its first keyframe.
func TestRtc_Common(t *testing.T) {
	t.Parallel()

	// WhipAnswer checks the WHIP answer: 201 with an SDP that receives H.264
	// and Opus on SRS's UDP port, and a Location to DELETE.
	check.Case(t, "WhipAnswer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		checkCreated(c, srs, &pub.Session, "live", stream)
		checkMedia(c, pub.Answer, "recvonly")
	})

	// WhepAnswer checks the WHEP answer: 201 with an SDP that sends H.264
	// and Opus, and a Location to DELETE.
	check.Case(t, "WhepAnswer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		checkCreated(c, srs, &player.Session, "live", stream)
		checkMedia(c, player.Answer, "sendonly")
	})

	check.Case(t, "PublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// WhipPlayPath plays by /rtc/v1/whip-play/, SRS's other WHEP path.
	check.Case(t, "WhipPlayPath", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, srs.APIURL("/rtc/v1/whip-play/?app=live&stream="+stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// StreamNaming publishes app live with stream a/b/<stream>: like RTMP,
	// the last part is the stream and the rest the app, live/a/b.
	check.Case(t, "StreamNaming", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", "a/b/"+stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		c.CheckEqual(h.Field("app"), "live/a/b", "on_publish app")
		c.CheckEqual(h.Field("stream"), stream, "on_publish stream")
		q := locationQuery(c, pub.Location)
		c.CheckEqual(q.Get("app"), "live/a/b", "location app")
		c.CheckEqual(q.Get("stream"), stream, "location stream")

		player := rtcPlayer(c, whepURL(srs, "live/a/b", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// DeletePublisher deletes the publisher's session: SRS answers 200,
	// unpublishes the stream, and the publisher and the stream are gone from
	// the API; the stream can be published again.
	check.Case(t, "DeletePublisher", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")

		res := rtcDelete(c, &pub.Session)
		c.CheckEqual(res.StatusCode, http.StatusOK, "DELETE status")
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after DELETE")
		c.CheckNoError(untilGone(c, srs, streamPath), "the stream after DELETE")
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
	})

	// DeletePlayer deletes a player's session: SRS answers 200, the player
	// is gone from the API, and the stream counts only the publisher.
	check.Case(t, "DeletePlayer", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_play", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the player in the API")
		c.CheckEqual(streamClients(c, srs, streamPath), 2, "stream clients with a player")
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		res := rtcDelete(c, &player.Session)
		c.CheckEqual(res.StatusCode, http.StatusOK, "DELETE status")
		waitHook(c, srs, "on_stop", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after DELETE")
		c.CheckEqual(streamClients(c, srs, streamPath), 1, "stream clients after DELETE")
	})

	// DeleteWrongToken deletes a session with another token: SRS refuses it,
	// and the session goes on.
	check.Case(t, "DeleteWrongToken", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		// Only the token changes; the rest of the query stays as SRS wrote
		// it, since SRS reads the session as it is, not URL-decoded.
		token := locationQuery(c, pub.Location).Get("token")
		wrong := strings.Replace(pub.Location, "token="+token, "token=wrong", 1)
		c.Check(wrong != pub.Location, "no token=%v in %v", token, pub.Location)
		req, err := http.NewRequest(http.MethodDelete, wrong, nil)
		c.Must(err, "DELETE request")
		res, err := httpClient.Do(req)
		c.Must(err, "DELETE")
		res.Body.Close()
		c.Check(res.StatusCode != http.StatusOK, "DELETE with a wrong token: status %v, want it refused", res.StatusCode)
		r := player.Report()
		waitPlayed(c, player, report.Until{VideoFrames: r.Video.Frames + 3})
	})

	// LegacyPublishPlay publishes by /rtc/v1/publish/ and plays by
	// /rtc/v1/play/: each answer has code 0, the SDP and a session id.
	check.Case(t, "LegacyPublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := "webrtc://127.0.0.1/live/" + streamName(c)
		pub := rtcPublisher(c, srs.APIURL("/rtc/v1/publish/"), &rtc.Options{Legacy: stream})
		player := rtcPlayer(c, srs.APIURL("/rtc/v1/play/"), &rtc.Options{Legacy: stream})
		for _, s := range []*rtc.Session{&pub.Session, &player.Session} {
			c.Check(s.SessionID != "", "no sessionid")
			c.CheckEqual(s.Response.StatusCode, http.StatusOK, "status")
		}
		checkMedia(c, pub.Answer, "recvonly")
		checkMedia(c, player.Answer, "sendonly")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// LegacyBadBody posts a body that is not JSON: the legacy API answers
	// HTTP 200 with code 400, as SRS documents for it.
	check.Case(t, "LegacyBadBody", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		for _, path := range []string{"/rtc/v1/publish/", "/rtc/v1/play/"} {
			res, err := httpClient.Post(srs.APIURL(path), "application/json", strings.NewReader("not json"))
			c.Must(err, "POST "+path)
			var v struct {
				Code int `json:"code"`
			}
			err = json.NewDecoder(res.Body).Decode(&v)
			res.Body.Close()
			c.CheckNoError(err, "decode "+path)
			c.CheckEqual(res.StatusCode, http.StatusOK, path+" status")
			c.CheckEqual(v.Code, 400, path+" code")
		}
	})

	check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// PlayAnsweredBeforePublish answers a player before the publisher comes,
	// and lets it connect only once the publisher has started: SRS answered
	// it with the tracks of a play before publishing, and subscribes it
	// after the stream changed to the publisher's, so it must take the
	// publisher's tracks then, or its keyframe requests and the publisher's
	// packets find no track of it.
	check.Case(t, "PlayAnsweredBeforePublish", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		held := &heldAnswer{answered: make(chan struct{}), release: make(chan struct{})}
		release := sync.OnceFunc(func() { close(held.release) })
		var player *rtc.Player
		var playErr error
		done := make(chan struct{})
		go func() {
			defer close(done)
			ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
			defer cancel()
			player, playErr = rtc.Play(ctx, whepURL(srs, "live", stream), &rtc.Options{Client: &http.Client{Transport: held}})
		}()
		c.Cleanup(func() {
			release()
			<-done
			if player != nil {
				player.Close()
			}
		})
		select {
		case <-held.answered:
		case <-done:
		}
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		// The publisher has started once a player's keyframe request reaches it.
		requestKeyframe(c, pub, rtcPlayer(c, whepURL(srs, "live", stream), nil), "the player after the publisher")
		release()
		<-done
		c.Must(playErr, "WHEP play")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	check.Case(t, "ManyPlayers", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)

		const n = 4
		players, errs := make([]*rtc.Player, n), make([]error, n)
		var wg sync.WaitGroup
		for i := range n {
			wg.Add(1)
			go func() {
				defer wg.Done()
				ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
				defer cancel()
				players[i], errs[i] = rtc.Play(ctx, whepURL(srs, "live", stream), &rtc.Options{Client: httpClient})
			}()
		}
		wg.Wait()
		for i, p := range players {
			if p != nil {
				c.Cleanup(func() { p.Close() })
			}
			c.Must(errs[i], fmt.Sprintf("player %d", i))
		}
		receivers := make([]rtcReceiver, n)
		for i, p := range players {
			receivers[i] = p
		}
		sendRTC(c, pub, receivers...)
		for _, p := range players {
			checkRTCPlayed(c, waitPlayed(c, p, rtcPlayedUntil))
		}
	})

	// SecondPublisherRejected publishes a stream that is already published:
	// SRS refuses the second offer with 409, and the first stream still
	// plays.
	check.Case(t, "SecondPublisherRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		second, err := rtc.Publish(ctx, whipURL(srs, "live", stream), &rtc.Options{Client: httpClient})
		if second != nil {
			second.Close()
		}
		var se *rtc.StatusError
		c.Check(errors.As(err, &se) && se.Code == http.StatusConflict, "second publisher: %v, want status 409", err)
		r := player.Report()
		waitPlayed(c, player, report.Until{VideoFrames: r.Video.Frames + 3})
	})

	// RepublishPlays deletes the publisher and publishes the stream again: a
	// player that stays connected plays the new publish from its keyframe.
	check.Case(t, "RepublishPlays", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		stop := sendRTC(c, pub, player)
		waitPlayed(c, player, rtcPlayedUntil)

		stop()
		rtcDelete(c, &pub.Session)
		waitHook(c, srs, "on_unpublish", stream)
		r := player.Report()
		pub = rtcPublisher(c, whipURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		after := waitPlayed(c, player, report.Until{Keyframes: r.Video.Keyframes + 1, VideoFrames: r.Video.Frames + 3})
		c.CheckEqual(after.Video.SequenceHeaders, 1, "video sequence headers of the same file")
	})

	// PublisherCloseCleanup closes the publisher's peer connection without
	// DELETE: SRS ends the session on its DTLS alert, and the publisher and
	// the stream are gone from the API.
	check.Case(t, "PublisherCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_publish", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")
		c.CheckEqual(apiCode(c, srs, streamPath), 0, "the stream in the API")

		pub.Close()
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after it closed")
		c.CheckNoError(untilGone(c, srs, streamPath), "the stream after its publisher closed")
	})

	// PlayerCloseCleanup closes a player's peer connection without DELETE:
	// the player is gone from the API, and the stream counts only the
	// publisher.
	check.Case(t, "PlayerCloseCleanup", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		h := waitHook(c, srs, "on_play", stream)
		clientPath, streamPath := "/api/v1/clients/"+h.Field("client_id"), "/api/v1/streams/"+h.Field("stream_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the player in the API")

		player.Close()
		waitHook(c, srs, "on_stop", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the player after it closed")
		c.CheckEqual(streamClients(c, srs, streamPath), 1, "stream clients after the player closed")
	})

	rtcErrors(t)
	rtcParams(t)
	rtcCodecs(t)
	rtcTransport(t)
	rtcRebind(t)
	rtcRecovery(t)
	rtcLoss(t)
}

// whipURL is the WHIP URL of a stream.
func whipURL(srs *server.Server, app, stream string) string {
	return srs.APIURL("/rtc/v1/whip/?app=" + app + "&stream=" + stream)
}

// whepURL is the WHEP URL of a stream.
func whepURL(srs *server.Server, app, stream string) string {
	return srs.APIURL("/rtc/v1/whep/?app=" + app + "&stream=" + stream)
}

// rtcPublisher connects a publisher, closed when the case ends.
func rtcPublisher(c *check.C, url string, opts *rtc.Options) *rtc.Publisher {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	pub, err := rtc.Publish(ctx, url, rtcOptions(opts))
	c.Must(err, "WHIP publish")
	c.Cleanup(func() { pub.Close() })
	return pub
}

// rtcPlayer connects a player, closed when the case ends.
func rtcPlayer(c *check.C, url string, opts *rtc.Options) *rtc.Player {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	player, err := rtc.Play(ctx, url, rtcOptions(opts))
	c.Must(err, "WHEP play")
	c.Cleanup(func() { player.Close() })
	return player
}

// rtcOptions are the options with the cases' HTTP client.
func rtcOptions(opts *rtc.Options) *rtc.Options {
	o := rtc.Options{}
	if opts != nil {
		o = *opts
	}
	if o.Client == nil {
		o.Client = httpClient
	}
	return &o
}

// rtcMedia holds the samples of each WebRTC test file, read once.
var rtcMedia struct {
	mu    sync.Mutex
	files map[string]*rtcFile
}

type rtcFile struct {
	once    sync.Once
	samples []rtc.Sample
	err     error
}

// rtcFileSamples are the samples of a WebRTC test file: an MP4 file, or a
// G.711 file, read with the video of the WHIP test file.
func rtcFileSamples(c *check.C, name string) []rtc.Sample {
	c.Helper()
	path := media.Path(c, name)
	rtcMedia.mu.Lock()
	if rtcMedia.files == nil {
		rtcMedia.files = map[string]*rtcFile{}
	}
	f := rtcMedia.files[name]
	if f == nil {
		f = &rtcFile{}
		rtcMedia.files[name] = f
	}
	rtcMedia.mu.Unlock()
	if name == media.PCMU || name == media.PCMA {
		video := rtcFileSamples(c, media.H264BaselineOpus)
		f.once.Do(func() {
			var audio []rtc.Sample
			if audio, f.err = rtc.ReadG711(path); f.err != nil {
				return
			}
			for _, s := range video {
				if s.Video {
					f.samples = append(f.samples, s)
				}
			}
			f.samples = append(f.samples, audio...)
			sort.SliceStable(f.samples, func(i, j int) bool { return f.samples[i].DTS < f.samples[j].DTS })
		})
	} else {
		f.once.Do(func() { f.samples, f.err = rtc.ReadMP4(path) })
	}
	c.Must(f.err, "read the media "+name)
	return f.samples
}

// rtcSamples are the samples of the WHIP test file.
func rtcSamples(c *check.C) []rtc.Sample {
	c.Helper()
	return rtcFileSamples(c, media.H264BaselineOpus)
}

// sendRTC sends the WHIP test file, with sendRTCSamples.
func sendRTC(c *check.C, pub rtcSender, players ...rtcReceiver) (stop func()) {
	c.Helper()
	return sendRTCSamples(c, rtcSamples(c), pub, players...)
}

// sendRTCSamples sends samples from their start, the first 200ms at once,
// like a stream that has run for that long, and the rest in real time, until
// the case ends or the returned stop is called. First, each player asks for a
// keyframe, and the publisher waits until SRS forwards the request: SRS
// subscribes a player after its DTLS handshake, on a coroutine of its own,
// and drops what the publisher sends before, so the request shows the
// player gets the file from its first keyframe. A request before the
// subscription goes nowhere, so the player asks until one comes through.
func sendRTCSamples(c *check.C, samples []rtc.Sample, pub rtcSender, players ...rtcReceiver) (stop func()) {
	c.Helper()
	for i, p := range players {
		requestKeyframe(c, pub, p, fmt.Sprintf("player %d", i))
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	lead := 0
	for lead < len(samples) && samples[lead].DTS < rtcLead {
		lead++
	}
	go func() {
		defer close(done)
		if pub.Send(ctx, samples[:lead], nil) == nil {
			pub.Send(ctx, samples[lead:], &rtmp.Pacer{})
		}
	}()
	stop = func() {
		cancel()
		<-done
	}
	c.Cleanup(stop)
	return stop
}

// requestKeyframe has a player ask for a keyframe until the publisher gets
// the request. A request that comes before SRS has subscribed the player
// goes nowhere, so the player asks again every 100ms, for at most 1s.
func requestKeyframe(c *check.C, pub rtcSender, p rtcReceiver, name string) {
	c.Helper()
	n := pub.PLIs()
	var err error
	for try := 0; try < 10; try++ {
		c.Must(p.RequestKeyframe(), name+" requests a keyframe")
		ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
		err = pub.WaitPLI(ctx, n)
		cancel()
		if err == nil {
			break
		}
	}
	c.Must(err, "the publisher gets "+name+"'s keyframe request")
}

// A heldAnswer is an HTTP transport that holds each answer until release is
// closed, so a WebRTC session that SRS has answered connects only then.
type heldAnswer struct {
	answered, release chan struct{}
	once              sync.Once
}

func (h *heldAnswer) RoundTrip(req *http.Request) (*http.Response, error) {
	res, err := httpClient.Transport.RoundTrip(req)
	h.once.Do(func() { close(h.answered) })
	<-h.release
	return res, err
}

// An rtcSender is a WebRTC publisher that sendRTC sends the test file with:
// pion's, or a raw one that sends RTP in clear.
type rtcSender interface {
	PLIs() int
	WaitPLI(ctx context.Context, n int) error
	Send(ctx context.Context, samples []rtc.Sample, pacer *rtmp.Pacer) error
}

// An rtcReceiver is a WebRTC player: pion's, or a raw one that reads RTP in
// clear.
type rtcReceiver interface {
	RequestKeyframe() error
	Report() *report.Report
	Until(ctx context.Context, u report.Until) (*report.Report, error)
}

// rtcLead is the media sendRTC sends at once.
const rtcLead = 200 * time.Millisecond

// rtcPlayedUntil is when a WebRTC player has seen a stream play: the
// parameter sets, a keyframe, 5 video frames and 5 Opus packets.
var rtcPlayedUntil = report.Until{VideoHeader: true, Keyframes: 1, VideoFrames: 5, AudioFrames: 5}

// waitPlayed waits at most 1s until a player's report meets u.
func waitPlayed(c *check.C, p rtcReceiver, u report.Until) *report.Report {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	r, err := p.Until(ctx, u)
	c.Must(err, "play")
	return r
}

// checkRTCPlayed checks the report of a player of the WHIP test file: H.264
// baseline 768x320 with one sequence header from the first frame, Opus
// 48 kHz stereo, and timestamps that only go forward, without gaps.
func checkRTCPlayed(c *check.C, r *report.Report) {
	c.Helper()
	if !c.Check(r.Video != nil && r.Audio != nil, "no video or audio: %v", r) {
		return
	}
	v, a := r.Video, r.Audio
	c.CheckEqual(v.Codec, "h264", "video codec")
	c.CheckEqual(v.Profile, "Baseline", "video profile")
	c.Check(v.Width == 768 && v.Height == 320, "size %vx%v, want 768x320", v.Width, v.Height)
	c.CheckEqual(v.SequenceHeaders, 1, "video sequence headers")
	c.CheckEqual(v.LeadingFrames, 0, "frames before the first keyframe")
	c.CheckEqual(a.Codec, "opus", "audio codec")
	c.Check(a.SampleRate == 48000 && a.Channels == 2, "audio %vHz %vch, want 48000Hz 2ch", a.SampleRate, a.Channels)
	c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
}

// checkCreated checks a WHIP or WHEP answer: 201 with an SDP, and a Location
// to DELETE that names the stream, a session and its token.
func checkCreated(c *check.C, srs *server.Server, s *rtc.Session, app, stream string) {
	c.Helper()
	c.CheckEqual(s.Response.StatusCode, http.StatusCreated, "status")
	c.CheckEqual(s.Response.Header.Get("Content-Type"), "application/sdp", "content type")
	loc, err := url.Parse(s.Location)
	c.Must(err, "location")
	c.CheckEqual(loc.Host, srs.Addr(server.HTTPAPI), "location host")
	c.CheckEqual(loc.Path, "/rtc/v1/whip/", "location path")
	q := loc.Query()
	c.CheckEqual(q.Get("action"), "delete", "location action")
	c.CheckEqual(q.Get("app"), app, "location app")
	c.CheckEqual(q.Get("stream"), stream, "location stream")
	c.Check(q.Get("token") != "" && q.Get("session") != "", "location %v has no token or session", s.Location)
	host := fmt.Sprintf("127.0.0.1 %d typ host", srs.Ports[server.RTCUDP])
	c.Check(strings.Contains(s.Answer, host), "answer has no candidate %q", host)
	c.Check(strings.Contains(s.Answer, "a=fingerprint:sha-256 "), "answer has no sha-256 fingerprint")
}

// checkMedia checks an answer has an H.264 video and an Opus audio section,
// each in the direction SRS takes.
func checkMedia(c *check.C, answer, direction string) {
	c.Helper()
	sections := strings.Split(answer, "\nm=")[1:]
	c.CheckEqual(len(sections), 2, "media sections")
	for _, m := range sections {
		codec := "H264/90000"
		if strings.HasPrefix(m, "audio") {
			codec = "opus/48000/2"
		}
		c.Check(strings.Contains(m, codec), "section %q has no %v", firstLine(m), codec)
		c.Check(strings.Contains(m, "a="+direction), "section %q is not %v", firstLine(m), direction)
	}
}

func firstLine(s string) string {
	line, _, _ := strings.Cut(s, "\n")
	return strings.TrimSpace(line)
}

// locationQuery is the query of a session's Location.
func locationQuery(c *check.C, location string) url.Values {
	c.Helper()
	loc, err := url.Parse(location)
	c.Must(err, "location")
	return loc.Query()
}

// rtcDelete sends DELETE to a session's Location.
func rtcDelete(c *check.C, s *rtc.Session) *http.Response {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	res, err := s.Delete(ctx)
	c.Must(err, "DELETE")
	return res
}
